#include "matrix-check.hh"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>

#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

namespace {
  using namespace matrix_check;
  using scalar_vector = posets::vectors::vector_backed<int8_t>;

  bool insert_oracle (region& values, logical candidate) {
    if (std::ranges::any_of (values, [&] (const auto& old) { return below (candidate, old); }))
      return false;
    std::erase_if (values, [&] (const auto& old) { return below (old, candidate); });
    values.push_back (std::move (candidate));
    return true;
  }
  template <class S>
  void observe (const S& actual, const region& wanted, const region& probes) {
    check (actual, wanted, probes);
    for (const auto& raw : probes) {
      const auto q = vector<typename S::value_type> (raw);
      const bool strict = std::ranges::any_of (
          wanted, [&] (const auto& old) { return raw != old and below (raw, old); });
      require (actual.dominates (q, true) == strict, "incorrect strict lazy dominance");
    }
  }
  template <class V, bool Cascade>
  void bulk_construction () {
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    std::mt19937 random (73);
    for (const size_t dim : {1U, 2U, 3U, 7U, 17U, 33U, 65U, 129U}) {
      posets::vectors::bool_threshold = posets::vectors::bitset_threshold = dim / 2;
      region rows;
      for (size_t i = 0; i < 80; ++i) {
        logical row (dim);
        for (size_t d = 0; d < dim; ++d)
          row[d] = d < dim / 2 ? static_cast<int> (random () % 7) - 3
                               : static_cast<int> (random () % 2) - 1;
        rows.push_back (std::move (row));
      }
      rows.push_back (rows[5]);
      logical bottom (dim, -1);
      std::fill_n (bottom.begin (), dim / 2, -4);
      rows.push_back (bottom);
      auto queries = rows;
      queries.emplace_back (dim, 0);
      for (int permutation = 0; permutation < 3; ++permutation) {
        if (permutation == 1)
          std::ranges::reverse (rows);
        else if (permutation == 2)
          std::shuffle (rows.begin (), rows.end (), random);
        for (const size_t limit : {size_t {0}, rows.size () - 1, rows.size (), rows.size () + 1}) {
          posets::downsets::lazy_kdtree_policy policy;
          policy.min_index_size = 3;
          policy.min_buffer = 4;
          policy.leaf_size = 2;
          policy.bulk_scan_limit = limit;
          std::vector<V> input;
          for (const auto& row : rows)
            input.push_back (vector<V> (row));
          S subject (std::move (input), policy);
          require (input.size () == rows.size (), "constructor stole the input container");
          observe (subject, rows, queries);
          if (limit >= rows.size ()) {
            const auto stats = subject.storage ();
            require (stats.created_ids == subject.size () and stats.retired_ids == 0 and
                         stats.compactions == 1,
                     "bulk constructor retained history or staged repeatedly");
            require (
                stats.rebuilds == static_cast<size_t> (subject.size () >= policy.min_index_size),
                "bulk constructor ignored the frozen-index size threshold");
          }
        }
      }
    }
  }

