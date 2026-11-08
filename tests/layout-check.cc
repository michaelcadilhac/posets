#include "matrix-check.hh"

#include <iostream>
#include <random>

#include <posets/utils/block_sharingtrie.hh>
#include <posets/utils/comptrie.hh>
#include <posets/utils/kdtree.hh>
#include <posets/utils/sharingtrie.hh>
#include <posets/vectors.hh>

namespace {
  using namespace matrix_check;
  using vector_type = posets::vectors::vector_backed<int8_t>;
  std::vector<vector_type> values (const region& rows) {
    std::vector<vector_type> result;
    for (const auto& row : rows)
      result.push_back (vector<vector_type> (row));
    return result;
  }
  template <class T>
  void build (T& trie, const region& rows) {
    if constexpr (requires { trie.relabel_trie (values (rows)); })
      trie.relabel_trie (values (rows));
    else if constexpr (requires { trie.relabel_tree (values (rows)); })
      trie.relabel_tree (values (rows));
    else
      trie.assign_and_build (values (rows));
  }
  template <class T>
  void oracle (const region& rows, const region& probes, size_t minimum_colors = 0) {
    T trie;
    build (trie, rows);
    for (const auto& raw : probes)
      for (bool strict : {false, true}) {
        const bool wanted = std::ranges::any_of (rows, [&] (const auto& row) {
          return below (raw, row) and (not strict or raw != row);
        });
        const bool found = trie.dominates (vector<vector_type> (raw), strict);
        if (found != wanted) {
          std::cerr << "type=" << typeid (T).name () << " dim=" << raw.size ()
                    << " strict=" << strict << " expected=" << wanted << " found=" << found
                    << " query=";
          for (auto x : raw)
            std::cerr << x << ',';
          std::cerr << '\n';
        }
        require (found == wanted, "layout/path scalar oracle mismatch");
      }
    const auto usage = trie.storage ();
    require (usage.node_capacity >= usage.reachable_nodes, "capacity below reachable count");
    require (usage.known_retained_bytes () >= usage.node_storage_bytes,
             "retained accounting underflow");
    if (minimum_colors != 0)
      require (usage.cache_bytes >= minimum_colors * 2 * sizeof (unsigned),
               "stamp capacity below distinct suffix language count");
#if POSETS_SHARINGTRIE_COMPACT_NODES
    if constexpr (std::is_same_v<T, posets::utils::sharingtrie<vector_type>>)
      require (usage.node_capacity == usage.reachable_nodes, "trie did not compact exactly");
#endif
  }
}
int main () try {
  // A repeated split from equality to greater must not credit an axis twice.
  oracle<posets::utils::kdtree<vector_type>> ({{0, 4}, {1, 3}, {2, 2}, {3, 1}, {4, 0}},
                                              {{2, 3}, {2, 2}, {1, 1}});
  // Identical labels at different depths are distinct query states.
  oracle<posets::utils::sharingtrie<vector_type>> ({logical (25, 0)}, {logical (25, 0)}, 25);
  // Equal suffixes share colors, but every outgoing branch participates in equality.
  oracle<posets::utils::sharingtrie<vector_type>> ({{3, 1, 2}, {2, 1, 2}}, {{1, 1, 2}, {3, 1, 2}},
                                                   4);
  oracle<posets::utils::sharingtrie<vector_type>> (
      {{3, 1, 2, 0}, {3, 1, 1, 2}, {2, 1, 2, 0}, {2, 1, 1, 3}},
      {{1, 1, 1, 3}, {2, 1, 1, 3}, {3, 1, 1, 3}}, 10);
  const posets::utils::comptrie<vector_type> default_comp;
  require (not default_comp.dominates (vector<vector_type> ({-1})), "default comptrie membership");
  // A reserved tree has allocation but no live nodes or labels.
  posets::utils::kdtree<vector_type> reserved (3, 32);
  auto usage = reserved.storage ();
  require (
      usage.node_capacity > 0 and usage.reachable_nodes == 0 and usage.logical_label_bytes == 0,
      "reserved kd-tree accounting underflow");
  region many (32, logical (3));
  for (size_t i = 0; i < many.size (); ++i)
    many[i] = {static_cast<int> (i), static_cast<int> (31 - i), -1};
  build (reserved, many);
  const auto capacity = reserved.storage ().node_capacity;
  build (reserved, {{-1, 0, 1}});
  usage = reserved.storage ();
  require (usage.node_capacity == capacity and usage.reachable_nodes == 1,
           "shrinking kd-tree lost allocated capacity");
  posets::utils::kdtree<vector_type> moved (std::move (reserved));
  // The moved-from capacity is an explicit utility contract.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  require (reserved.storage ().node_capacity == 0 and moved.storage ().node_capacity == capacity,
           "kd-tree move capacity accounting");
  auto* self = &moved;
  moved = std::move (*self);
  require (moved.storage ().node_capacity == capacity, "kd-tree self move capacity accounting");
  build (reserved, many);
  moved = std::move (reserved);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  require (reserved.storage ().node_capacity == 0 and moved.storage ().reachable_nodes == 63,
           "kd-tree move assignment accounting");
