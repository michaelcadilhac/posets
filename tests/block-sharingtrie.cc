#include "matrix-check.hh"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>

#include <posets/downsets/block_sharingtrie_backed.hh>
#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

namespace {
  using namespace matrix_check;
  template <class V>
  std::vector<V> values (const region& rows) {
    std::vector<V> result;
    for (const auto& row : rows)
      result.push_back (vector<V> (row));
    return result;
  }
  template <class T>
  void queries (const T& trie, const region& rows, const region& probes) {
    using V = std::ranges::range_value_t<T>;
    for (const auto& raw : probes) {
      const auto q = vector<V> (raw);
      for (bool strict : {false, true}) {
        const bool expected = std::ranges::any_of (rows, [&] (const logical& row) {
          return below (raw, row) and (not strict or raw != row);
        });
        require (trie.dominates (q, strict) == expected,
                 "blocked dominance differs from scalar oracle");
      }
    }
  }
  template <class V, size_t B, bool Simd, bool Cache = true>
  void exercise () {
    using T = posets::utils::block_sharingtrie<V, B, Simd, Cache>;
    using S = posets::downsets::block_sharingtrie_backed<V, B, Simd, Cache>;
    static_assert (posets::Downset<S>);
    constexpr size_t width = posets::utils::simd_traits<typename V::value_type>::simd_size;
    std::mt19937 random (19);
    const std::set<size_t> dimensions {
        1,  std::max (size_t {1}, B - 1), B, B + 1, 2 * B + 1, width - 1, width, width + 1, 65,
        129};
    for (const size_t dim : dimensions) {
      posets::vectors::bool_threshold = posets::vectors::bitset_threshold = dim / 2;
      region rows (24, logical (dim));
      for (auto& row : rows)
        for (size_t d = 0; d < dim; ++d)
          row[d] = int (random () % (d >= dim / 2 ? 2 : 6)) - 1;
      rows.push_back (rows.front ());
      rows.emplace_back (dim, -1);
      region probes = rows;
      for (size_t n = 0; n < 180; ++n) {
        logical row (dim);
        for (size_t d = 0; d < dim; ++d)
          row[d] = int (random () % (d >= dim / 2 ? 2 : 7)) - 1;
        probes.push_back (std::move (row));
      }
      T trie (values<V> (rows));
      queries (trie, rows, probes);
      const auto equal = vector<V> (logical (dim, -1));
      T tail (values<V> ({logical (dim, -1)}));
      require (tail.dominates (equal) and not tail.dominates (equal, true),
               "tail created strictness");
      if constexpr (Cache)
        require (
            tail.storage ().cache_bytes >= 2 * ((dim / B) + (dim % B != 0)) * sizeof (unsigned),
            "stamp capacity below number of block depths");
      else
        require (tail.storage ().cache_bytes == 0, "uncached trie retained color stamps");
      T moved (std::move (trie));
      require (trie.empty () and not trie.dominates (equal), "moved source not empty");
      auto* self = &moved;
      moved = std::move (*self);
      queries (moved, rows, probes);
      trie = std::move (moved);
      require (moved.empty (), "move-assigned source not empty");
      trie.relabel_trie (std::move (trie.get_backing_vector ()));
      queries (trie, rows, probes);
      trie.relabel_trie (values<V> ({logical (dim, -1)}));
      queries (trie, {logical (dim, -1)}, probes);
      trie.relabel_trie (values<V> (rows));
      queries (trie, rows, probes);
      const auto stats = trie.storage ();
      require (stats.reachable_nodes <= stats.node_capacity, "node accounting");
      require (stats.cache_bytes == 0 or stats.cache_bytes % (2 * sizeof (unsigned)) == 0,
               "stamp accounting");
      auto set = build<S> (rows);
      check (set, rows, probes);
      auto copy = set.apply ([] (const V& value) { return value.copy (); });
      set.union_with (build<S> ({logical (dim, -1)}));
      check (set, rows, probes);
      set.intersect_with (copy);
      check (set, rows, probes);
      auto* self_set = &set;
      set = std::move (*self_set);
      check (set, rows, probes);
    }
    // A later lexicographic sibling can dominate after an earlier one fails.
    posets::vectors::bool_threshold = posets::vectors::bitset_threshold = 1000;
    T later (values<V> ({{5, 0}, {4, 9}}));
    queries (later, {{5, 0}, {4, 9}}, {{4, 8}, {4, 9}, {5, 1}});
    // Equal incoming blocks and equal first children, but different complete
    // outgoing languages. Memoization must not conflate these two suffixes.
    region suffixes;
    for (int prefix : {4, 3})
      for (int choice : {0, 1}) {
        logical row (2 * B + 2, 1);
        row[0] = prefix;
        row[2 * B] = choice == 0 ? 2 : 1;
        row[2 * B + 1] = choice == 0 ? 0 : (prefix == 4 ? 2 : 3);
        suffixes.push_back (std::move (row));
      }
    logical suffix_query (2 * B + 2, 1);
    suffix_query[0] = 2;
    suffix_query[2 * B] = 1;
    suffix_query[2 * B + 1] = 3;
    T suffix (values<V> (suffixes));
    queries (suffix, suffixes, {suffix_query});
    // Repeated equivalent suffix languages exercise cache hits and strict state.
    suffixes[2] = suffixes[0];
    suffixes[2][0] = 3;
    suffixes[3] = suffixes[1];
    suffixes[3][0] = 3;
    suffix.relabel_trie (values<V> (suffixes));
    queries (suffix, suffixes, suffixes);
    matrix_check::run<S> ("empty");
    matrix_check::run<S> ("nonempty");
    T empty;
    require (empty.get_all ().empty () and not empty.dominates (vector<V> ({})), "empty contract");
    try {
      empty.relabel_trie (values<V> ({{}}));
      require (false, "zero-dimensional input was accepted");
    } catch (const std::invalid_argument&) {
    }
    require (empty.empty (), "failed zero-dimensional rebuild mutated trie");
    try {
      suffix.relabel_trie (values<V> ({{0}, {0, 1}}));
      require (false, "mixed dimensions accepted");
    } catch (const std::invalid_argument&) {
    }
    queries (suffix, suffixes, suffixes);
  }
  template <class V>
  void blocks () {
    exercise<V, 1, false> ();
    exercise<V, 4, false> ();
    exercise<V, 4, true> ();
    exercise<V, 8, false> ();
    exercise<V, 8, true> ();
    exercise<V, 16, false> ();
    exercise<V, 16, true> ();
    exercise<V, 8, true, false> ();
  }
}
int main () try {
  blocks<posets::vectors::vector_backed<int8_t>> ();
  blocks<posets::vectors::simd_vector_backed<int8_t>> ();
  exercise<posets::vectors::simd_array_backed<int8_t, 129>, 8, true> ();
  exercise<posets::vectors::simd_array_ptr_backed<int8_t, 129>, 16, true> ();
  exercise<posets::vectors::vector_backed<int16_t>, 8, true> ();
  exercise<posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<int8_t>, 3>, 8,
           true> ();
  std::cout << "block sharingtrie scalar oracle, algebra, moves, tails, suffixes PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
