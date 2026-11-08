#include "allocation-failure.hh"
#include "matrix-check.hh"
#include <type_traits>

#include <array>
#include <bit>
#include <iostream>
#include <limits>

#include <posets/downsets/simple_sharingtree_backed.hh>
#include <posets/utils/kdtree.hh>
#include <posets/utils/sharingforest.hh>
#include <posets/vectors.hh>

namespace {
  using matrix_check::logical;
  using matrix_check::region;
  using matrix_check::require;
  using V = posets::vectors::vector_backed<int16_t>;
  using forest_type = posets::utils::sharingforest<V>;
  using simple_tree = posets::downsets::simple_sharingtree_backed<V>;
  static_assert (not std::is_nothrow_constructible_v<simple_tree, std::vector<V>&&>);

  std::vector<V> values (const region& rows) {
    std::vector<V> result;
    result.reserve (rows.size ());
    for (const auto& row : rows)
      result.push_back (matrix_check::vector<V> (row));
    return result;
  }

  bool covered (const region& rows, const logical& query, bool strict) {
    return std::ranges::any_of (rows, [&] (const auto& row) {
      return matrix_check::below (query, row) and (not strict or query != row);
    });
  }

  void check_forest (forest_type& forest, size_t root, const region& rows, const region& queries) {
    const auto materialized = forest.get_all (root);
    std::set<logical> actual;
    for (const auto& value : materialized)
      require (actual.insert (matrix_check::unpack (value)).second, "duplicate forest path");
    require (actual == std::set<logical> (rows.begin (), rows.end ()), "forest paths changed");
    require (forest.count_vectors (root) == materialized.size (), "forest path count changed");

    auto probes = values (queries);
    std::vector<std::array<bool, 2>> expected;
    for (const auto& query : queries)
      expected.push_back ({covered (rows, query, false), covered (rows, query, true)});
    // Warm both the stack and each visited layer before checking query allocation reuse.
    for (int repeat = 0; repeat < 2; ++repeat)
      for (size_t i = 0; i < probes.size (); ++i)
        for (bool strict : {false, true})
          require (forest.covers_vector (root, probes[i], strict) == expected[i][strict],
                   "forest query differs from scalar oracle");
    {
      struct reset_budget {
          ~reset_budget () { allocations_left = -1; }
      } guard;
      allocations_left = 0;
      for (int repeat = 0; repeat < 8; ++repeat)
        for (size_t i = 0; i < probes.size (); ++i)
          for (bool strict : {false, true})
            require (forest.covers_vector (root, probes[i], strict) == expected[i][strict],
                     "forest repeated query changed");
    }
  }

  void wide_forest () {
    region rows;
    // The first reservation exceeds twice the default child-buffer capacity.
    for (int i = 0; i < 2401; ++i)
      rows.push_back ({i, 2400 - i});
    forest_type forest (2);
    const auto root = forest.add_vectors (values (rows), false);
    check_forest (forest, root, rows,
                  {{0, 2400}, {1200, 1200}, {2400, 0}, {2401, 0}, {1200, 1201}, {-1, -1}});
    require (forest.check_child_order (), "wide forest child order changed");
  }

  void shared_suffixes () {
    const region rows {{1, 1, 2, 2}, {3, 1, 2, 2}, {2, 3, 1, 2}, {3, 0, 1, 2}, {0, 3, 1, 2}};
    region queries;
    for (int a = 0; a <= 3; ++a)
      for (int b = 0; b <= 3; ++b)
        for (int c = 1; c <= 3; ++c)
          queries.push_back ({a, b, c, 2});
    forest_type forest (4);
    const auto root = forest.add_vectors (values (rows), false);
    // The equal low prefix visits a suffix owing strictness before a larger prefix does.
    check_forest (forest, root, rows, queries);
    const region later {{4, 4, 4, 4}, {5, 3, 4, 4}};
    const auto later_root = forest.add_vectors (values (later), false);
    check_forest (forest, later_root, later, queries);
    check_forest (forest, root, rows, queries);
    forest.clear ();
    const region rebuilt {{0, 0, 0, 0}};
    check_forest (forest, forest.add_vectors (values (rebuilt), false), rebuilt, queries);
  }

