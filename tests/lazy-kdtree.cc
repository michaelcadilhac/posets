#include "matrix-check.hh"

#include <cstdint>
#include <iostream>
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
  }
}
int main () try {
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
