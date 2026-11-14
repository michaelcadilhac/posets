#include "allocation-failure.hh"
#include "matrix-check.hh"

#include <iostream>

#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/filtered_vector_backed.hh>
#include <posets/downsets/vector_backed.hh>
#include <posets/downsets/vector_backed_one_dim_split_intersection_only.hh>
#include <posets/utils/antichain.hh>
#include <posets/vectors.hh>

namespace {
  using namespace matrix_check;

  struct injected_failure {};

  class observed_vector : public posets::vectors::vector_backed<int> {
      using base = posets::vectors::vector_backed<int>;
      explicit observed_vector (base&& value) : base (std::move (value)) {}

      [[nodiscard]] static base&& moving (observed_vector& value) {
        if (++moves == fail_move)
          throw injected_failure {};
        value.was_moved = true;
        return std::move (value);
      }

    public:
      using base::base;
      static inline int comparisons = 0;
      static inline int moves = 0;
      static inline int fail_comparison = 0;
      static inline int fail_move = 0;
      bool was_moved = false;

      observed_vector (observed_vector&& value) : base (moving (value)) {}
      observed_vector& operator= (observed_vector&& value) {
        base::operator= (moving (value));
        was_moved = false;
        return *this;
      }
      [[nodiscard]] auto partial_order (const observed_vector& other) const {
        if (++comparisons == fail_comparison)
          throw injected_failure {};
        return base::partial_order (other);
      }
      [[nodiscard]] observed_vector copy () const { return observed_vector (base::copy ()); }
      [[nodiscard]] observed_vector meet (const observed_vector& other) const {
        return observed_vector (base::meet (other));
      }
      [[nodiscard]] observed_vector join (const observed_vector& other) const {
        return observed_vector (base::join (other));
      }
      static void reset () { comparisons = moves = fail_comparison = fail_move = 0; }
  };
  static_assert (posets::Vector<observed_vector>);

  template <class S>
  [[nodiscard]] region snapshot (const S& subject) {
    region result;
    for (const auto& value : subject)
      result.push_back (value.was_moved ? logical {} : unpack (value));
    return result;
  }

  template <template <class> class Backend>
  void insertion_boundaries () {
    using v = observed_vector;
    using set = Backend<v>;
    const region rows {{2, 0}, {0, 2}, {1, 1}};

    for (const logical& query : {logical {1, 0}, logical {2, 0}, logical {0, 1}}) {
      auto subject = build<set> (rows);
      auto candidate = vector<v> (query);
      v::reset ();
      require (not subject.insert (std::move (candidate)), "dominated insertion accepted");
      require (v::moves == 0 and v::comparisons == (query == logical {0, 1} ? 2 : 1),
               "rejection must stop at first witness without moving");
      require (snapshot (subject) == rows and unpack (candidate) == query,
               "rejected insertion mutated a source");
    }

    // Throws before removal, before compaction, and after compaction pin the
    // existing basic guarantee. Later throws do not roll earlier moves back.
    for (int comparison = 1; comparison <= 3; ++comparison) {
      auto subject = build<set> (rows);
      subject.get_backing_vector ().reserve (8);
      auto candidate = vector<v> ({3, 1});
      v::reset ();
      v::fail_comparison = comparison;
      bool threw = false;
      try {
        (void) subject.insert (std::move (candidate));
      } catch (const injected_failure&) {
        threw = true;
      }
      v::reset ();
      require (threw, "comparison failure not reached");
      const region expected = comparison == 3 ? region {{0, 2}, {}, {1, 1}} : rows;
      require (snapshot (subject) == expected and unpack (candidate) == logical {3, 1},
               "comparison exception boundary changed");
    }

    for (int move = 1; move <= 2; ++move) {
      auto subject = build<set> (rows);
      subject.get_backing_vector ().reserve (8);
      auto candidate = vector<v> ({3, 1});
      v::reset ();
      v::fail_move = move;
      bool threw = false;
      try {
        (void) subject.insert (std::move (candidate));
      } catch (const injected_failure&) {
        threw = true;
      }
      v::reset ();
      require (threw, "move failure not reached");
      require (snapshot (subject) == (move == 1 ? rows : region {{0, 2}}),
               "compaction/append move exception boundary changed");
      require (unpack (candidate) == logical {3, 1}, "throwing move consumed candidate");
    }

    auto subject = build<set> (rows);
    auto candidate = vector<v> ({3, 1});
    require (subject.insert (std::move (candidate)), "dominating insertion rejected");
    require (snapshot (subject) == region {{0, 2}, {3, 1}}, "survivor order changed");
    check (subject, {{0, 2}, {3, 1}}, {{0, 2}, {3, 1}, {2, 2}, {4, 1}});
  }