  void union_lifecycle () {
    region left;
    region right;
    for (int i = 0; i < 16; ++i) {
      left.push_back ({4, i, 32 - i, 0, 2, 1});
      right.push_back ({4, i, 31 - i, 1, 1, 2});
    }
    auto combined = left;
    combined.insert (combined.end (), right.begin (), right.end ());
    region queries = combined;
    queries.push_back ({4, 7, 25, 1, 2, 2});
    queries.push_back ({0, 0, 0, 0, 0, 0});
    forest_type forest (6);
    const auto left_root = forest.add_vectors (values (left), false);
    const auto right_root = forest.add_vectors (values (right), false);
    auto united = forest.st_union (left_root, right_root);
    check_forest (forest, united, combined, queries);
    for (int repeat = 0; repeat < 8; ++repeat)
      united = forest.st_union (united, repeat % 2 ? left_root : right_root);
    check_forest (forest, united, combined, queries);
    check_forest (forest, left_root, left, queries);
    check_forest (forest, right_root, right, queries);
    require (forest.check_child_order (), "union child order changed");
  }

  region kd_rows (size_t count) {
    region rows;
    for (size_t i = 0; i < count; ++i)
      rows.push_back ({static_cast<int> (i), static_cast<int> (count - i)});
    return rows;
  }

  void check_kd (const posets::utils::kdtree<V>& tree, const region& rows) {
    region queries = rows;
    queries.push_back ({-1, -1});
    queries.push_back ({1000, 1000});
    for (const auto& query : queries)
      for (bool strict : {false, true})
        require (tree.dominates (matrix_check::vector<V> (query), strict) ==
                     covered (rows, query, strict),
                 "KD query differs from scalar oracle");
  }

  void kd_capacity () {
    for (size_t count : {1U, 2U, 3U, 4U, 5U, 7U, 8U, 9U, 15U, 16U, 17U, 31U, 32U, 33U}) {
      const auto rows = kd_rows (count);
      posets::utils::kdtree<V> tree (values (rows));
      const auto usage = tree.storage ();
      require (usage.node_capacity == 2 * std::bit_ceil (count) - 1, "KD heap capacity not tight");
      require (usage.reachable_nodes == 2 * count - 1, "KD live node accounting changed");
      check_kd (tree, rows);
    }
    posets::utils::kdtree<V> reused (values (kd_rows (128)));
    const auto capacity = reused.storage ().node_capacity;
    for (size_t count : {1U, 64U, 2U, 127U}) {
      const auto rows = kd_rows (count);
      reused.relabel_tree (values (rows));
      require (reused.storage ().node_capacity == capacity, "KD high-water allocation lost");
      check_kd (reused, rows);
    }
    bool rejected = false;
    try {
      posets::utils::kdtree<V> oversized (2, std::numeric_limits<size_t>::max ());
    } catch (const std::length_error&) {
      rejected = true;
    }
    require (rejected, "KD unrepresentable heap capacity accepted");

    // Rebuilds do not promise rollback, but each allocation failure must permit destruction.
    for (long fail = 0; fail < 3; ++fail) {
      posets::utils::kdtree<V> tree (values (kd_rows (2)));
      auto replacement = values (kd_rows (16));
      bool threw = false;
      try {
        allocations_left = fail;
        tree.relabel_tree (std::move (replacement));
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      require (threw, "KD rebuild allocation failure not injected");
    }
  }

  void simple_tree_failure () {
    const region rows {{3, 1, 2}};
    const simple_tree keeper (values (rows));
    auto input = values (rows);
    bool propagated = false;
    try {
      // A live same-dimension forest makes the first allocation occur inside reset_tree.
      allocations_left = 0;
      simple_tree failed (std::move (input));
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    allocations_left = -1;
    require (propagated, "simple sharing tree allocation failure did not propagate");
    // This early failure precedes forest insertion; no general rebuild rollback is promised.
    const auto query = matrix_check::vector<V> (rows.front ());
    require (keeper.size () == 1 and keeper.contains (query), "live shared tree changed");
    const simple_tree retry (values (rows));
    require (retry.size () == 1 and retry.contains (query), "simple tree retry failed");
  }
}

int main () try {
  wide_forest ();
  shared_suffixes ();
  union_lifecycle ();
  kd_capacity ();
  simple_tree_failure ();
  std::cout
      << "forest cache, spans, union, KD capacity and simple tree failure propagation PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
