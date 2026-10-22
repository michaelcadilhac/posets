// The optional rank-downset counters.
//
// Built twice: once with POSETS_RANK_STATS and once without, so the same file
// proves both that the counters count and that the header compiles when they
// are switched off.  A counter that only ever builds one way is how an
// instrumentation header rots.

#include <initializer_list>

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <sstream>
#include <utility>
#include <vector>

#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/utils/rank_stats.hh>
#include <posets/vectors.hh>

namespace {
  using value_type = std::int8_t;
  using vector_type = posets::vectors::vector_backed<value_type>;
  using downset_type = posets::downsets::rank_bucketed_vector_backed<vector_type>;

  vector_type vector (value_type first, value_type second) {
    const std::array<value_type, 2> values {first, second};
    return vector_type {std::span<const value_type> {values}};
  }

  // The vector type is move-only, so an initializer list will not do: it would
  // copy, and the copy constructor is deleted.
  template <typename... Args>
  downset_type downset (Args&&... elements) {
    std::vector<vector_type> backing;
    backing.reserve (sizeof...(elements));
    (backing.push_back (std::forward<Args> (elements)), ...);
    return downset_type {std::move (backing)};
  }

  void check (const downset_type& d, std::initializer_list<std::array<value_type, 2>> maxima) {
    assert (d.size () == maxima.size ());
    auto it = d.begin ();
    for (const auto& values : maxima) {
      assert (*it == vector (values[0], values[1]));
      ++it;
    }

    for (value_type x = -3; x <= 7; ++x)
      for (value_type y = -3; y <= 7; ++y) {
        bool contained = false;
        for (const auto& values : maxima)
          contained or_eq x <= values[0] and y <= values[1];
        assert (d.contains (vector (x, y)) == contained);
      }
  }

  void check_ranked_operations () {
    auto d = downset (vector (4, -2), vector (0, 5), vector (2, 1), vector (-1, 6));
    check (d, {{4, -2}, {2, 1}, {-1, 6}, {0, 5}});
    assert (not d.insert (vector (2, 1)));
    assert (not d.insert (vector (1, 1)));
    assert (d.insert (vector (3, 2)));
    check (d, {{4, -2}, {3, 2}, {-1, 6}, {0, 5}});

    d.union_with (downset (vector (5, -1)));
    check (d, {{5, -1}, {3, 2}, {-1, 6}, {0, 5}});
    d.intersect_with (downset (vector (2, 4)));
    check (d, {{0, 4}, {2, 2}});

    auto moved = downset (vector (7, 7));
    moved = std::move (d);
    check (moved, {{0, 4}, {2, 2}});
    check (moved.apply ([] (const auto& v) { return v.copy (); }), {{2, 2}, {0, 4}});
  }
}  // namespace

int main () {
  check_ranked_operations ();
#ifdef POSETS_RANK_STATS
  using posets::utils::rank_stats_current;
  using posets::utils::rank_stats_reset;

  // An antichain of two incomparable maxima, plus one element each of them
  // dominates, so insertion exercises both the reject and the evict path.
  rank_stats_reset ();
  auto d = downset (vector (2, 0), vector (0, 2));
  assert (d.size () == 2);
  assert (rank_stats_current ().insert_attempts == 2);
  assert (rank_stats_current ().partial_order_calls > 0);
  assert (rank_stats_current ().peak_backing_size == 2);

  // A dominated element is rejected, and the set does not grow.
  const auto before_rejected = rank_stats_current ().insert_rejected_dominated;
  assert (not d.insert (vector (1, 0)));
  assert (rank_stats_current ().insert_rejected_dominated == before_rejected + 1);
  assert (d.size () == 2);

  // A dominating element evicts what it covers.
  const auto before_removed = rank_stats_current ().existing_elements_removed;
  assert (d.insert (vector (2, 2)));
  assert (rank_stats_current ().existing_elements_removed > before_removed);
  assert (d.size () == 1);

  rank_stats_reset ();
  assert (rank_stats_current ().insert_attempts == 0);
  assert (d.contains (vector (1, 1)));
  assert (rank_stats_current ().contains_calls == 1);

  // The unused bucket index is gone; its legacy counters stay at zero.
  rank_stats_reset ();
  auto growing = downset (vector (0, 3), vector (1, 2), vector (2, 1));
  assert (growing.size () == 3);
  assert (rank_stats_current ().bucket_rebuilds == 0);
  assert (rank_stats_current ().bucket_rebuild_elements == 0);

  rank_stats_reset ();
  auto left = downset (vector (2, 2));
  auto right = downset (vector (1, 3));
  left.intersect_with (right);
  assert (rank_stats_current ().intersection_left_elements == 1);
  assert (rank_stats_current ().intersection_short_circuits +
              rank_stats_current ().intersection_pair_meets >
          0);

  rank_stats_reset ();
  auto applied = left.apply ([] (const auto& v) { return v.copy (); });
  assert (applied.size () == left.size ());
  assert (rank_stats_current ().apply_elements == left.size ());

  std::ostringstream text;
  posets::utils::rank_stats_print (text);
  assert (text.str ().find ("rank_apply_elements=") != std::string::npos);
  assert (text.str ().find ("rank_bucket_rebuilds=0") != std::string::npos);
  assert (text.str ().find ("rank_bucket_rebuild_elements=0") != std::string::npos);
#else
  // Disabled: the operations must still work, and the macros must expand to
  // nothing that needs a counter to exist.
  auto d = downset (vector (2, 0), vector (0, 2));
  assert (d.size () == 2);
  assert (not d.insert (vector (1, 0)));
  assert (d.contains (vector (1, 0)));
  auto other = downset (vector (1, 1));
  d.intersect_with (other);
  assert (d.size () >= 1);
#endif
  return 0;
}
