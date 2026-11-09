#include "allocation-failure.hh"
#include <type_traits>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/utils/rank_stats.hh>
#include <posets/vectors.hh>

namespace {
  void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }
  template <class T>
  using row = std::vector<T>;
  template <class T>
  using region = std::vector<row<T>>;
  template <class V>
  using downset = posets::downsets::rank_bucketed_vector_backed<V>;
  template <class T>
  bool below (const row<T>& a, const row<T>& b) {
    require (a.size () == b.size (), "oracle dimension mismatch");
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }
  template <class T>
  std::set<row<T>> maxima (const region<T>& values) {
    std::set<row<T>> result;
    for (const auto& a : values)
      if (std::ranges::none_of (values, [&] (const auto& b) { return a != b and below (a, b); }))
        result.insert (a);
    return result;
  }
  template <class V>
  V vector (const row<typename V::value_type>& values) {
    return V (std::span<const typename V::value_type> (values));
  }
  template <class V>
  row<typename V::value_type> unpack (const V& value) {
    row<typename V::value_type> result;
    for (size_t i = 0; i < value.size (); ++i)
      result.push_back (value[i]);
    return result;
  }
  template <class V>
  downset<V> build (const region<typename V::value_type>& values) {
    std::vector<V> input;
    for (const auto& value : values)
      input.push_back (vector<V> (value));
    return downset<V> (std::move (input));
  }
  template <class V>
  void check (const downset<V>& actual, const region<typename V::value_type>& expected,
              const region<typename V::value_type>& probes) {
    std::set<row<typename V::value_type>> seen;
    for (const auto& value : actual)
      require (seen.insert (unpack (value)).second, "duplicate ranked generator");
    require (seen == maxima (expected) and seen.size () == actual.size (), "ranked maxima");
    for (const auto& probe : probes) {
      const bool wanted =
          std::ranges::any_of (expected, [&] (const auto& old) { return below (probe, old); });
      require (actual.contains (vector<V> (probe)) == wanted, "ranked membership");
    }
  }
  template <class T>
  region<T> meets (const region<T>& a, const region<T>& b) {
    region<T> result;
    for (const auto& x : a)
      for (const auto& y : b) {
        row<T> value (x.size ());
        for (size_t i = 0; i < value.size (); ++i)
          value[i] = std::min (x[i], y[i]);
        result.push_back (std::move (value));
      }
    return result;
  }

  template <class V>
  void differential () {
    using coordinate_type = V::value_type;
    // Reproducible differential fixture.
    std::mt19937 random (260619);  // NOLINT(bugprone-random-generator-seed)
    for (const size_t dim : {0U, 1U, 2U, 15U, 16U, 17U, 31U, 32U, 33U, 63U, 64U, 65U}) {
      region<coordinate_type> values;
      for (int n = 0; n < 20; ++n) {
        row<coordinate_type> value (dim);
        for (auto& coordinate : value)
          coordinate = static_cast<coordinate_type> (static_cast<int> (random () % 7) - 3);
        values.push_back (std::move (value));
      }
      values.push_back (values.front ());
      row<coordinate_type> extremes (dim);
      row<coordinate_type> opposite (dim);
      for (size_t i = 0; i < dim; ++i) {
        extremes[i] = i % 2 == 0 ? std::numeric_limits<coordinate_type>::min ()
                                 : std::numeric_limits<coordinate_type>::max ();
        opposite[i] = i % 2 == 0 ? std::numeric_limits<coordinate_type>::max ()
                                 : std::numeric_limits<coordinate_type>::min ();
      }
      values.push_back (std::move (extremes));
      values.push_back (std::move (opposite));
      auto subject = build<V> ({});
      region<coordinate_type> prefix;
      for (const auto& candidate : values) {
        const bool accepted = std::ranges::none_of (
            prefix, [&] (const auto& old) { return below (candidate, old); });
        require (subject.insert (vector<V> (candidate)) == accepted, "ranked insert result");
        prefix.push_back (candidate);
        check (subject, prefix, {candidate, row<coordinate_type> (dim, coordinate_type {-1})});
      }
      const region<coordinate_type> a (values.begin (), values.begin () + 5);
      const region<coordinate_type> b (values.begin () + 5, values.begin () + 10);
      auto joined = build<V> (a);
      joined.union_with (build<V> (b));
      auto union_rows = a;
      union_rows.insert (union_rows.end (), b.begin (), b.end ());
      check (joined, union_rows, values);
      auto intersected = build<V> (a);
      intersected.intersect_with (build<V> (b));
      check (intersected, meets (a, b), values);
      region<coordinate_type> mapped;
      const auto transform = [&] (const V& value) {
        auto raw = unpack (value);
        for (size_t i = 0; i < raw.size (); ++i)
          raw[i] = i % 2 == 0 ? static_cast<coordinate_type> (-raw[i])
                              : static_cast<coordinate_type> (raw[i] - 1);
        return vector<V> (raw);
      };
      for (const auto& value : maxima (a))
        mapped.push_back (unpack (transform (vector<V> (value))));
      check (build<V> (a).apply (transform), mapped, values);
    }
  }

  void floating_and_wide () {
    using floating_vector = posets::vectors::vector_backed<double>;
    auto floating = build<floating_vector> ({{0.1, 0.0}});
    require (floating.insert (vector<floating_vector> ({0.2, 0.0})),
             "same-truncated-rank insertion rejected");
    check (floating, {{0.2, 0.0}}, {{0.1, 0.0}, {0.2, 0.0}, {0.3, 0.0}});
    auto floating_tie = build<floating_vector> ({{0.1, 0.0}, {0.0, 0.2}});
    require (floating_tie.insert (vector<floating_vector> ({0.2, 0.0})),
             "floating tie victim rejected");
    check (floating_tie, {{0.2, 0.0}, {0.0, 0.2}}, {{0.1, 0.0}, {0.2, 0.0}, {0.0, 0.2}});
    region<double> floating_order;
    for (const auto& value : floating_tie)
      floating_order.push_back (unpack (value));
    require (floating_order == region<double> ({{0.2, 0.0}, {0.0, 0.2}}),
             "new floating tie must precede the surviving equal-rank generator");
    const region<double> a {{0.1, 0.5}, {0.4, 0.2}, {-0.2, 0.7}};
    const region<double> b {{0.2, 0.4}, {0.5, 0.1}, {-0.1, 0.6}};
    auto combined = build<floating_vector> (a);
    combined.union_with (build<floating_vector> (b));
    auto wanted = a;
    wanted.insert (wanted.end (), b.begin (), b.end ());
    check (combined, wanted, wanted);
    auto intersection = build<floating_vector> (a);
    intersection.intersect_with (build<floating_vector> (b));
    check (intersection, meets (a, b), wanted);

    using wide_vector = posets::vectors::vector_backed<std::int64_t>;
    const auto lo = std::numeric_limits<std::int64_t>::min ();
    const auto hi = std::numeric_limits<std::int64_t>::max ();
    const region<std::int64_t> wide {{lo, hi}, {hi, lo}, {0, 0}, {1, -1}};
    check (build<wide_vector> (wide), wide, wide);
#ifdef POSETS_RANK_STATS
    auto fallback = build<wide_vector> ({{2, 0}, {0, 2}});
    posets::utils::rank_stats_reset ();
    require (fallback.insert (vector<wide_vector> ({1, 1})), "wide equal-rank insertion");
    require (posets::utils::rank_stats_current ().partial_order_calls == 4,
             "wide two-dimensional input skipped inclusive fallback");
#endif
  }

  struct vector_error {};
  template <bool ThrowMoves>
  class watched : public posets::vectors::vector_backed<std::int8_t> {
      using base = posets::vectors::vector_backed<std::int8_t>;
      explicit watched (base&& value) : base (std::move (value)) {}
      static void charge (long& remaining) {
        if (remaining == 0)
          throw vector_error {};
        if (remaining > 0)
          --remaining;
      }

    public:
      using base::base;
      static inline long copies_left = -1, orders_left = -1, moves_left = -1, reads_left = -1;
      static inline size_t coordinate_reads = 0;
      static void reset () {
        copies_left = orders_left = moves_left = reads_left = -1;
        coordinate_reads = 0;
      }
      watched (const watched&) = delete;
      // The noexcept instantiation discards the throwing branch.
      // NOLINTNEXTLINE(bugprone-exception-escape)
      watched (watched&& other) noexcept (not ThrowMoves) : base (std::move (other)) {
        if constexpr (ThrowMoves)
          charge (moves_left);
      }
      // The noexcept instantiation discards the throwing branch.
      // NOLINTNEXTLINE(bugprone-exception-escape)
      watched& operator= (watched&& other) noexcept (not ThrowMoves) {
        base::operator= (std::move (other));
        if constexpr (ThrowMoves)
          charge (moves_left);
        return *this;
      }
      // Intercept the base API to inject read failures.
      // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
      [[nodiscard]] auto operator[] (size_t i) const {
        charge (reads_left);
        ++coordinate_reads;
        return base::operator[] (i);
      }
      [[nodiscard]] auto partial_order (const watched& other) const {
        charge (orders_left);
        return base::partial_order (other);
      }
      // Intercept the base API to inject copy failures.
      // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
      [[nodiscard]] watched copy () const {
        charge (copies_left);
        return watched (base::copy ());
      }
      [[nodiscard]] watched meet (const watched& other) const {
        return watched (base::meet (other));
      }
      [[nodiscard]] watched join (const watched& other) const {
        return watched (base::join (other));
      }
  };
  static_assert (posets::Vector<watched<false>> and posets::Vector<watched<true>>);
  static_assert (
      not std::is_nothrow_constructible_v<downset<watched<false>>, std::vector<watched<false>>&&>);

  template <bool ThrowMoves>
  void covered_and_late () {
    using vector_type = watched<ThrowMoves>;
    const region<std::int8_t> rows {{3, 0}, {2, 1}, {1, 2}, {0, 3}};
    auto subject = build<vector_type> (rows);
    auto covering = build<vector_type> ({{4, 4}});
    const auto capacity = subject.get_backing_vector ().capacity ();
    bool threw = false;
    allocations_left = 0;
    vector_type::copies_left = 0;
    try {
      subject.intersect_with (covering);
      subject.intersect_with (subject);
      subject.union_with (std::move (subject));
    } catch (...) {
      threw = true;
    }
    allocations_left = -1;
    vector_type::reset ();
    require (not threw, "covered/self operation allocated or copied");
    // Self-union must return before consuming its own generators.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    require (subject.get_backing_vector ().capacity () == capacity, "covered capacity changed");
    check (subject, rows, rows);
    const auto* value = &subject.get_backing_vector ().front ();
    require (unpack (*value) == row<std::int8_t> ({0, 3}), "covered tie order changed");

    const region<std::int8_t> rhs {{0, 3}, {1, 2}, {2, 1}};
    auto late_right = build<vector_type> (rhs);
    for (int failure = 0; failure < 2; ++failure) {
      bool failed = false;
      if (failure == 0)
        allocations_left = 0;
      else
        vector_type::copies_left = 0;
      try {
        subject.intersect_with (late_right);
      } catch (const std::bad_alloc&) {
        failed = failure == 0;
      } catch (const vector_error&) {
        failed = failure == 1;
      }
      allocations_left = -1;
      vector_type::reset ();
      require (failed, "late-changing intersection did not reach the copy/allocation sentinel");
      check (subject, rows, rows);
      check (late_right, rhs, rows);
    }
    subject.intersect_with (late_right);
    check (subject, meets (rows, rhs), rows);
    region<std::int8_t> order;
    for (const auto& v : subject)
      order.push_back (unpack (v));
    require (order == region<std::int8_t> ({{2, 1}, {1, 2}, {0, 3}}), "late prefix tie order");

    const region<std::int8_t> increasing {{6, -5}, {4, -1}, {1, 4}, {-2, 9}};
    for (size_t missing = 0; missing < increasing.size (); ++missing) {
      auto right = increasing;
      right.erase (right.begin () + static_cast<std::ptrdiff_t> (missing));
      auto changed = build<vector_type> (increasing);
      changed.intersect_with (build<vector_type> (right));
      check (changed, meets (increasing, right), increasing);
    }
    auto empty = build<vector_type> ({});
    subject.intersect_with (empty);
    empty.intersect_with (covering);
    check (subject, {}, rows);
    check (empty, {}, rows);
  }

  void cached_ranks_and_counts () {
    using vector_type = watched<false>;
    auto left = build<vector_type> ({{4, 0}, {0, 4}});
    auto right = build<vector_type> ({{2, 2}});
    vector_type::coordinate_reads = 0;
    left.union_with (std::move (right));
    require (vector_type::coordinate_reads == 0, "union recomputed a cached rank");
    auto covering = build<vector_type> ({{5, 5}});
    vector_type::coordinate_reads = 0;
#ifdef POSETS_RANK_STATS
    posets::utils::rank_stats_reset ();
#endif
    left.intersect_with (covering);
    require (vector_type::coordinate_reads == 0, "covered intersection recomputed cached ranks");
#ifdef POSETS_RANK_STATS
    const auto covered = posets::utils::rank_stats_current ();
    require (covered.contains_calls == left.size () and covered.insert_attempts == 0 and
                 covered.intersection_short_circuits == left.size (),
             "covered rank counters");
#endif
    auto restricted = build<vector_type> ({{4, 0}, {2, 2}});
    vector_type::coordinate_reads = 0;
#ifdef POSETS_RANK_STATS
    const auto left_size = left.size ();
    posets::utils::rank_stats_reset ();
#endif
    left.intersect_with (restricted);
#ifdef POSETS_RANK_STATS
    const auto changed = posets::utils::rank_stats_current ();
    require (changed.contains_calls == left_size, "changed containment scanned twice");
    require (changed.insert_attempts ==
                 changed.intersection_short_circuits + changed.intersection_pair_meets,
             "changed insert counters");
    require (vector_type::coordinate_reads == 2 * changed.intersection_pair_meets,
             "unchanged intersection copies recomputed rank");
#endif
    vector_type::reset ();
  }

  row<std::int8_t> bit_pair_row (size_t identity) {
    constexpr size_t pairs = std::numeric_limits<size_t>::digits;
    row<std::int8_t> result (2 * pairs);
    for (size_t bit = 0; bit < pairs; ++bit) {
      const auto value = (identity >> bit) & size_t {1};
      result[2 * bit] = static_cast<std::int8_t> (value);
      result[(2 * bit) + 1] = static_cast<std::int8_t> (1 - value);
    }
    return result;
  }

  template <class V>
  region<std::int8_t> fill_backing_capacity (downset<V>& subject) {
    require (subject.size () == 0, "capacity fixture requires an empty set");
    region<std::int8_t> rows;
    const auto append = [&] {
      auto value = bit_pair_row (rows.size ());
      require (subject.insert (vector<V> (value)), "capacity fixture point was not incomparable");
      rows.push_back (std::move (value));
    };
    for (int i = 0; i < 4; ++i)
      append ();
    const auto capacity = subject.get_backing_vector ().capacity ();
    while (subject.size () < capacity)
      append ();
    require (subject.size () == capacity and subject.get_backing_vector ().capacity () == capacity,
             "capacity fixture did not fill actual backing storage");
    return rows;
  }

  template <class V>
  void allocation_failures () {
    bool completed = false;
    bool first_growth_failed = false;
    bool second_growth_failed = false;
    for (long fail = 0; fail < 32; ++fail) {
      auto subject = build<V> ({});
      auto candidate = vector<V> ({3, 3});
      bool threw = false;
      try {
        allocations_left = fail;
        require (subject.insert (std::move (candidate)), "first insertion rejected");
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (threw) {
        first_growth_failed or_eq fail == 0;
        second_growth_failed or_eq fail == 1;
        if (fail == 1)
          require (subject.get_backing_vector ().capacity () > 0,
                   "second allocation failure preceded successful generator reserve");
        check (subject, {}, {{3, 3}, {-1, -1}});
        // The preflight failure must preserve this rvalue.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        require (unpack (candidate) == row<std::int8_t> ({3, 3}),
                 "first growth failure consumed input");
        require (subject.insert (std::move (candidate)), "first growth retry failed");
      }
      check (subject, {{3, 3}}, {{3, 3}, {4, 4}, {-1, -1}});
      if (not threw) {
        completed = true;
        break;
      }
    }
    require (completed and first_growth_failed and second_growth_failed,
             "both empty parallel-array allocation failures were not reached");

    completed = false;
    size_t populated_failures = 0;
    for (long fail = 0; fail < 64; ++fail) {
      auto subject = build<V> ({});
      auto rows = fill_backing_capacity (subject);
      const auto raw = bit_pair_row (rows.size ());
      auto candidate = vector<V> (raw);
      bool threw = false;
      try {
        allocations_left = fail;
        require (subject.insert (std::move (candidate)), "populated growth insertion rejected");
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (threw) {
        ++populated_failures;
        check (subject, rows, rows);
        // The preflight failure must preserve this rvalue.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        require (unpack (candidate) == raw, "populated growth failure consumed input");
        require (subject.insert (std::move (candidate)), "populated growth retry failed");
      }
      rows.push_back (raw);
      check (subject, rows, rows);
      if (not threw) {
        completed = true;
        break;
      }
    }
    require (completed and populated_failures > 0,
             "populated generator growth failure was not reached");

    auto covered = build<V> ({});
    const auto rows = fill_backing_capacity (covered);
    const auto capacity = covered.get_backing_vector ().capacity ();
    const row<std::int8_t> top_raw (rows.front ().size (), 1);
    auto top = vector<V> (top_raw);
    bool threw = false;
    try {
      allocations_left = 0;
      require (covered.insert (std::move (top)), "covering insertion rejected");
    } catch (...) {
      threw = true;
    }
    allocations_left = -1;
    require (not threw and covered.get_backing_vector ().capacity () == capacity,
             "covering insertion grew storage despite retiring generators");
    check (covered, {top_raw}, rows);

    std::vector<V> input;
    input.push_back (vector<V> ({1, 1}));
    bool propagated = false;
    try {
      allocations_left = 0;
      const downset<V> constructed (std::move (input));
    } catch (const std::bad_alloc&) {
      propagated = true;
    }
    allocations_left = -1;
    require (propagated, "allocating constructor did not propagate bad_alloc");
  }

  void comparison_failure_policy () {
    using vector_type = watched<false>;
    const region<std::int8_t> rows {{5, -5}, {3, -1}, {1, 3}, {-1, 7}, {-3, 11}, {-5, 15}};
    for (long fail = 0; fail <= 5; ++fail) {
      auto subject = build<vector_type> (rows);
      auto candidate = vector<vector_type> ({4, 8});
      bool threw = false;
      vector_type::orders_left = fail;
      try {
        subject.insert (std::move (candidate));
      } catch (const vector_error&) {
        threw = true;
      }
      vector_type::reset ();
      require (threw, "comparison injection did not trigger");
      if (fail < 5) {
        check (subject, rows, rows);
      }
      else {
        check (subject, {}, rows);
        require (subject.insert (vector<vector_type> ({1, 1})), "valid-empty result not reusable");
        check (subject, {{1, 1}}, {{1, 1}, {2, 2}});
      }
    }
    auto rejected = build<vector_type> ({{10, 10}});
    vector_type::reads_left = 0;
    bool rank_threw = false;
    try {
      rejected.insert (vector<vector_type> ({1, 1}));
    } catch (const vector_error&) {
      rank_threw = true;
    }
    vector_type::reset ();
    require (rank_threw, "rank-access injection");
    check (rejected, {{10, 10}}, {{1, 1}, {10, 10}});
    vector_type::orders_left = 0;
    bool threw = false;
    try {
      rejected.insert (vector<vector_type> ({1, 1}));
    } catch (const vector_error&) {
      threw = true;
    }
    vector_type::reset ();
    require (threw, "rejection comparison injection");
    check (rejected, {{10, 10}}, {{1, 1}, {10, 10}});
  }

  void throwing_move_staging () {
    using vector_type = watched<true>;
    const region<std::int8_t> rows {{5, -5}, {3, -1}, {1, 3}, {-1, 7}, {-3, 11}, {-5, 15}};
    for (int mode = 0; mode < 3; ++mode) {
      bool completed = false;
      size_t failures = 0;
      for (long fail = 0; fail < 128; ++fail) {
        auto subject = build<vector_type> (rows);
        auto candidate = vector<vector_type> ({4, 8});
        if (mode == 0)
          vector_type::copies_left = fail;
        else if (mode == 1)
          vector_type::moves_left = fail;
        else
          vector_type::orders_left = fail;
        bool threw = false;
        try {
          subject.insert (std::move (candidate));
        } catch (const vector_error&) {
          threw = true;
        }
        vector_type::reset ();
        if (threw) {
          ++failures;
          check (subject, rows, rows);
          // The rvalue may have been consumed by a throwing move; retry fresh.
          require (subject.insert (vector<vector_type> ({4, 8})), "staged retry failed");
        }
        auto expected = rows;
        expected.push_back ({4, 8});
        check (subject, expected, expected);
        if (not threw) {
          completed = true;
          break;
        }
      }
      require (completed and failures > 0, "throwing Vector sweep did not finish");
    }
  }

  void staging_allocation_failures () {
    using vector_type = watched<true>;
    const region<std::int8_t> rows {{5, -5}, {3, -1}, {1, 3}, {-1, 7}};
    bool completed = false;
    size_t failures = 0;
    for (long fail = 0; fail < 128; ++fail) {
      auto subject = build<vector_type> (rows);
      auto candidate = vector<vector_type> ({4, 8});
      bool threw = false;
      try {
        allocations_left = fail;
        subject.insert (std::move (candidate));
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (threw) {
        ++failures;
        check (subject, rows, rows);
        require (subject.insert (vector<vector_type> ({4, 8})), "staging allocation retry failed");
      }
      auto expected = rows;
      expected.push_back ({4, 8});
      check (subject, expected, expected);
      if (not threw) {
        completed = true;
        break;
      }
    }
    require (completed and failures >= 3, "staging allocation sites not covered");
  }
}  // namespace

int main () try {
  differential<posets::vectors::vector_backed<std::int8_t>> ();
  differential<posets::vectors::simd_vector_backed<std::int8_t>> ();
  floating_and_wide ();
  covered_and_late<false> ();
  covered_and_late<true> ();
  cached_ranks_and_counts ();
  allocation_failures<posets::vectors::vector_backed<std::int8_t>> ();
  allocation_failures<posets::vectors::simd_vector_backed<std::int8_t>> ();
  comparison_failure_policy ();
  throwing_move_staging ();
  staging_allocation_failures ();
  std::cout << "ranked default oracle/allocation fixtures passed\n";
} catch (const std::exception& error) {
  allocations_left = -1;
  std::cerr << error.what () << '\n';
  return 1;
} catch (...) {
  allocations_left = -1;
  std::cerr << "unexpected Vector exception\n";
  return 1;
}
