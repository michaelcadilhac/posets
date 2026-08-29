#pragma once

// Optional instrumentation for the rank-bucketed downset.
//
// Compiled out unless POSETS_RANK_STATS is defined, following the
// POSETS_BBOX_STATS precedent in bboxtree.hh.  Unlike the bbox counters, these
// accumulate globally rather than per instance: a downset here is created and
// destroyed on every apply and every intersection, so per-instance counters
// would be discarded before anything could read them.  What a caller wants to
// know is what one CPre update cost in total.
//
// The accumulator is thread-local.  The solver forks rather than threads, so
// each worker keeps its own tally and no synchronization is needed on the hot
// path.

#include <cstdint>
#include <ostream>

namespace posets::utils {

#ifdef POSETS_RANK_STATS

  struct rank_stats {
      // Membership and the partial order.
      uint64_t contains_calls {0};
      uint64_t partial_order_calls {0};

      // Insertion, which is where the antichain is maintained.
      uint64_t insert_attempts {0};
      uint64_t insert_rejected_dominated {0};
      uint64_t existing_elements_removed {0};

      // The three operations a CPre update is made of.
      uint64_t union_insertions {0};
      uint64_t intersection_left_elements {0};
      uint64_t intersection_short_circuits {0};
      uint64_t intersection_pair_meets {0};
      uint64_t apply_elements {0};

      // Representation size, and the bucket index maintained alongside it.
      uint64_t peak_backing_size {0};
      uint64_t bucket_rebuilds {0};
      uint64_t bucket_rebuild_elements {0};

      void observe_size (uint64_t size) {
        if (size > peak_backing_size)
          peak_backing_size = size;
      }
  };

  inline rank_stats& rank_stats_current () {
    static thread_local rank_stats stats;
    return stats;
  }

  inline void rank_stats_reset () { rank_stats_current () = {}; }

  inline void rank_stats_print (std::ostream& os) {
    const auto& s = rank_stats_current ();
    os << "rank_contains_calls=" << s.contains_calls
       << " rank_partial_order_calls=" << s.partial_order_calls
       << " rank_insert_attempts=" << s.insert_attempts
       << " rank_insert_rejected_dominated=" << s.insert_rejected_dominated
       << " rank_existing_elements_removed=" << s.existing_elements_removed
       << " rank_union_insertions=" << s.union_insertions
       << " rank_intersection_left_elements=" << s.intersection_left_elements
       << " rank_intersection_short_circuits=" << s.intersection_short_circuits
       << " rank_intersection_pair_meets=" << s.intersection_pair_meets
       << " rank_apply_elements=" << s.apply_elements
       << " rank_peak_backing_size=" << s.peak_backing_size
       << " rank_bucket_rebuilds=" << s.bucket_rebuilds
       << " rank_bucket_rebuild_elements=" << s.bucket_rebuild_elements;
  }

#endif

}  // namespace posets::utils

// Macros, not inline functions, so the disabled build has no argument to
// evaluate: a counter update must not keep a size computation alive.
#ifdef POSETS_RANK_STATS
# define POSETS_RANK_STAT_ADD(field, n) \
   (::posets::utils::rank_stats_current ().field += (n))
# define POSETS_RANK_STAT_SIZE(n) \
   (::posets::utils::rank_stats_current ().observe_size (n))
#else
# define POSETS_RANK_STAT_ADD(field, n) ((void) 0)
# define POSETS_RANK_STAT_SIZE(n) ((void) 0)
#endif
