#include "check.hh"

// The optional rank-downset counters.
//
// Built twice: once with POSETS_RANK_STATS and once without, so the same file
// proves both that the counters count and that the header compiles when they
// are switched off.  A counter that only ever builds one way is how an
// instrumentation header rots.

#include <initializer_list>

#include <array>
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
    CHECK (d.size () == maxima.size ());
    auto it = d.begin ();
    for (const auto& values : maxima) {
      CHECK (*it == vector (values[0], values[1]));
      ++it;
    }

    for (value_type x = -3; x <= 7; ++x)
      for (value_type y = -3; y <= 7; ++y) {
        bool contained = false;
        for (const auto& values : maxima)
          contained or_eq x <= values[0] and y <= values[1];
        CHECK (d.contains (vector (x, y)) == contained);
      }
  }

  void check_ranked_operations () {
    auto d = downset (vector (4, -2), vector (0, 5), vector (2, 1), vector (-1, 6));
    check (d, {{4, -2}, {2, 1}, {-1, 6}, {0, 5}});
    CHECK (not d.insert (vector (2, 1)));
    CHECK (not d.insert (vector (1, 1)));
    CHECK (d.insert (vector (3, 2)));
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

  auto equal_rank = downset (vector (2, 0), vector (0, 2));
  rank_stats_reset ();
  CHECK (equal_rank.insert (vector (1, 1)));
  CHECK (rank_stats_current ().insert_attempts == 1);
  CHECK (rank_stats_current ().partial_order_calls == 2);
  check (equal_rank, {{1, 1}, {0, 2}, {2, 0}});

  auto bounded = downset (vector (4, -2), vector (2, 1), vector (0, 5));
  rank_stats_reset ();
  CHECK (bounded.insert (vector (3, 2)));
  CHECK (rank_stats_current ().partial_order_calls == 3);
  CHECK (rank_stats_current ().existing_elements_removed == 1);
  check (bounded, {{4, -2}, {3, 2}, {0, 5}});
  rank_stats_reset ();
  bounded.intersect_with (bounded);
  CHECK (rank_stats_current ().intersection_left_elements == bounded.size ());
  CHECK (rank_stats_current ().intersection_short_circuits == bounded.size ());
  CHECK (rank_stats_current ().contains_calls == 0);
  CHECK (rank_stats_current ().insert_attempts == 0);

  // An antichain of two incomparable maxima, plus one element each of them
  // dominates, so insertion exercises both the reject and the evict path.
  rank_stats_reset ();
  auto d = downset (vector (2, 0), vector (0, 2));
  CHECK (d.size () == 2);
  CHECK (rank_stats_current ().insert_attempts == 2);
  CHECK (rank_stats_current ().partial_order_calls > 0);
  CHECK (rank_stats_current ().peak_backing_size == 2);

  // A dominated element is rejected, and the set does not grow.
  const auto before_rejected = rank_stats_current ().insert_rejected_dominated;
  CHECK (not d.insert (vector (1, 0)));
  CHECK (rank_stats_current ().insert_rejected_dominated == before_rejected + 1);
  CHECK (d.size () == 2);

  // A dominating element evicts what it covers.
  const auto before_removed = rank_stats_current ().existing_elements_removed;
  CHECK (d.insert (vector (2, 2)));
  CHECK (rank_stats_current ().existing_elements_removed > before_removed);
  CHECK (d.size () == 1);

  rank_stats_reset ();
  CHECK (rank_stats_current ().insert_attempts == 0);
  CHECK (d.contains (vector (1, 1)));
  CHECK (rank_stats_current ().contains_calls == 1);

  // The unused bucket index is gone; its legacy counters stay at zero.
  rank_stats_reset ();
  auto growing = downset (vector (0, 3), vector (1, 2), vector (2, 1));
  CHECK (growing.size () == 3);
  CHECK (rank_stats_current ().bucket_rebuilds == 0);
  CHECK (rank_stats_current ().bucket_rebuild_elements == 0);

  rank_stats_reset ();
  auto left = downset (vector (2, 2));
  auto right = downset (vector (1, 3));
  left.intersect_with (right);
  CHECK (rank_stats_current ().intersection_left_elements == 1);
  CHECK (rank_stats_current ().intersection_short_circuits +
              rank_stats_current ().intersection_pair_meets >
          0);

  rank_stats_reset ();
  auto applied = left.apply ([] (const auto& v) { return v.copy (); });
  CHECK (applied.size () == left.size ());
  CHECK (rank_stats_current ().apply_elements == left.size ());

  std::ostringstream text;
  posets::utils::rank_stats_print (text);
  CHECK (text.str ().find ("rank_apply_elements=") != std::string::npos);
  CHECK (text.str ().find ("rank_bucket_rebuilds=0") != std::string::npos);
  CHECK (text.str ().find ("rank_bucket_rebuild_elements=0") != std::string::npos);
#else
  // Disabled: the operations must still work, and the macros must expand to
  // nothing that needs a counter to exist.
  auto d = downset (vector (2, 0), vector (0, 2));
  CHECK (d.size () == 2);
  CHECK (not d.insert (vector (1, 0)));
  CHECK (d.contains (vector (1, 0)));
  auto other = downset (vector (1, 1));
  d.intersect_with (other);
  CHECK (d.size () >= 1);
#endif
  return 0;
}
