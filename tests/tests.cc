#include "check.hh"

#include "test_maker.hh"
#include <type_traits>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cxxabi.h>
#include <memory>
#include <ostream>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <posets/concepts.hh>
#include <posets/downsets.hh>
#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 128;
size_t posets::vectors::bitset_threshold = 128;

using test_value_type = std::int8_t;

#define il std::initializer_list<test_value_type>

namespace {
  template <typename Lhs, typename Rhs>
  void assert_same_vector_values (const Lhs& lhs, const Rhs& rhs) {
    CHECK (lhs.size () == rhs.size ());
    for (size_t i = 0; i < lhs.size (); ++i)
      CHECK (lhs[i] == rhs[i]);

    std::vector<test_value_type> lhs_values (lhs.size ());
    std::vector<test_value_type> rhs_values (rhs.size ());
    lhs.to_vector (lhs_values);
    rhs.to_vector (rhs_values);
    CHECK (lhs_values == rhs_values);
  }

  void test_x_and_wordvec () {
    using inner_type = posets::vectors::vector_backed_sum<test_value_type>;
    using bool_type = posets::vectors::x_and_boolvec<inner_type>;
    using word_type = posets::vectors::x_and_wordvec<inner_type>;

    const size_t previous_bool_threshold = posets::vectors::bool_threshold;
    constexpr size_t inner_size = 7;
    posets::vectors::bool_threshold = inner_size;

    std::mt19937_64 random {0x58a6d17b2c9043efULL};
    std::uniform_int_distribution<int> inner_value {-8, 8};
    std::uniform_int_distribution<int> bool_value {-1, 0};
    constexpr std::array<size_t, 6> tail_lengths {0, 1, 63, 64, 65, 1000};

    for (const size_t tail_length : tail_lengths) {
      const size_t vector_size = inner_size + tail_length;

      const word_type sized (vector_size);
      CHECK (sized.unused_trailing_bits_are_zero ());

      for (size_t round = 0; round < 48; ++round) {
        std::vector<test_value_type> lhs_values (vector_size);
        std::vector<test_value_type> rhs_values (vector_size);
        for (size_t i = 0; i < inner_size; ++i) {
          lhs_values[i] = static_cast<test_value_type> (inner_value (random));
          rhs_values[i] = static_cast<test_value_type> (inner_value (random));
        }
        for (size_t i = inner_size; i < vector_size; ++i) {
          lhs_values[i] = static_cast<test_value_type> (bool_value (random));
          rhs_values[i] = static_cast<test_value_type> (bool_value (random));
        }

        // In addition to unrelated pairs, force both domination directions and equality.
        switch (round % 4) {
          case 1:
            for (size_t i = 0; i < vector_size; ++i)
              rhs_values[i] = std::min (lhs_values[i], rhs_values[i]);
            break;
          case 2:
            for (size_t i = 0; i < vector_size; ++i)
              rhs_values[i] = std::max (lhs_values[i], rhs_values[i]);
            break;
          case 3: rhs_values = lhs_values; break;
          default: break;
        }

        const bool_type bool_lhs {std::span<const test_value_type> {lhs_values}};
        const bool_type bool_rhs {std::span<const test_value_type> {rhs_values}};
        const word_type word_lhs {std::span<const test_value_type> {lhs_values}};
        const word_type word_rhs {std::span<const test_value_type> {rhs_values}};

        CHECK (word_lhs.unused_trailing_bits_are_zero ());
        CHECK (word_rhs.unused_trailing_bits_are_zero ());
        assert_same_vector_values (word_lhs, bool_lhs);
        assert_same_vector_values (word_rhs, bool_rhs);
        CHECK (word_lhs.cached_sum () == bool_lhs.cached_sum ());
        CHECK (word_rhs.cached_sum () == bool_rhs.cached_sum ());
        CHECK (word_lhs.bin () == bool_lhs.bin ());
        CHECK (word_rhs.bin () == bool_rhs.bin ());

        {
          auto word_order = word_lhs.partial_order (word_rhs);
          auto bool_order = bool_lhs.partial_order (bool_rhs);
          CHECK (word_order.leq () == bool_order.leq ());
          CHECK (word_order.geq () == bool_order.geq ());
        }
        {
          auto word_order = word_rhs.partial_order (word_lhs);
          auto bool_order = bool_rhs.partial_order (bool_lhs);
          CHECK (word_order.geq () == bool_order.geq ());
          CHECK (word_order.leq () == bool_order.leq ());
        }

        CHECK ((word_lhs == word_rhs) == (bool_lhs == bool_rhs));
        CHECK ((word_lhs != word_rhs) == (bool_lhs != bool_rhs));
        CHECK ((word_lhs < word_rhs) == (bool_lhs < bool_rhs));
        CHECK ((word_rhs < word_lhs) == (bool_rhs < bool_lhs));

        auto word_meet = word_lhs.meet (word_rhs);
        auto bool_meet = bool_lhs.meet (bool_rhs);
        CHECK (word_meet.unused_trailing_bits_are_zero ());
        assert_same_vector_values (word_meet, bool_meet);
        CHECK (word_meet.cached_sum () == bool_meet.cached_sum ());

        auto word_join = word_lhs.join (word_rhs);
        auto bool_join = bool_lhs.join (bool_rhs);
        CHECK (word_join.unused_trailing_bits_are_zero ());
        assert_same_vector_values (word_join, bool_join);
        CHECK (word_join.cached_sum () == bool_join.cached_sum ());

        auto word_meet_in_place = word_lhs.copy ();
        auto bool_meet_in_place = bool_lhs.copy ();
        word_meet_in_place.meet_with (word_rhs);
        bool_meet_in_place.meet_with (bool_rhs);
        CHECK (word_meet_in_place.unused_trailing_bits_are_zero ());
        CHECK (word_meet_in_place == word_meet);
        assert_same_vector_values (word_meet_in_place, bool_meet_in_place);

        auto word_join_in_place = word_lhs.copy ();
        auto bool_join_in_place = bool_lhs.copy ();
        word_join_in_place.join_with (word_rhs);
        bool_join_in_place.join_with (bool_rhs);
        CHECK (word_join_in_place.unused_trailing_bits_are_zero ());
        CHECK (word_join_in_place == word_join);
        assert_same_vector_values (word_join_in_place, bool_join_in_place);

        auto word_copy = word_lhs.copy ();
        CHECK (word_copy.unused_trailing_bits_are_zero ());
        CHECK (word_copy == word_lhs);
        auto word_moved = std::move (word_copy);
        CHECK (word_moved.unused_trailing_bits_are_zero ());
        assert_same_vector_values (word_moved, bool_lhs);
        word_moved = word_rhs.copy ();
        CHECK (word_moved.unused_trailing_bits_are_zero ());
        assert_same_vector_values (word_moved, bool_rhs);

        std::ostringstream word_output;
        std::ostringstream bool_output;
        word_lhs.print (word_output);
        bool_lhs.print (bool_output);
        CHECK (word_output.str () == bool_output.str ());
      }
    }

    posets::vectors::bool_threshold = 2;
    const word_type word_initialized (il {4, -3, -1, 0, -1});
    const bool_type bool_initialized (il {4, -3, -1, 0, -1});
    CHECK (word_initialized.unused_trailing_bits_are_zero ());
    assert_same_vector_values (word_initialized, bool_initialized);

    posets::vectors::bool_threshold = previous_bool_threshold;
  }
}