  void filtered_exception_cache () {
    using v = observed_vector;
    using set = posets::downsets::filtered_vector_backed<v>;
    const region rows {{3, 0}, {0, 3}, {1, 2}, {2, 1}};
    auto subject = build<set> (rows);
    const auto query = vector<v> ({0, 3});
    require (subject.contains (query), "initial filtered cache");
    auto candidate = vector<v> ({4, 1});
    v::reset ();
    v::fail_move = 3;  // Two survivor assignments, then append.
    bool threw = false;
    try {
      (void) subject.insert (std::move (candidate));
    } catch (const injected_failure&) {
      threw = true;
    }
    v::reset ();
    require (threw and snapshot (subject) == region {{0, 3}, {1, 2}},
             "filtered append exception boundary");
    check (subject, {{0, 3}, {1, 2}}, {{0, 3}, {1, 2}, {3, 0}, {2, 1}});
  }

  void diagnostics () {
    using v = observed_vector;
    std::vector<v> values;
    for (const logical& row : {logical {2, 0}, logical {0, 2}, logical {1, 1}})
      values.push_back (vector<v> (row));
    v::reset ();
    allocations_left = 0;
    const bool antichain = posets::utils::is_antichain (values);
    allocations_left = -1;
    require (antichain and v::comparisons == 3 and v::moves == 0,
             "diagnostic must compare every unordered pair without moving/allocating");
    values.push_back (vector<v> ({2, 0}));
    v::reset ();
    require (not posets::utils::is_antichain (values) and v::comparisons == 3,
             "diagnostic duplicate/first-witness order");
    v::reset ();
  }

  void bbox_boundaries () {
    using v = observed_vector;
    using set = posets::downsets::bboxtree_backed<v>;
    const region left {{4, 0}, {0, 4}, {2, 2}};
    const region right {{3, 1}, {1, 3}};
    region intersection;
    for (const auto& x : left)
      for (const auto& y : right)
        intersection.push_back ({std::min (x[0], y[0]), std::min (x[1], y[1])});
    for (int kind = 0; kind < 3; ++kind) {
      bool completed = false;
      for (int site = 0; site < 512; ++site) {
        auto subject = build<set> (left);
        const auto other = build<set> (right);
        const auto original = snapshot (subject);
        v::reset ();
        bool threw = false;
        try {
          if (kind == 0)
            allocations_left = site;
          else if (kind == 1)
            v::fail_comparison = site + 1;
          else
            v::fail_move = site + 1;
          subject.intersect_with (other);
        } catch (const std::bad_alloc&) {
          threw = true;
        } catch (const injected_failure&) {
          threw = true;
        }
        allocations_left = -1;
        v::reset ();
        check (other, right, left);
        const bool published = snapshot (subject) != original;
        // The existing BBox debug antichain check runs after publication and
        // can throw. All earlier failures leave both live operands unchanged.
        require (not published or kind == 1 or not threw, "early BBox publication");
        check (subject, published ? intersection : left, right);
        if (published and threw)
          std::cout << "BBox post-publication comparison site=" << site << '\n';
        if (not threw) {
          check (subject, intersection, left);
          std::cout << "BBox failure kind=" << kind << " sites=" << site << " PASS\n";
          completed = true;
          break;
        }
      }
      require (completed, "BBox failure sweep did not finish");
    }
  }
}

int main () try {
  insertion_boundaries<posets::downsets::vector_backed> ();
  insertion_boundaries<posets::downsets::vector_backed_one_dim_split_intersection_only> ();
  insertion_boundaries<posets::downsets::filtered_vector_backed> ();
  filtered_exception_cache ();
  diagnostics ();
  bbox_boundaries ();
  std::cout << "antichain rejection, move/throw boundaries and diagnostics PASS\n";
} catch (const std::exception& error) {
  allocations_left = -1;
  std::cerr << error.what () << '\n';
  return 1;
}
