#include "allocation-failure.hh"
#include "matrix-check.hh"
#include <type_traits>

#include <iostream>

#include <posets/downsets/filtered_vector_backed.hh>
#include <posets/vectors.hh>

namespace {
  using namespace matrix_check;
  template <class V>
  using downset = posets::downsets::filtered_vector_backed<V>;

  [[nodiscard]] region fixture () {
    region result;
    for (int i = 0; i < 256; ++i)
      result.push_back ({i / 16, 15 - i / 16, i % 16, 15 - i % 16, -1, 0, -1, 0});
    return result;
  }

  template <class V>
  void misses () {
    const auto rows = fixture ();
    auto subject = build<downset<V>> (rows);
    // Dimensions 0..3 have thresholds 0..15; dimensions 4..7 are constant
    // and cannot filter. The 256 incomparable points activate the real cache.
    const region queries {{16, 16, 16, 16, -1, 0, -1, 0},  // Above every selected threshold.
                          {15, 15, 0, 0, -1, 0, -1, 0},  // Each mask nonempty, intersection empty.
                          {0, 0, 0, 0, 0, 0, -1, 0}};    // Masks survive; full comparison rejects.
    for (const auto& query : queries)
      require (not subject.contains (vector<V> (query)), "filtered cold/warm miss");
    check (subject, rows, queries);
    // Preconstruct queries so a warm miss also proves query scratch stays free
    // of allocations on all three rejection paths.
    for (const auto& query : queries) {
      const auto value = vector<V> (query);
      allocations_left = 0;
      const bool answer = subject.contains (value);
      allocations_left = -1;
      require (not answer, "warm filtered miss");
    }
  }

  template <class V>
  void cache_failures (bool warm) {
    auto rows = fixture ();
    const logical added {16, -1, -1, 16, -1, 0, -1, 0};
    const auto query = vector<V> (rows.back ());
    bool completed = false;
    for (long fail = 0; fail < 1024; ++fail) {
      auto subject = build<downset<V>> (rows);
      if (warm) {
        require (subject.contains (query), "warm old cache");
        subject.insert (vector<V> (added));
      }
      bool threw = false;
      try {
        allocations_left = fail;
        require (subject.contains (query), "filtered first query");
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (threw) {
        // Failed publication must leave the cache dirty, even if some dimensions
        // were completed. A retry with no allocation budget must try rebuilding.
        bool retried = false;
        try {
          allocations_left = 0;
          (void) subject.contains (query);
        } catch (const std::bad_alloc&) {
          retried = true;
        }
        allocations_left = -1;
        require (retried, "failed cache rebuild was published");
      }
      require (subject.contains (query), "cache rebuild retry");
      allocations_left = 0;
      require (subject.contains (query), "warm filtered query allocated");
      allocations_left = -1;
      auto expected = rows;
      if (warm)
        expected.push_back (added);
      check (subject, expected, expected);
      if (not threw) {
        completed = true;
        break;
      }
    }
    require (completed, "cache failure sweep did not complete");
  }

  template <class V>
  void construction_failure () {
    auto input = std::vector<V> {};
    for (const auto& row : fixture ())
      input.push_back (vector<V> (row));
    bool threw = false;
    try {
      allocations_left = 0;
      const downset<V> subject (std::move (input));
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    allocations_left = -1;
    require (threw, "constructor allocation failure must propagate");
  }

  template <class V>
  void mutations () {
    auto rows = fixture ();
    auto subject = build<downset<V>> (rows);
    require (subject.contains (vector<V> (rows.front ())), "initial cache");
    logical added {16, 0, -1, 16, -1, 0, -1, 0};
    subject.insert (vector<V> (added));
    rows.push_back (added);
    check (subject, rows, rows);
    auto moved = std::move (subject);
    check (moved, rows, rows);
    const auto mapped_rows = rows;
    auto mapped = moved.apply ([] (const V& value) { return value.copy (); });
    check (mapped, rows, rows);
    auto other = build<downset<V>> ({logical (8, 0)});
    moved.union_with (std::move (other));
    rows.emplace_back (8, 0);
    check (moved, rows, rows);
    auto intersection = build<downset<V>> (rows);
    intersection.intersect_with (mapped);
    check (intersection, mapped_rows, rows);

    // Iteration cannot modify indexed generators. Retained backing references
    // remain supported by permanently falling back to a linear membership scan.
    static_assert (std::is_const_v<std::remove_reference_t<decltype (*moved.begin ())>>);
    auto& raw = moved.get_backing_vector ();
    require (moved.contains (vector<V> (rows.front ())), "mutable exposure");
    raw.clear ();
    for (const auto& row : fixture ())
      raw.push_back (vector<V> (row));
    require (moved.contains (vector<V> (fixture ().back ())), "retained reference refill");
    // Assignment cannot revoke the fallback while a destination reference lives.
    moved = build<downset<V>> (fixture ());
    require (moved.contains (vector<V> (fixture ().back ())), "assignment cache");
    auto high = vector<V> (logical (8, 30));
    raw.front () = std::move (high);
    require (moved.contains (vector<V> (logical (8, 29))), "retained reference edit");
  }
}
int main () try {
  misses<posets::vectors::vector_backed<int8_t>> ();
  misses<posets::vectors::simd_vector_backed<int8_t>> ();
  construction_failure<posets::vectors::vector_backed<int8_t>> ();
  construction_failure<posets::vectors::simd_vector_backed<int8_t>> ();
  for (const bool warm : {false, true}) {
    cache_failures<posets::vectors::vector_backed<int8_t>> (warm);
    cache_failures<posets::vectors::simd_vector_backed<int8_t>> (warm);
  }
  mutations<posets::vectors::vector_backed<int8_t>> ();
  mutations<posets::vectors::simd_vector_backed<int8_t>> ();
  std::cout << "filtered cache publication, retries and mutations PASS\n";
} catch (const std::exception& error) {
  allocations_left = -1;
  std::cerr << error.what () << '\n';
  return 1;
}