template <typename SetType>
struct test_t : public generic_test<void> {
    using v_type = SetType::value_type;

    std::vector<v_type> vvtovv (const std::vector<std::vector<test_value_type>>& vv) {
      std::vector<v_type> out;
      out.reserve (vv.size ());

      for (const auto& value : vv)
        out.emplace_back (v_type (value));
      return out;
    }

    SetType vec_to_set (std::vector<v_type>&& v) { return SetType (std::move (v)); }

    void twodim () {
      std::cout << "Create set" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 0}));

        vec_to_set (std::move (e1));
      }

      std::cout << "Create set loop" << '\n';
      {
        for (int i = 1; i <= 8; ++i) {
          std::cout << "Inserting " << i << " (-1, 0)\n";
          std::vector<v_type> e1;
          e1.reserve (static_cast<size_t> (i) + 1);
          for (int j = 0; j < i; ++j)
            e1.emplace_back (v_type (il {-1, 0}));
          e1.emplace_back (v_type (il {0, -1}));
          vec_to_set (std::move (e1));
        }
      }

      std::cout << "Create set 5" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {0, -1}));

        vec_to_set (std::move (e1));
      }
    }

    void threedim () {
      const v_type v1 (il {1, 2, 3});
      const v_type v2 (il {2, 5, 1});
      const v_type v3 (il {4, 1, 1});
      const v_type v4 (il {0, 1, 2});

      auto meet = v1.meet (v2);
      auto join = v1.join (v2);
      CHECK (meet == v_type (il {1, 2, 1}));
      CHECK (join == v_type (il {2, 5, 3}));
      auto meet_in_place = v1.copy ();
      meet_in_place.meet_with (v2);
      CHECK (meet_in_place == meet);
      auto join_in_place = v1.copy ();
      join_in_place.join_with (v2);
      CHECK (join_in_place == join);

      // singleton checks
      std::cout << "Singleton checks" << '\n';
      CHECK (v1.size () > 0);

      SetType set_one_elt (v1.copy ());
      CHECK (set_one_elt.contains (v1));
      CHECK (set_one_elt.contains (v4));
      CHECK (not set_one_elt.contains (v2));
      CHECK (not set_one_elt.contains (v3));

      {
        std::vector<v_type> e1;
        e1.emplace_back (v1.copy ());
        e1.emplace_back (v1.copy ());
        SetType set = vec_to_set (std::move (e1));
        CHECK (set.contains (v1));
        CHECK (set.contains (v4));
        CHECK (not set.contains (v2));
        CHECK (not set.contains (v3));
      }

      // singleton union test
      std::cout << "Singleton union test" << '\n';
      SetType set_one_elt_cpy (v1.copy ());
      set_one_elt.union_with (std::move (set_one_elt_cpy));
      CHECK (set_one_elt.size () == 1);
      CHECK (set_one_elt.contains (v1));
      CHECK (set_one_elt.contains (v4));
      CHECK (not set_one_elt.contains (v2));
      CHECK (not set_one_elt.contains (v3));

      std::cout << "Intersect that has duplicate meet" << '\n';
      {
        const v_type v1 (il {2, 2, 4});
        const v_type v2 (il {2, 1, 5});
        const v_type v3 (il {1, 3, 0});
        const v_type v4 (il {3, 1, 0});

        std::vector<v_type> e1;
        e1.emplace_back (v1.copy ());
        e1.emplace_back (v2.copy ());
        SetType set = vec_to_set (std::move (e1));

        std::vector<v_type> e2;
        e2.emplace_back (v3.copy ());
        e2.emplace_back (v4.copy ());
        SetType set2 = vec_to_set (std::move (e2));

        set.intersect_with (std::move (set2));

        CHECK (set.contains (v_type (il {1, 1, 0})));
        CHECK (not set.contains (v_type (il {1, 3, 0})));
      }

      std::cout << "Intersect that reduces to a single element" << '\n';
      {
        const v_type v1 (il {2, 4, 1, 8, 5, 4, 1, 8, 1, 8});
        const v_type v2 (il {2, 4, 1, 8, 7, 4, 1, 10, 2, 8});
        std::vector<v_type> e1;
        std::vector<v_type> e2;
        e1.emplace_back (v1.copy ());
        SetType s1 = vec_to_set (std::move (e1));
        e2.emplace_back (v2.copy ());
        SetType s2 = vec_to_set (std::move (e2));

        s1.intersect_with (std::move (s2));

        CHECK (s1.contains (v1));
        CHECK (not s1.contains (v2));
        CHECK (s1.size () == 1);
      }

      std::cout << "Create set" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {-1, 1}));
        e1.emplace_back (v_type (il {-1, 0}));
        e1.emplace_back (v_type (il {0, -1}));

        vec_to_set (std::move (e1));
      }

      // simple intersect
      std::cout << "Simple intersection test" << '\n';
      {
        std::vector<v_type> e1;
        e1.emplace_back (v1.copy ());
        e1.emplace_back (v1.copy ());
        SetType set = vec_to_set (std::move (e1));
        std::vector<v_type> e2;
        e2.emplace_back (v4.copy ());
        set.intersect_with (vec_to_set (std::move (e2)));
        CHECK (not set.contains (v1));
        CHECK (set.contains (v4));
        CHECK (not set.contains (v2));
        CHECK (not set.contains (v3));
      }

      // appply test
      std::cout << "Apply test" << '\n';

      SetType set_one_elt_cpy2 (v1.copy ());
      set_one_elt.intersect_with (std::move (set_one_elt_cpy2));
      set_one_elt = set_one_elt.apply ([] (const v_type& v) { return v.copy (); });
      CHECK (set_one_elt.contains (v1));
      CHECK (set_one_elt.contains (v4));
      CHECK (not set_one_elt.contains (v2));
      CHECK (not set_one_elt.contains (v3));

      // construction test
      std::cout << "Construction test" << '\n';
      std::vector<v_type> elements;
      elements.emplace_back (v1.copy ());
      elements.emplace_back (v2.copy ());
      elements.emplace_back (v3.copy ());
      SetType set = vec_to_set (std::move (elements));
      const v_type v5 (il {1, 1, 10});
      CHECK (set.contains (v1));
      CHECK (set.contains (v3));
      CHECK (set.contains (v4));
      CHECK (not set.contains (v5));
      CHECK (set.contains (v2));

      // union test
      std::cout << "Union test" << '\n';
      std::vector<v_type> elements_cpy;
      elements_cpy.emplace_back (v1.copy ());
      elements_cpy.emplace_back (v2.copy ());
      elements_cpy.emplace_back (v3.copy ());
      std::cout << "creating the union operand" << '\n';
      SetType set_cpy = vec_to_set (std::move (elements_cpy));
      std::cout << "ready to union" << '\n';
      set.union_with (std::move (set_cpy));

      // More domination tests
      std::cout << "Domination tests" << '\n';
      auto tree = vec_to_set (vvtovv ({
          {1, 2, 3},
          {2, 5, 1},
          {4, 1, 1},
      }));
      // CHECK (tree.size () == 3);
      CHECK (tree.contains (v_type (il {1, 2, 1})));
      CHECK (tree.contains (v_type (il {1, 1, 1})));
      CHECK (tree.contains (v_type (il {2, 1, 1})));

      // Another apply test
      std::cout << "Apply test" << '\n';
      set = set.apply ([] (const v_type& v) { return v.copy (); });
      CHECK (set.contains (v2));
      CHECK (set.contains (v4));
      CHECK (not set.contains (v5));

      // More union tests
      std::cout << "Union tests" << '\n';
      std::vector<v_type> others;
      others.emplace_back (v4.copy ());
      others.emplace_back (v5.copy ());
      std::cout << "creating union operand" << '\n';
      SetType set2 = vec_to_set (std::move (others));
      std::cout << "ready to union" << '\n';
      set.union_with (std::move (set2));

      CHECK (set.contains (v2));
      CHECK (set.contains (v4));
      CHECK (set.contains (v5));

      // More construction and domination tests
      std::cout << "Construction + domination tests" << '\n';
      std::vector<v_type> others2;
      others2.emplace_back (v4.copy ());
      others2.emplace_back (v5.copy ());
      SetType other_set = vec_to_set (std::move (others2));
      CHECK (not other_set.contains (v2));
      CHECK (other_set.contains (v4));
      CHECK (other_set.contains (v5));
    }

    void fivedim () {
      std::cout << "Final tests" << '\n';
      {
        auto tree = vec_to_set (
            vvtovv ({{7, 0, 9, 9, 7}, {8, 0, 7, 7, 8}, {8, 0, 9, 9, 8}, {9, 0, 7, 7, 9}}));
        // CHECK (tree.size () == 4);
        CHECK (tree.contains (v_type (il {0, 0, 0, 0, 0})));
        CHECK (tree.contains (v_type (il {6, 0, 9, 9, 7})));
        CHECK (tree.contains (v_type (il {7, 0, 9, 9, 7})));
      }

      auto first = vec_to_set (vvtovv ({{7, 0, 9, 9, 7}, {8, 0, 9, 9, 6}, {9, 0, 7, 7, 9}}));
      auto frontier = vec_to_set (vvtovv ({{7, 0, 9, 9, 7}, {8, 0, 9, 9, 5}, {9, 0, 7, 7, 9}}));

      std::cout << "Preparing to intersect" << '\n';
      frontier.intersect_with (std::move (first));

      CHECK (frontier.contains (v_type (il {8, 0, 9, 9, 4})));
      CHECK (frontier.contains (v_type (il {8, 0, 9, 9, 5})));
      CHECK (not frontier.contains (v_type (il {8, 0, 9, 9, 6})));
      CHECK (frontier.contains (v_type (il {7, 0, 9, 9, 7})));
      CHECK (not frontier.contains (v_type (il {7, 0, 9, 9, 8})));
      CHECK (frontier.contains (v_type (il {9, 0, 7, 7, 9})));
      CHECK (not frontier.contains (v_type (il {9, 0, 7, 7, 10})));
    }

    void sixteendim () {
      auto frontier = vec_to_set (vvtovv ({{0, 7, 0, 0, 9, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {0, 8, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 8, -1, 0, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 7, -1, 0, -1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                           {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}));
      CHECK (frontier.contains (v_type (il {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));
      CHECK (
          not frontier.contains (v_type (il {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0})));
    }

    void operator() () override {
      twodim ();
      threedim ();
      fivedim ();
      sixteendim ();
    }
};

namespace {
  void usage (const char* progname) {
    std::cout << "usage: " << progname << " SETTYPE VECTYPE" << '\n';

    std::cout << "  SETTYPE:\n  - all" << '\n';
    for (auto&& s : set_names) {
      auto start = s.find_last_of (':') + 1;
      std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << '\n';
    }

    std::cout << "  VECTYPE:\n  - all" << '\n';
    for (auto&& s : vector_names) {
      auto start = s.find_last_of (':') + 1;
      std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << '\n';
    }

    exit (0);
  }
}

namespace posets::vectors {
  template <typename T>
  using array_backed_fixed = posets::vectors::array_backed<T, 10>;

  template <typename T>
  using array_backed_sum_fixed = posets::vectors::array_backed_sum<T, 10>;

  template <typename T>
  using simd_array_backed_fixed = posets::vectors::simd_array_backed<T, 10>;

  template <typename T>
  using simd_array_ptr_backed_fixed = posets::vectors::simd_array_ptr_backed<T, 10>;

  template <typename T>
  using simd_array_backed_sum_fixed = posets::vectors::simd_array_backed_sum<T, 10>;

  template <typename T>
  using simd_array_ptr_backed_sum_fixed = posets::vectors::simd_array_ptr_backed_sum<T, 10>;

  template <typename T>
  using simd_vector_and_bitset_backed =
      posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<T>, 1>;

  template <typename T>
  using simd_vector_and_boolvec_backed =
      posets::vectors::x_and_boolvec<posets::vectors::simd_vector_backed<T>>;

  template <typename T>
  using simd_vector_and_wordvec_backed =
      posets::vectors::x_and_wordvec<posets::vectors::simd_vector_backed<T>>;
}

VECTOR_TYPES (posets::vectors::vector_backed<test_value_type>,
              posets::vectors::array_backed_fixed<test_value_type>,
              posets::vectors::array_backed_sum_fixed<test_value_type>,
              posets::vectors::simd_vector_backed<test_value_type>,
              posets::vectors::simd_array_backed_fixed<test_value_type>,
              posets::vectors::simd_array_ptr_backed_fixed<test_value_type>,
              posets::vectors::simd_array_backed_sum_fixed<test_value_type>,
              posets::vectors::simd_array_ptr_backed_sum_fixed<test_value_type>,
              posets::vectors::simd_vector_and_bitset_backed<test_value_type>,
              posets::vectors::simd_vector_and_boolvec_backed<test_value_type>,
              posets::vectors::simd_vector_and_wordvec_backed<test_value_type>);

using set_types = template_type_list<
    posets::downsets::bboxtree_backed, posets::downsets::comptrie_backed,
    posets::downsets::sharingtree_backed, posets::downsets::simple_sharingtree_backed,
    posets::downsets::sharingtrie_backed, posets::downsets::cst_backed,
    posets::downsets::kdtree_backed, posets::downsets::rank_bucketed_vector_backed,
    posets::downsets::filtered_vector_backed, posets::downsets::skiplist_backed,
    posets::downsets::vector_or_kdtree_backed, posets::downsets::set_backed,
    posets::downsets::vector_backed, posets::downsets::vector_backed_bin,
    posets::downsets::vector_backed_one_dim_split_intersection_only>;

int main (int argc, char* argv[]) {
  test_x_and_wordvec ();
  register_maker (static_cast<vector_types*> (nullptr), static_cast<set_types*> (nullptr));

  if (argc != 3)
    usage (argv[0]);

  auto implem =
      std::string ("posets::downsets::") + argv[1] + "<posets::vectors::" + argv[2] + ">";

  try {
    posets::vectors::bool_threshold = 128;
    posets::vectors::bitset_threshold = 128;
    auto& tests = test_list<void>::list;
    tests[implem]();
    return 0;
  } catch (std::bad_function_call& e) {
    std::cout << "error: no such implem: " << implem << '\n';
    return 1;
  }
}