  template <class T, bool Cascade>
  void wide_bulk_construction () {
    using V = posets::vectors::vector_backed<T>;
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    using row_type = std::array<T, 2>;
    const T low = std::numeric_limits<T>::min ();
    const T high = std::numeric_limits<T>::max ();
    const auto near_low = static_cast<T> (low + 1);
    const auto near_high = static_cast<T> (high - 1);
    std::vector<row_type> rows {{low, high},      {high, low},           {0, 0},
                                {low, low},       {high, near_high},     {high, near_high},
                                {near_low, high}, {near_high, near_high}};
    const auto below = [] (const row_type& a, const row_type& b) {
      return a[0] <= b[0] and a[1] <= b[1];
    };
    std::set<row_type> wanted;
    for (const auto& row : rows)
      if (std::ranges::none_of (
              rows, [&] (const auto& other) { return row != other and below (row, other); }))
        wanted.insert (row);
    for (int permutation = 0; permutation < 2; ++permutation) {
      std::ranges::reverse (rows);
      for (const size_t limit : {size_t {0}, rows.size ()}) {
        posets::downsets::lazy_kdtree_policy policy;
        policy.min_index_size = policy.leaf_size = 1;
        policy.bulk_scan_limit = limit;
        std::vector<V> input;
        for (const auto& row : rows)
          input.emplace_back (std::span<const T> (row));
        S subject (std::move (input), policy);
        std::set<row_type> seen;
        for (const auto& value : subject)
          seen.insert ({value[0], value[1]});
        require (seen == wanted and subject.size () == wanted.size (),
                 "full-width signed bulk reduction is incorrect");
        auto queries = rows;
        queries.push_back ({high, high});
        for (const auto& query : queries) {
          const V value {std::span<const T> (query)};
          const bool ordinary =
              std::ranges::any_of (rows, [&] (const auto& old) { return below (query, old); });
          const bool strict = std::ranges::any_of (
              rows, [&] (const auto& old) { return query != old and below (query, old); });
          require (
              subject.dominates (value) == ordinary and subject.dominates (value, true) == strict,
              "full-width signed constructor membership is incorrect");
        }
      }
    }
  }
  template <bool Cascade>
  void preflight_construction () {
    using S = posets::downsets::lazy_kdtree_backed<scalar_vector, Cascade>;
    const std::array fixtures {
        region (65, logical {1, 1, -1}),
        region {{-1, -1, -1}, {0, 0, -1}, {1, 1, 0}, {2, 2, 0}, {2, 2, 0}},
        region {{2, 2, 0}, {1, 1, 0}, {0, 0, -1}, {-1, -1, -1}},
        region {{4, 4, 0}, {3, 0, -1}, {0, 3, 0}, {4, 4, 0}},
        region {{0, 0, -1}, {1, 1, -1}, {1, 1, -1}, {2, 2, 0}, {3, 0, 0}, {4, 4, 0}},
        region {{0, 0, -1}, {1, 1, -1}, {1, 1, -1}, {2, 2, 0}, {3, 0, 0}}};
    for (size_t which = 0; which < fixtures.size (); ++which)
      for (const size_t min_index : {size_t {1}, size_t {32}}) {
        const auto& rows = fixtures[which];
        posets::downsets::lazy_kdtree_policy policy;
        policy.min_index_size = min_index;
        policy.bulk_scan_limit = 100;
        std::vector<scalar_vector> input;
        for (const auto& row : rows)
          input.push_back (vector<scalar_vector> (row));
        S subject (std::move (input), policy);
        observe (subject, rows, rows);
        const size_t retained = maxima (rows).size ();
        require (subject.size () == retained and subject.storage ().created_ids == retained and
                     subject.storage ().compactions == 1 and
                     subject.storage ().rebuilds == (retained >= min_index ? 1U : 0U),
                 "preflight/fallback staged intermediate epochs");
#if POSETS_LAZY_KDTREE_STATS
        // Late incomparable pairs need four preflight tests, then either five
        // coverage tests with a final dominator, or six while retaining both maxima.
        const size_t comparisons = which < 4 ? rows.size () - 1 : (which == 4 ? 9 : 10);
        require (subject.storage ().bulk_comparisons == comparisons,
                 "preflight/fallback comparison count is incorrect");
#endif
      }
  }
  template <class T, bool Cascade>
  void wide_preflight () {
    using V = posets::vectors::vector_backed<T>;
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    const T low = std::numeric_limits<T>::min ();
    const T high = std::numeric_limits<T>::max ();
    const std::array<std::array<T, 2>, 4> rows {
        {{low, low}, {low, high}, {high, high}, {high, high}}};
    std::vector<V> input;
    for (const auto& row : rows)
      input.emplace_back (std::span<const T> (row));
    posets::downsets::lazy_kdtree_policy policy;
    policy.min_index_size = 1;
    S subject (std::move (input), policy);
    const V maximum {std::span<const T> (rows.back ())};
    const V lesser {std::span<const T> (rows[1])};
    require (subject.size () == 1 and (*subject.begin ())[0] == high and
                 (*subject.begin ())[1] == high and subject.contains (maximum) and
                 not subject.dominates (maximum, true) and subject.dominates (lesser, true),
             "full-width preflight dominance/strictness is incorrect");
#if POSETS_LAZY_KDTREE_STATS
    require (subject.storage ().bulk_comparisons == rows.size () - 1,
             "full-width preflight was not linear");
#endif
  }
  template <bool Cascade>
  void bulk_statistics () {
    using S = posets::downsets::lazy_kdtree_backed<scalar_vector, Cascade>;
    posets::downsets::lazy_kdtree_policy policy;
    policy.min_index_size = 2;
    policy.leaf_size = 1;
    policy.bulk_scan_limit = 100;
    std::vector<scalar_vector> input;
    for (const auto& row : region {{3, 0}, {2, 1}, {2, 1}, {1, 2}, {0, 3}, {0, 0}})
      input.push_back (vector<scalar_vector> (row));
    S subject (std::move (input), policy);
    require (subject.storage ().bulk_scan_limit == policy.bulk_scan_limit,
             "storage omitted the selected constructor cutoff");
#if POSETS_LAZY_KDTREE_STATS
    // One incomparable preflight test, then 0+1+2+2+3+1 sorted coverage tests.
    require (subject.storage ().bulk_comparisons == 10 and
                 subject.query_statistics ().comparisons == 0 and
                 subject.query_statistics ().pending_comparisons == 0,
             "bulk coverage work was omitted or conflated with indexed queries");
    require (subject.contains (vector<scalar_vector> ({2, 1})), "counter fixture membership");
    require (subject.storage ().bulk_comparisons == 10,
             "membership changed construction comparison accounting");
    S moved (std::move (subject));
    require (moved.storage ().bulk_comparisons == 10 and subject.storage ().bulk_comparisons == 0,
             "move lost or duplicated construction counters");
    moved.rebuild ();
    require (moved.storage ().bulk_comparisons == 10,
             "frozen-index replacement reset construction comparison accounting");
    moved.reset_query_statistics ();
    require (moved.storage ().bulk_comparisons == 0,
             "statistics reset omitted construction comparisons");
#endif
  }
  template <class V, bool Cascade>
  void random_algebra () {
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    static_assert (posets::Downset<S>);
    posets::downsets::lazy_kdtree_policy policy;
    policy.leaf_size = 3;
    policy.min_buffer = 4;
    policy.min_index_size = 3;
    std::mt19937 random (29);  // Deterministic scalar-oracle fixture.
    for (const size_t dim : {1U, 2U, 3U, 7U, 17U, 33U, 65U, 129U}) {
      posets::vectors::bool_threshold = posets::vectors::bitset_threshold = dim / 2;
      S subject (std::vector<V> {}, policy);
      region wanted;
      for (size_t step = 0; step < 100; ++step) {
        logical row (dim);
        for (size_t d = 0; d < dim; ++d)
          row[d] = static_cast<int> (random () % (d >= dim / 2 ? 2 : 7)) - 1;
        const bool accepted = insert_oracle (wanted, row);
        require (subject.insert (vector<V> (row)) == accepted, "insertion decision differs");
        region probes = wanted;
        probes.push_back (row);
        probes.emplace_back (dim, -1);
        logical high (dim, 0);
        std::fill_n (high.begin (), dim / 2, 7);
        probes.push_back (high);
        observe (subject, wanted, probes);
        if (step % 9 == 0) {
          subject.rebuild ();
          observe (subject, wanted, probes);
          const auto stats = subject.storage ();
          require (stats.created_ids == stats.live, "rebuild did not reset ID epoch");
          if constexpr (not Cascade)
            require (stats.frozen.bridge_bytes == 0, "off control allocated FC bridges");
        }
      }
      auto copied = subject.apply ([] (const V& v) { return v.copy (); });
      observe (copied, wanted, wanted);
      auto moved = std::move (subject);
      require (subject.empty () and not subject.contains (vector<V> (logical (dim, -1))),
               "moved source not empty");
      subject.insert (vector<V> (logical (dim, -1)));
      require (subject.size () == 1, "moved source cannot be reused");
      auto* self = &moved;
      moved = std::move (*self);
      observe (moved, wanted, wanted);
      moved.union_with (std::move (moved));
      moved.intersect_with (moved);
      observe (moved, wanted, wanted);
      auto united = wanted;
      insert_oracle (united, logical (dim, 0));
      copied.union_with (S (vector<V> (logical (dim, 0)), policy));
      observe (copied, united, united);
      region met;
      for (const auto& a : wanted) {
        logical row = a;
        for (auto& coordinate : row)
          coordinate = std::min (coordinate, 0);
        insert_oracle (met, std::move (row));
      }
      moved.intersect_with (S (vector<V> (logical (dim, 0)), policy));
      observe (moved, met, met);
      auto lowered = moved.apply ([] (const V& old) {
        auto raw = unpack (old);
        for (auto& x : raw)
          x = -1;
        return vector<V> (raw);
      });
      observe (lowered, {logical (dim, -1)}, {logical (dim, -1), logical (dim, 0)});
    }
  }

