#include <cassert>
#include <span>
#include <memory>
#include <ostream>
#include <set>
#include <vector>
#include <string>
#include <type_traits>
#include <cxxabi.h>

#include "test_maker.hh"

#include <posets/concepts.hh>
#include <posets/downsets.hh>
#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 128;
size_t posets::vectors::bitset_threshold = 128;

#define il std::initializer_list<char>

template<typename SetType>
struct test_t : public generic_test<void> {
    using VType = typename SetType::value_type;

    std::vector<VType> vvtovv (const std::vector<std::vector<char>>& vv) {
      std::vector<VType> out;
      //out.reserve (vv.size ());

      for (size_t i = 0; i < vv.size (); ++i)
        out.emplace_back(VType (std::move (vv[i]))); //out[i] = VType<VType> (vv[i]);
      return out;
    }

    SetType vec_to_set (std::vector<VType>&& v) {
      return SetType (std::move (v));
    }

    void twodim() {
      std::cout << "Create set" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {0, -1}));

        vec_to_set (std::move (e1));
      }
      std::cout << "Create set" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 0}));

        vec_to_set (std::move (e1));
      }

      std::cout << "Create set loop" << std::endl;
      {
        for (int i = 1; i <= 8; ++i) {
          std::cout << "Inserting " << i << " (-1, 0)\n";
          std::vector<VType> e1;
          for (int j = 0; j < i; ++j)
            e1.emplace_back (VType (il {-1, 0}));
          e1.emplace_back (VType (il {0, -1}));
          vec_to_set (std::move (e1));
        }
      }

      std::cout << "Create set 5" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {0, -1}));

        vec_to_set (std::move (e1));
      }
    }

    void threedim() {
      VType v1 (il {1, 2, 3});
      VType v2 (il {2, 5, 1});
      VType v3 (il {4, 1, 1});
      VType v4 (il {0, 1, 2});

      // singleton checks
      std::cout << "Singleton checks" << std::endl;
      assert(v1.size () > 0);

      SetType set_one_elt (v1.copy ());
      assert (set_one_elt.contains (v1));
      assert (set_one_elt.contains (v4));
      assert (not set_one_elt.contains (v2));
      assert (not set_one_elt.contains (v3));

      {
        std::vector<VType> e1;
        e1.emplace_back(v1.copy ());
        e1.emplace_back(v1.copy ());
        SetType set = vec_to_set (std::move (e1));
        assert (set.contains (v1));
        assert (set.contains (v4));
        assert (not set.contains (v2));
        assert (not set.contains (v3));
      }

      // singleton union test
      std::cout << "Singleton union test" << std::endl;
      SetType set_one_elt_cpy (v1.copy ());
      set_one_elt.union_with (std::move (set_one_elt_cpy));
      assert (set_one_elt.size () == 1);
      assert (set_one_elt.contains (v1));
      assert (set_one_elt.contains (v4));
      assert (not set_one_elt.contains (v2));
      assert (not set_one_elt.contains (v3));

      std::cout << "Intersect that has duplicate meet" << std::endl;
      {
        VType v1 (il {2, 2, 4});
        VType v2 (il {2, 1, 5});
        VType v3 (il {1, 3, 0});
        VType v4 (il {3, 1, 0});

        std::vector<VType> e1;
        e1.emplace_back(v1.copy ());
        e1.emplace_back(v2.copy ());
        SetType set = vec_to_set (std::move (e1));

        std::vector<VType> e2;
        e2.emplace_back(v3.copy ());
        e2.emplace_back(v4.copy ());
        SetType set2 = vec_to_set (std::move (e2));

        set.intersect_with (std::move (set2));

        assert (set.contains (VType (il {1, 1, 0})));
        assert (not set.contains (VType (il {1, 3, 0})));
      }

      std::cout << "Intersect that reduces to a single element" << std::endl;
      {
        VType v1 (il {2, 4, 1, 8, 5, 4, 1, 8, 1, 8});
        VType v2 (il {2, 4, 1, 8, 7, 4, 1, 10, 2, 8});
        std::vector<VType> e1, e2;
        e1.emplace_back(v1.copy ());
        SetType s1 = vec_to_set (std::move (e1));
        e2.emplace_back(v2.copy ());
        SetType s2 = vec_to_set (std::move (e2));

        s1.intersect_with (std::move (s2));

        assert (s1.contains (v1));
        assert (not s1.contains (v2));
        assert (s1.size () == 1);
      }

      std::cout << "Create set" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {-1, 1}));
        e1.emplace_back (VType (il {-1, 0}));
        e1.emplace_back (VType (il {0, -1}));

        vec_to_set (std::move (e1));
      }

      // simple intersect
      std::cout << "Simple intersection test" << std::endl;
      {
        std::vector<VType> e1;
        e1.emplace_back(v1.copy ());
        e1.emplace_back(v1.copy ());
        SetType set = vec_to_set (std::move (e1));
        std::vector<VType> e2;
        e2.emplace_back(v4.copy ());
        set.intersect_with (vec_to_set (std::move (e2)));
        assert (not set.contains (v1));
        assert (set.contains (v4));
        assert (not set.contains (v2));
        assert (not set.contains (v3));
      }

      // appply test
      std::cout << "Apply test" << std::endl;

      SetType set_one_elt_cpy2 (v1.copy ());
      set_one_elt.intersect_with (std::move (set_one_elt_cpy2));
      set_one_elt = set_one_elt.apply ([] (const VType& v) { return v.copy (); });
      assert (set_one_elt.contains (v1));
      assert (set_one_elt.contains (v4));
      assert (not set_one_elt.contains (v2));
      assert (not set_one_elt.contains (v3));

      // construction test
      std::cout << "Construction test" << std::endl;
      std::vector<VType> elements;
      elements.emplace_back(v1.copy ());
      elements.emplace_back(v2.copy ());
      elements.emplace_back(v3.copy ());
      SetType set = vec_to_set (std::move (elements));
      VType v5 (il {1, 1, 10});
      assert(set.contains(v1));
      assert(set.contains(v3));
      assert(set.contains(v4));
      assert(!set.contains(v5));
      assert(set.contains(v2));

      // union test
      std::cout << "Union test" << std::endl;
      std::vector<VType> elements_cpy;
      elements_cpy.emplace_back(v1.copy ());
      elements_cpy.emplace_back(v2.copy ());
      elements_cpy.emplace_back(v3.copy ());
      std::cout << "creating the union operand" << std::endl;
      SetType set_cpy = vec_to_set (std::move (elements_cpy));
      std::cout << "ready to union" << std::endl;
      set.union_with (std::move (set_cpy));

      // More domination tests
      std::cout << "Domination tests" << std::endl;
      auto tree = vec_to_set (vvtovv ({
            {1, 2, 3},
            {2, 5, 1},
            {4, 1, 1},
          }));
      //assert (tree.size () == 3);
      assert (tree.contains (VType (il {1, 2, 1})));
      assert (tree.contains (VType (il {1, 1, 1})));
      assert (tree.contains (VType (il {2, 1, 1})));

      // Another apply test
      std::cout << "Apply test" << std::endl;
      set = set.apply ([] (const VType& v) { return v.copy (); });
      assert(set.contains(v2));
      assert(set.contains(v4));
      assert(!set.contains(v5));

      // More union tests
      std::cout << "Union tests" << std::endl;
      std::vector<VType> others;
      others.emplace_back(v4.copy ());
      others.emplace_back(v5.copy ());
      std::cout << "creating union operand" << std::endl;
      SetType set2 = vec_to_set (std::move (others));
      std::cout << "ready to union" << std::endl;
      set.union_with (std::move (set2));

      assert(set.contains(v2));
      assert(set.contains(v4));
      assert(set.contains(v5));

      // More construction and domination tests
      std::cout << "Construction + domination tests" << std::endl;
      std::vector<VType> others2;
      others2.emplace_back(v4.copy ());
      others2.emplace_back(v5.copy ());
      SetType other_set = vec_to_set (std::move (others2));
      assert(!other_set.contains(v2));
      assert(other_set.contains(v4));
      assert(other_set.contains(v5));

    }

    void fivedim() {
      std::cout << "Final tests" << std::endl;
      {
        auto tree = vec_to_set (vvtovv ({
              {7, 0, 9, 9, 7},
              {8, 0, 7, 7, 8},
              {8, 0, 9, 9, 8},
              {9, 0, 7, 7, 9}
            }));
        //assert (tree.size () == 4);
        assert (tree.contains (VType (il {0, 0, 0, 0, 0})));
        assert (tree.contains (VType (il {6, 0, 9, 9, 7})));
        assert (tree.contains (VType (il {7, 0, 9, 9, 7})));
      }


      // Full set is so slow, this will never finish.
      if constexpr (std::is_same<SetType, posets::downsets::full_set<VType>>::value)
        return;


      auto F1i = vec_to_set (vvtovv ({
            {7, 0, 9, 9, 7},
            {8, 0, 9, 9, 6},
            {9, 0, 7, 7, 9}
          }));
      auto F = vec_to_set (vvtovv ({
            {7, 0, 9, 9, 7},
            {8, 0, 9, 9, 5},
            {9, 0, 7, 7, 9}
          }));

      std::cout << "Preparing to intersect" << std::endl;
      F.intersect_with (std::move (F1i));

      assert (F.contains (VType (il {8, 0, 9 ,9, 4})));
      assert (F.contains (VType (il {8, 0, 9 ,9, 5})));
      assert (not F.contains (VType (il {8, 0, 9 ,9, 6})));
      assert (F.contains (VType (il {7, 0, 9 ,9, 7})));
      assert (not F.contains (VType (il {7, 0, 9 ,9, 8})));
      assert (F.contains (VType (il {9, 0, 7 ,7, 9})));
      assert (not F.contains (VType (il {9, 0, 7 ,7, 10})));

    }

    void sixteendim() {
      auto F = vec_to_set (vvtovv ({
            {0, 7, 0, 0, 9, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 8, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 8, -1, 0, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 7, -1, 0, -1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
          }));
      assert (F.contains (VType (il {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));
      assert (not F.contains (VType (il {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0})));
    }

    void operator() () {
      twodim();
      threedim();
      fivedim();
      sixteendim();
    }

};



void usage (const char* progname) {
  std::cout << "usage: " << progname << " SETTYPE VECTYPE" << std::endl;

  std::cout << "  SETTYPE:\n  - all" << std::endl;
  for (auto&& s : set_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << std::endl;
  }

  std::cout << "  VECTYPE:\n  - all" << std::endl;
  for (auto&& s : vector_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << std::endl;
  }

  exit (0);
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
  using simd_vector_and_bitset_backed = posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<T>, 1>;

  template <typename T>
  using simd_vector_and_boolvec_backed = posets::vectors::x_and_boolvec<posets::vectors::simd_vector_backed<T>>;
}

#define DEFINE_VECTOR_NAME(V) template <> struct vector_name<V> { static constexpr auto str = #V; };
#define DEFINE_VECTOR_NAMES(...)
#define VECTOR_TYPES(...) FOR_EACH(DEFINE_VECTOR_NAME, __VA_ARGS__)     \
  using vector_types = type_list<__VA_ARGS__>;

VECTOR_TYPES (posets::vectors::vector_backed<char>,
              posets::vectors::array_backed_fixed<char>,
              posets::vectors::array_backed_sum_fixed<char>,
              posets::vectors::simd_vector_backed<char>,
              posets::vectors::simd_array_backed_fixed<char>,
              posets::vectors::simd_array_ptr_backed_fixed<char>,
              posets::vectors::simd_array_backed_sum_fixed<char>,
              posets::vectors::simd_array_ptr_backed_sum_fixed<char>,
              posets::vectors::simd_vector_and_bitset_backed<char>,
              posets::vectors::simd_vector_and_boolvec_backed<char>);

using set_types = template_type_list<//posets::downsets::full_set, ; too slow.
  posets::downsets::sharingtree_backed,
  posets::downsets::simple_sharingtree_backed,
  posets::downsets::sharingtrie_backed,
  posets::downsets::kdtree_backed,
  posets::downsets::skiplist_backed,
  posets::downsets::vector_or_kdtree_backed,
  posets::downsets::set_backed,
  posets::downsets::vector_backed,
  posets::downsets::vector_backed_bin,
  posets::downsets::vector_backed_one_dim_split_intersection_only>;



int main(int argc, char* argv[]) {
  register_maker ((vector_types*) 0, (set_types*) 0);

  if (argc != 3)
    usage (argv[0]);

  auto implem = std::string ("posets::downsets::") + argv[1]
    + "<posets::vectors::" + argv[2] + ">";

  try {
    posets::vectors::bool_threshold = 128;
    posets::vectors::bitset_threshold = 128;
    auto& tests = test_list<void>::list;
    tests[implem] ();
    return 0;
  } catch (std::bad_function_call& e) {
    std::cout << "error: no such implem: " << implem << std::endl;
    return 1;
  }
}
