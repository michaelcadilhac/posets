#include "allocation-tracking.hh"
#include "matrix-check.hh"

#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <string>

#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/utils/block_sharingtrie.hh>
#include <posets/utils/comptrie.hh>
#include <posets/utils/kdtree.hh>
#include <posets/utils/sharingtrie.hh>
#include <posets/vectors.hh>

namespace {
  using V = posets::vectors::vector_backed<int8_t>;
  using clock_type = std::chrono::steady_clock;
  using namespace matrix_check;
  region shape (const std::string& kind, size_t dim, size_t count, std::mt19937& random) {
    require (kind == "random" or kind == "prefix" or kind == "suffix", "unknown shape");
    const size_t begin = kind == "prefix" ? dim / 2 : 0;
    const size_t end = kind == "suffix" ? std::max (size_t {1}, dim / 2) : dim;
    std::vector<size_t> positions (end - begin);
    std::iota (positions.begin (), positions.end (), begin);
    region result (count, logical (dim, -1));
    for (auto& row : result) {
      std::shuffle (positions.begin (), positions.end (), random);
      // Equal Hamming weight creates an antichain (apart from duplicates).
      for (size_t i = 0; i < positions.size () / 2; ++i)
        row[positions[i]] = 0;
    }
    return result;
  }
  std::vector<V> materialize (const region& rows) {
    std::vector<V> result;
    result.reserve (rows.size ());
    for (const auto& row : rows)
      result.push_back (vector<V> (row));
    return result;
  }
  template <class T>
  void run (const std::string& name, const std::string& kind, size_t dim, size_t count,
            size_t query_count, size_t rounds, bool verify) {
    std::mt19937 random (41);
    const region rows = shape (kind, dim, count, random);
    region probes = shape ("random", dim, query_count, random);
    for (size_t i = 0; i < probes.size (); ++i)
      if (i % 2 == 0)
        probes[i] = rows[i % rows.size ()];
      else if (i % 4 == 3)
        probes[i][random () % dim] = 1;  // Guaranteed miss, including shared shapes.
    auto input = materialize (rows);
    auto queries = materialize (probes);
    const size_t heap_before = allocation_tracking::live;
    allocation_tracking::reset_peak ();
    const auto start = clock_type::now ();
    T trie = [&] {
      if constexpr (std::is_default_constructible_v<T>) {
        T result;
        if constexpr (requires { result.relabel_trie (std::move (input)); })
          result.relabel_trie (std::move (input));
        else if constexpr (requires { result.relabel_tree (std::move (input)); })
          result.relabel_tree (std::move (input));
        else
          result.assign_and_build (std::move (input));
        return result;
      }
      else
        return T (std::move (input));
    }();
    const auto built = clock_type::now ();
    const auto build_peak = allocation_tracking::peak;
    const auto build_allocations = allocation_tracking::count;
    // Normalize caller ownership after measuring the actual construction peak.
    std::vector<V> {}.swap (input);
    const auto heap_after = allocation_tracking::live;
    allocation_tracking::reset_peak ();
    size_t hits = 0;
    const auto query_start = clock_type::now ();
    for (size_t r = 0; r < rounds; ++r)
      for (const auto& query : queries)
        hits += trie.dominates (query);
    const auto queried = clock_type::now ();
    const auto query_allocations = allocation_tracking::count;
    if (verify)
      for (size_t i = 0; i < probes.size (); ++i)
        for (bool strict : {false, true}) {
          const bool expected = std::ranges::any_of (rows, [&] (const auto& row) {
            return below (probes[i], row) and (not strict or probes[i] != row);
          });
          require (trie.dominates (queries[i], strict) == expected, "layout oracle mismatch");
        }
#if POSETS_TRACK_ALLOCATIONS
    // Report replacement coexistence separately from first construction.
    auto replacement = materialize (rows);
    const auto rebuild_before = allocation_tracking::live;
    allocation_tracking::reset_peak ();
    if constexpr (requires { trie.relabel_trie (std::move (replacement)); })
      trie.relabel_trie (std::move (replacement));
    else if constexpr (requires { trie.relabel_tree (std::move (replacement)); })
      trie.relabel_tree (std::move (replacement));
    else if constexpr (requires { trie.assign_and_build (std::move (replacement)); })
      trie.assign_and_build (std::move (replacement));
    else
      trie = T (std::move (replacement));
    const auto rebuild_peak = allocation_tracking::peak;
    const auto rebuild_allocations = allocation_tracking::count;
    std::vector<V> {}.swap (replacement);
    const auto rebuild_after = allocation_tracking::live;
#endif
    std::cout
        << "backend=" << name << " shape=" << kind << " dimension=" << dim
        << " fixture=binary-antichain probes=half-generators_quarter-equalweight_quarter-high"
        << " inputs=" << count << " queries=" << query_count << " rounds=" << rounds
        << " selected_lanes=" << posets::utils::simd_traits<int8_t>::simd_size
#ifdef NDEBUG
        << " ndebug=1"
#else
        << " ndebug=0"
#endif
        << " trie_compact=" << POSETS_SHARINGTRIE_COMPACT_NODES
        << " kd_narrow=" << POSETS_KDTREE_NARROW_INDICES
        << " comptrie_simd=" << POSETS_COMPTRIE_SIMD_PATHS;
    if constexpr (requires { T::block_size; })
      std::cout << " B=" << T::block_size
                << " comparison=" << (T::uses_simd ? "simd" : "coordinate-loop")
                << " memo=" << T::uses_cache;
    if constexpr (requires { T::uses_cascade; })
      std::cout << " cascade=" << T::uses_cascade << " filter_coordinate=0"
                << " leaf_size=" << trie.rebuild_policy ().leaf_size;
    if constexpr (requires { trie.root_branches (); })
      std::cout << " root_children=" << trie.root_branches ();
    std::cout << ' ' << trie.storage () << " estimated_vector_payload_bytes="
              << trie.size () * V::blocks_for (dim) * sizeof (V::block_type)
              << " retained_generators=" << trie.size ()
              << " heap_tracking=" << POSETS_TRACK_ALLOCATIONS;
#if POSETS_TRACK_ALLOCATIONS
    require (query_allocations == 0, "post-build queries allocated");
    std::cout << " heap_before=" << heap_before << " heap_after=" << heap_after
              << " build_heap_peak=" << build_peak
              << " build_extra_peak=" << build_peak - heap_before
              << " build_allocations=" << build_allocations
              << " query_allocations=" << query_allocations
              << " rebuild_heap_before=" << rebuild_before << " rebuild_heap_peak=" << rebuild_peak
              << " rebuild_extra_peak=" << rebuild_peak - rebuild_before
              << " rebuild_heap_after=" << rebuild_after
              << " rebuild_allocations=" << rebuild_allocations;
#else
    (void) heap_before;
    (void) heap_after;
    (void) build_peak;
    (void) build_allocations;
    (void) query_allocations;
#endif
    std::cout
        << " build_ns="
        << std::chrono::duration_cast<std::chrono::nanoseconds> (built - start).count ()
        << " query_ns="
        << std::chrono::duration_cast<std::chrono::nanoseconds> (queried - query_start).count ()
        << " hits=" << hits << " checked=" << verify << '\n';
  }
}
int main (int argc, char** argv) try {
  if (argc != 8) {
    std::cerr << "usage: layout-bm BACKEND random|prefix|suffix DIM N QUERIES ROUNDS CHECK(0|1)\n"
              << "backends: kd trie comp b1scalar b4scalar b4simd b8scalar b8simd b16scalar "
                 "b16simd b8nocache lazy-fc lazy-search\n";
    return 1;
  }
  const std::string backend = argv[1], kind = argv[2];
  const auto dim = std::stoul (argv[3]), count = std::stoul (argv[4]),
             queries = std::stoul (argv[5]), rounds = std::stoul (argv[6]);
  const bool check = std::stoi (argv[7]) != 0;
  matrix_check::require (dim > 0 and count > 0 and queries > 0 and rounds > 0,
                         "positive sizes required");
#define RUN(Name, ...)                                                    \
  if (backend == Name) {                                                  \
    run<__VA_ARGS__> (backend, kind, dim, count, queries, rounds, check); \
    return 0;                                                             \
  }
  RUN ("lazy-fc", posets::downsets::lazy_kdtree_backed<V, true>)
  RUN ("lazy-search", posets::downsets::lazy_kdtree_backed<V, false>)
  RUN ("kd", posets::utils::kdtree<V>)
  RUN ("trie", posets::utils::sharingtrie<V>)
  RUN ("comp", posets::utils::comptrie<V>)
  RUN ("b1scalar", posets::utils::block_sharingtrie<V, 1, false>)
  RUN ("b4scalar", posets::utils::block_sharingtrie<V, 4, false>)
  RUN ("b4simd", posets::utils::block_sharingtrie<V, 4>)
  RUN ("b8scalar", posets::utils::block_sharingtrie<V, 8, false>)
  RUN ("b8simd", posets::utils::block_sharingtrie<V, 8>)
  RUN ("b16scalar", posets::utils::block_sharingtrie<V, 16, false>)
  RUN ("b16simd", posets::utils::block_sharingtrie<V, 16>)
  RUN ("b8nocache", posets::utils::block_sharingtrie<V, 8, true, false>)
#undef RUN
  throw std::runtime_error ("unknown backend");
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