  template <bool Cascade>
  void stable_ids_and_tiny_history () {
    using S = posets::downsets::lazy_kdtree_backed<scalar_vector, Cascade>;
    posets::downsets::lazy_kdtree_policy policy;
    policy.leaf_size = 1;
    policy.min_index_size = 2;
    policy.min_buffer = 10000;
    S subject (std::vector<scalar_vector> {}, policy);
    region wanted;
    for (const auto& row : region {{4, 0}, {0, 4}, {3, 1}}) {
      subject.insert (vector<scalar_vector> (row));
      insert_oracle (wanted, row);
    }
    subject.rebuild ();
    for (const auto& row : region {{5, 1}, {1, 5}}) {
      subject.insert (vector<scalar_vector> (row));
      insert_oracle (wanted, row);
    }
    require (subject.storage ().created_ids == 5, "retired ID was reused within frozen epoch");
    observe (subject, wanted, {{5, 0}, {5, 1}, {1, 5}, {4, 4}, {2, 5}, {0, 4}});
    subject.insert (vector<scalar_vector> ({6, 6}));
    insert_oracle (wanted, {6, 6});
    require (subject.storage ().frozen.nodes > 0 and subject.storage ().pending_live == 1,
             "all-dead frozen subtree case was not exercised");
    observe (subject, wanted, {{6, 6}, {7, 0}, {5, 5}});
    subject.rebuild ();
    require (subject.storage ().created_ids == 1, "buffer compaction retained tombstone IDs");

    using wide_vector = posets::vectors::vector_backed<int16_t>;
    using W = posets::downsets::lazy_kdtree_backed<wide_vector, Cascade>;
    W chain (std::vector<wide_vector> {});
    for (int i = 0; i < 10000; ++i)
      chain.insert (vector<wide_vector> ({i, i}));
    require (chain.size () == 1 and chain.storage ().created_ids <= 64 and
                 chain.storage ().pending_slots <= 64,
             "tiny dominance chain accumulated unbounded history");
    require (chain.storage ().frozen.nodes == 0 and chain.storage ().rebuilds == 0,
             "tiny chain repeatedly built a KD tree");
    auto byte_policy = posets::downsets::lazy_kdtree_policy {};
    byte_policy.min_buffer = 1000000;
    byte_policy.min_index_size = 65;
    byte_policy.max_index_bytes = 512;
    W byte_limited (std::vector<wide_vector> {}, byte_policy);
    for (int i = 0; i < 1000; ++i)
      byte_limited.insert (vector<wide_vector> ({i, i}));
    require (byte_limited.storage ().created_ids < 64 and byte_limited.storage ().compactions > 0,
             "byte maintenance ignored pending-ID tombstones");
    W empty (std::vector<wide_vector> {});
    auto moved = std::move (chain);
    chain.insert (vector<wide_vector> ({-1, -1}));
    require (chain.contains (vector<wide_vector> ({-1, -1})), "move/reuse failed");
    moved.intersect_with (empty);
    require (moved.empty (), "empty intersection failed");
  }