#if POSETS_KDTREE_NARROW_INDICES
  if constexpr (sizeof (size_t) > sizeof (uint32_t)) {
    struct oversized_range : std::vector<vector_type> {
        size_t size () const { return size_t {1} + std::numeric_limits<uint32_t>::max (); }
    } oversized;
    oversized.push_back (vector<vector_type> ({-1, 0, 1}));
    try {
      moved.relabel_tree (std::move (oversized));
      require (false, "narrow index overflow accepted");
    } catch (const std::length_error&) {
    }
    require (moved.storage ().reachable_nodes == 63, "overflow mutated live kd-tree");
    require (unpack (oversized.front ()) == logical ({-1, 0, 1}), "overflow moved source vector");
  }
#endif
  const posets::utils::sharingtrie<vector_type> unbuilt (3, 24);
  require (unbuilt.storage ().node_capacity == 24 and unbuilt.storage ().reachable_nodes == 0,
           "reserved sharingtrie accounting");
  const posets::utils::block_sharingtrie<vector_type, 8> tail (values ({{-1, -1, -1}}));
  require (tail.storage ().logical_label_bytes == 3 and
               tail.storage ().separate_label_storage_bytes >= 8,
           "active tail labels confused with padded capacity");
  std::mt19937 random (27);  // NOLINT(bugprone-random-generator-seed)
  for (const size_t dim : {1U, 4U, 15U, 16U, 17U, 31U, 32U, 33U, 65U, 129U}) {
    region rows (36, logical (dim));
    for (auto& row : rows)
      for (size_t d = 0; d < dim; ++d)
        row[d] = static_cast<int> (random () % (d % 3 == 0 ? 2 : 256)) - (d % 3 == 0 ? 1 : 128);
    // Long identical suffix paths plus a non-identity coordinate permutation.
    for (size_t i = 0; i < rows.size () / 2; ++i)
      for (size_t d = dim / 2; d < dim; ++d)
        rows[i][d] = -1;
    rows.push_back (rows.front ());
    region probes = rows;
    probes.emplace_back (dim, -128);
    probes.emplace_back (dim, 127);
    for (size_t i = 0; i < 100; ++i) {
      logical q (dim);
      for (auto& x : q)
        x = static_cast<int> (random () % 256) - 128;
      probes.push_back (std::move (q));
    }
    oracle<posets::utils::sharingtrie<vector_type>> (rows, probes);
    oracle<posets::utils::kdtree<vector_type>> (rows, probes);
    oracle<posets::utils::comptrie<vector_type>> (rows, probes);
    oracle<posets::utils::block_sharingtrie<vector_type, 8>> (rows, probes);
  }
  std::cout << "layout capacity/moves, full signed oracle and permuted paths PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