  template <bool Cascade>
  void catalogs () {
    using T = posets::utils::bucket_kdtree<scalar_vector, Cascade>;
    for (const size_t leaf_size : {1U, 2U, 3U, 8U, 17U}) {
      std::vector<std::optional<scalar_vector>> points (129);
      std::vector<uint32_t> ids;
      std::vector<bool> alive (points.size ());
      region rows;
      for (uint32_t i = 1; i < points.size (); i += 3) {
        logical row {i % 4 == 0 ? 127 : (i % 4 == 1 ? -128 : 0), static_cast<int> (i % 11) - 1};
        points[i].emplace (vector<scalar_vector> (row));
        alive[i] = true;
        ids.push_back (i);
        rows.push_back (row);
      }
      const auto get = [&] (uint32_t id) -> const scalar_vector& {
        require (alive[id] and points[id].has_value (), "retired payload was accessed");
        return *points[id];
      };
      const auto live = [&] (uint32_t id) { return alive[id]; };
      T tree;
      tree.rebuild (ids, 2, leaf_size, get);
      for (int key = -128; key <= 127; ++key) {
        require (tree.check_bridges (static_cast<int8_t> (key), false), "lower bridge mismatch");
        require (tree.check_bridges (static_cast<int8_t> (key), true), "upper bridge mismatch");
        const logical query {key, 3};
        const auto value = vector<scalar_vector> (query);
        for (const bool strict : {false, true}) {
          const bool expected = std::ranges::any_of (rows, [&] (const auto& row) {
            return below (query, row) and (not strict or query != row);
          });
          require (tree.dominates (value, get, live, strict) == expected,
                   "bucket oracle mismatch");
        }
      }
      for (const auto id : ids) {
        alive[id] = false;
        points[id].reset ();
        tree.retire (id);
        tree.retire (id);
        require (tree.live_size () ==
                     static_cast<size_t> (std::count (alive.begin (), alive.end (), true)),
                 "live counts differ");
      }
      require (not tree.dominates (vector<scalar_vector> ({-128, -128}), get, live),
               "dead subtree answered");
    }
  }
  void search_attribution () {
    std::vector<scalar_vector> values;
    std::vector<uint32_t> ids (128);
    std::iota (ids.begin (), ids.end (), 0);
    for (size_t i = 0; i < ids.size (); ++i) {
      logical row (16, 0);
      row.back () = -1;
      values.push_back (vector<scalar_vector> (row));
    }
    const auto get = [&] (uint32_t id) -> const scalar_vector& { return values[id]; };
    const auto live = [] (uint32_t) { return true; };
    posets::utils::bucket_kdtree<scalar_vector, true> on;
    posets::utils::bucket_kdtree<scalar_vector, false> off;
    on.rebuild (ids, 16, 4, get);
    off.rebuild (ids, 16, 4, get);
    const auto query = vector<scalar_vector> (logical (16, 0));
    require (not on.dominates (query, get, live) and not off.dominates (query, get, live),
             "search fixture");
#if POSETS_LAZY_KDTREE_STATS
    require (on.query_statistics ().binary_searches == 1 and
                 off.query_statistics ().binary_searches == off.query_statistics ().leaf_visits and
                 off.query_statistics ().binary_searches > 1,
             "FC did not replace repeated leaf binary searches");
    require (on.query_statistics ().leaf_visits == off.query_statistics ().leaf_visits and
                 on.query_statistics ().comparisons == off.query_statistics ().comparisons,
             "FC attribution changed geometric work");
#endif
    require (off.storage ().bridge_bytes == 0 and off.storage ().keys == ids.size (),
             "off allocated catalogs");
  }
  void policy_and_contracts () {
    using S = posets::downsets::lazy_kdtree_backed<scalar_vector>;
    for (const bool buffer : {false, true})
      for (const double invalid :
           {-1.0, buffer ? 0.0 : 1.0, std::numeric_limits<double>::infinity (),
            -std::numeric_limits<double>::infinity (), std::numeric_limits<double>::quiet_NaN (),
            -std::numeric_limits<double>::quiet_NaN ()}) {
        auto policy = posets::downsets::lazy_kdtree_policy {};
        (buffer ? policy.buffer_fraction : policy.dead_fraction) = invalid;
        try {
          S bad (std::vector<scalar_vector> {}, policy);
          require (false, "bad policy accepted");
        } catch (const std::invalid_argument&) {
        }
      }
    S empty (std::vector<scalar_vector> {});
    require (not empty.contains (vector<scalar_vector> ({})), "empty query contract");
    try {
      empty.insert (vector<scalar_vector> ({}));
      require (false, "zero dimension accepted");
    } catch (const std::invalid_argument&) {
    }
    empty.insert (vector<scalar_vector> ({0, 0}));
    try {
      empty.insert (vector<scalar_vector> ({0}));
      require (false, "mixed dimension accepted");
    } catch (const std::invalid_argument&) {
    }
    observe (empty, {{0, 0}}, {{0, 0}, {1, 1}});
    empty.get_backing_vector ().clear ();
    empty.insert (vector<scalar_vector> ({1, 1, 1}));
    observe (empty, {{1, 1, 1}}, {{1, 1, 1}, {2, 2, 2}});
    for (const size_t limit : {size_t {0}, size_t {100}})
      for (const auto& rows : {region {{1, 1}, {0}}, region {{1, 1}, {}}, region {{}, {1, 1}}}) {
        posets::downsets::lazy_kdtree_policy policy;
        policy.bulk_scan_limit = limit;
        std::vector<scalar_vector> input;
        for (const auto& row : rows)
          input.push_back (vector<scalar_vector> (row));
        try {
          S invalid (std::move (input), policy);
          require (false, "constructor accepted inconsistent/zero dimensions");
        } catch (const std::invalid_argument&) {
        }
        require (input.size () == rows.size (),
                 "dimension validation consumed the input container");
        for (size_t i = 0; i < input.size (); ++i)
          require (unpack (input[i]) == rows[i], "dimension validation consumed an input");
      }
  }
}
int main () try {
  preflight_construction<true> ();
  preflight_construction<false> ();
  wide_preflight<int16_t, true> ();
  wide_preflight<int32_t, true> ();
  wide_preflight<int64_t, true> ();
  wide_preflight<int16_t, false> ();
  wide_preflight<int32_t, false> ();
  wide_preflight<int64_t, false> ();
  bulk_statistics<true> ();
  bulk_statistics<false> ();
  bulk_construction<scalar_vector, true> ();
  bulk_construction<scalar_vector, false> ();
  bulk_construction<posets::vectors::simd_vector_backed<int8_t>, true> ();
  bulk_construction<posets::vectors::simd_vector_backed<int8_t>, false> ();
  bulk_construction<posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<int8_t>, 3>,
                    true> ();
  wide_bulk_construction<int16_t, true> ();
  wide_bulk_construction<int32_t, true> ();
  wide_bulk_construction<int64_t, true> ();
  wide_bulk_construction<int16_t, false> ();
  wide_bulk_construction<int32_t, false> ();
  wide_bulk_construction<int64_t, false> ();
  random_algebra<scalar_vector, true> ();
  random_algebra<scalar_vector, false> ();
  random_algebra<posets::vectors::simd_vector_backed<int8_t>, true> ();
  random_algebra<posets::vectors::simd_vector_backed<int8_t>, false> ();
  random_algebra<posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<int8_t>, 3>,
                 true> ();
  stable_ids_and_tiny_history<true> ();
  stable_ids_and_tiny_history<false> ();
  catalogs<true> ();
  catalogs<false> ();
  search_attribution ();
  policy_and_contracts ();
  std::cout
      << "Lazy KD FC/independent searches, stable IDs, strict oracle, algebra and history PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
