// Diagnostic only. Reuse the exact captured-operation kernel; discard its clocks.
#define POSETS_TRACK_ALLOCATIONS 1
#define POSETS_RANK_STATS
#include "allocation-profile.hh"
#include "rank-profile-counters.hh"

#include <cinttypes>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/vectors.hh>

static void profile_start ();
static void profile_stop ();
#define CALLGRIND_START_INSTRUMENTATION profile_start ()
#define CALLGRIND_STOP_INSTRUMENTATION profile_stop ()
#include "matrix-cpre.hh"

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

template <class V>
struct measured_vector : V {
    using V::V;
    measured_vector (const measured_vector&) = delete;
    measured_vector (V&& value) noexcept (std::is_nothrow_move_constructible_v<V>)
        : V (std::move (value)) {
      if (rank_probe::active)
        ++rank_probe::work.vectors.base_adoptions_completed;
    }
    measured_vector (measured_vector&& value) noexcept (std::is_nothrow_move_constructible_v<V>)
        : V (std::move (value)) {
      if (rank_probe::active)
        ++rank_probe::work.vectors.move_constructions_completed;
    }
    measured_vector& operator= (measured_vector&& value) noexcept (std::is_nothrow_move_assignable_v<V>) {
      V::operator= (std::move (value));
      if (rank_probe::active)
        ++rank_probe::work.vectors.move_assignments_completed;
      return *this;
    }
    [[nodiscard]] measured_vector copy () const {
      if (rank_probe::active)
        ++rank_probe::work.vectors.copy_attempts;
      return measured_vector (V::copy ());
    }
    [[nodiscard]] auto partial_order (const measured_vector& other) const {
      if (rank_probe::active)
        ++rank_probe::work.vectors.partial_order_attempts;
      return V::partial_order (other);
    }
    [[nodiscard]] measured_vector meet (const measured_vector& other) const {
      if (rank_probe::active)
        ++rank_probe::work.vectors.meet_attempts;
      return measured_vector (V::meet (other));
    }
    [[nodiscard]] measured_vector join (const measured_vector& other) const {
      return measured_vector (V::join (other));
    }
};

template <class V>
constexpr bool representation_matches =
    posets::Vector<measured_vector<V>> and sizeof (V) == sizeof (measured_vector<V>) and
    alignof (V) == alignof (measured_vector<V>) and
    std::is_same_v<typename V::value_type, typename measured_vector<V>::value_type> and
    std::is_nothrow_move_constructible_v<V> == std::is_nothrow_move_constructible_v<measured_vector<V>> and
    std::is_nothrow_move_assignable_v<V> == std::is_nothrow_move_assignable_v<measured_vector<V>>;
static_assert (representation_matches<posets::vectors::vector_backed<int8_t>>);
static_assert (representation_matches<posets::vectors::simd_vector_backed<int8_t>>);

// No serialization/allocation is done until a snapshot has closed its window.
class phase_scope {
    const char* name;
    size_t entry;
    bool open = true;
    struct captured_phase {
        rank_probe::counters work;
        posets::utils::rank_stats stats;
        size_t live, peak, allocations, requested;
    };
    static_assert (std::is_trivially_copyable_v<captured_phase>);
    captured_phase close () noexcept {
      const captured_phase captured {rank_probe::work, posets::utils::rank_stats_current (),
          allocation_tracking::live, allocation_tracking::peak, allocation_tracking::count,
          allocation_tracking::allocated_bytes};
      rank_probe::active = false;
      open = false;
      return captured;
    }

  public:
    explicit phase_scope (const char* phase) : name (phase), entry (allocation_tracking::live) {
      matrix_check::require (not rank_probe::active, "nested diagnostic windows");
      rank_probe::work = {};
      posets::utils::rank_stats_reset ();
      allocation_tracking::reset_peak ();
      rank_probe::active = true;
    }
    phase_scope (const phase_scope&) = delete;
    phase_scope& operator= (const phase_scope&) = delete;
    ~phase_scope () {
      if (open)
        finish (false);
    }
  private:
    void report (const captured_phase& captured, bool success, size_t size = 0,
                 size_t capacity = 0, bool has_result = false) const noexcept {
      const auto& work = captured.work;
      const auto& stats = captured.stats;
      const auto live = captured.live;
      const auto peak = captured.peak;
      const auto allocations = captured.allocations;
      const auto requested = captured.requested;
      std::printf ("phase=%s success=%d has_result=%d size=%zu generator_capacity=%zu"
                   " entry_live=%zu exit_live=%zu live_delta=%" PRId64
                   " extra_peak=%zu allocations=%zu requested_bytes=%zu"
                   " rank_started=%" PRIu64 " rank_completed=%" PRIu64
                   " rank_active_coordinates_planned=%" PRIu64
                   " rank_active_coordinates_completed=%" PRIu64,
                   name, int (success), int (has_result), size, capacity, entry, live,
                   static_cast<int64_t> (live) - static_cast<int64_t> (entry), peak - entry,
                   allocations, requested, work.rank_started, work.rank_completed,
                   work.rank_active_coordinates_planned, work.rank_active_coordinates_completed);
      std::printf (" vector_copy_attempts=%" PRIu64 " vector_move_constructions_completed=%" PRIu64
                   " vector_move_assignments_completed=%" PRIu64 " vector_base_adoptions_completed=%" PRIu64
                   " vector_partial_order_attempts=%" PRIu64 " vector_meet_attempts=%" PRIu64,
                   work.vectors.copy_attempts, work.vectors.move_constructions_completed,
                   work.vectors.move_assignments_completed, work.vectors.base_adoptions_completed,
                   work.vectors.partial_order_attempts, work.vectors.meet_attempts);
      std::printf (" intersections_started=%" PRIu64 " intersections_completed=%" PRIu64
                   " intersections_noop=%" PRIu64 " intersections_changed=%" PRIu64
                   " last_intersection_left=%zu last_intersection_queries=%zu"
                   " last_intersection_reason=%s last_intersection_first_failed_query=",
                   work.intersections_started, work.intersections_completed, work.intersections_noop,
                   work.intersections_changed, work.last_left_size, work.last_queries, work.last_reason);
      if (work.last_has_failed_query)
        std::printf ("%zu", work.last_first_failed_query);
      else
        std::printf ("null");
      std::printf (" rank_contains_calls=%" PRIu64 " rank_partial_order_calls=%" PRIu64
                   " rank_insert_attempts=%" PRIu64 " rank_insert_rejected_dominated=%" PRIu64
                   " rank_existing_elements_removed=%" PRIu64 " rank_union_insertions=%" PRIu64
                   " rank_intersection_left_elements=%" PRIu64 " rank_intersection_short_circuits=%" PRIu64
                   " rank_intersection_pair_meets=%" PRIu64 " rank_apply_elements=%" PRIu64
                   " rank_peak_backing_size=%" PRIu64 " rank_bucket_rebuilds=%" PRIu64
                   " rank_bucket_rebuild_elements=%" PRIu64 "\n",
                   stats.contains_calls, stats.partial_order_calls, stats.insert_attempts,
                   stats.insert_rejected_dominated, stats.existing_elements_removed, stats.union_insertions,
                   stats.intersection_left_elements, stats.intersection_short_circuits,
                   stats.intersection_pair_meets, stats.apply_elements, stats.peak_backing_size,
                   stats.bucket_rebuilds, stats.bucket_rebuild_elements);
      std::fflush (stdout);
    }
  public:
    void finish (bool success) noexcept {
      const auto captured = close ();
      report (captured, success);
    }
    template <class S>
    void finish (const S& result) noexcept {
      const auto captured = close ();
      report (captured, true, result.size (), result.get_backing_vector ().capacity (), true);
    }
};

template <class S>
class measured_set {
    S subject;
    struct adopt {};
    measured_set (S&& value, adopt) : subject (std::move (value)) {}
    template <class R>
    static S construct (R&& input) {
      phase_scope phase ("constructor_only_caller_alive");
      S result (std::forward<R> (input));
      phase.finish (result);
      return result;
    }

  public:
    using value_type = typename S::value_type;
    measured_set (std::vector<value_type>&& input) : subject (construct (std::move (input))) {}
    measured_set (value_type&& input) : subject (construct (std::move (input))) {}
    measured_set (measured_set&&) = default;
    measured_set& operator= (measured_set&&) = default;
    bool contains (const value_type& query) const {
      phase_scope phase ("contains");
      const bool answer = subject.contains (query);
      phase.finish (subject);
      return answer;
    }
    template <class F>
    auto apply (const F& mapping) const {
      phase_scope phase ("apply");
      auto result = subject.apply (mapping);
      phase.finish (result);
      return measured_set (std::move (result), adopt {});
    }
    void union_with (measured_set&& other) {
      phase_scope phase ("union_rhs_still_alive");
      subject.union_with (std::move (other.subject));
      phase.finish (subject);
    }
    void intersect_with (const measured_set& other) {
      phase_scope phase ("intersection_rhs_still_alive");
      subject.intersect_with (other.subject);
      phase.finish (subject);
    }
    auto size () const { return subject.size (); }
    auto begin () { return subject.begin (); }
    auto end () { return subject.end (); }
    auto begin () const { return subject.begin (); }
    auto end () const { return subject.end (); }
    auto& get_backing_vector () { return subject.get_backing_vector (); }
    const auto& get_backing_vector () const { return subject.get_backing_vector (); }
};

static bool membership = false;
static bool roi_mode = false;
static size_t next_region = 0, completed_regions = 0;
static std::optional<phase_scope> roi;
static void profile_start () {
  const char* contains_names[] {"build_normalized", "contains", "rebuild_normalized_first_alive"};
  const char* cpre_names[] {"build_normalized", "cpre_after_rhs_destruction_net", "contains",
                          "rebuild_normalized_first_alive"};
  matrix_check::require (roi_mode and not roi and next_region < (membership ? 3U : 4U),
                         "changed diagnostic ROI sequence");
  roi.emplace (membership ? contains_names[next_region] : cpre_names[next_region]);
  ++next_region;
}
static void profile_stop () {
  matrix_check::require (roi.has_value (), "diagnostic ROI stop without start");
  roi->finish (true);
  roi.reset ();
  ++completed_regions;
}

template <class V>
void replay (const std::filesystem::path& path) {
  using vector_type = measured_vector<V>;
  using backend = posets::downsets::rank_bucketed_vector_backed<vector_type>;
  std::printf ("vector_bytes=%zu vector_alignment=%zu move_construct_noexcept=%d move_assign_noexcept=%d\n",
               sizeof (vector_type), alignof (vector_type),
               int (std::is_nothrow_move_constructible_v<vector_type>),
               int (std::is_nothrow_move_assignable_v<vector_type>));
  const auto entry_live = allocation_tracking::live;
  {
    // Eligibility/source checks are bound externally. The unchanged kernel still
    // checks the actual result/membership; source_verified never suppresses those.
    if (roi_mode) {
      const auto ignored_clocks = matrix_cpre::run<backend> (path, 1, false, false, true);
      matrix_check::require (next_region == (membership ? 3U : 4U) and
                             completed_regions == next_region, "missing diagnostic ROI");
      (void) ignored_clocks;
    }
    else {
      const auto no_clocks = matrix_cpre::run<measured_set<backend>> (path, 1, true, false, true);
      (void) no_clocks;
    }
  }
  std::printf ("kernel_check=pass after_kernel_cleanup_live=%zu cleanup_live_delta=%" PRId64 "\n",
               allocation_tracking::live,
               static_cast<int64_t> (allocation_tracking::live) - static_cast<int64_t> (entry_live));
}

#ifndef RANK_PROFILE_NO_MAIN
int main (int argc, char** argv) try {
  matrix_check::require (argc == 4, "usage: PROFILE TRACE vector_backed|simd_vector_backed operations|roi");
  const std::filesystem::path path (argv[1]);
  const std::string vector (argv[2]), mode (argv[3]);
  matrix_check::require (mode == "operations" or mode == "roi", "unknown diagnostic mode");
  roi_mode = mode == "roi";
  const auto input = matrix_cpre::load (path);
  membership = input.membership;
  std::printf ("instrumented=1 clocks_discarded=1 vector=%s mode=%s dimension=%zu numeric=%zu"
               " before=%zu actions=%zu selected_lanes=%zu width_selector=%d element_bytes=%zu\n", vector.c_str (), mode.c_str (),
               input.dimension, input.numeric, input.before.size (), input.actions.size (),
               static_cast<size_t> (posets::utils::simd_traits<int8_t>::simd_size),
               int (POSETS_SIMD_WIDTH), sizeof (int8_t));
  std::fflush (stdout);
  if (vector == "vector_backed")
    replay<posets::vectors::vector_backed<int8_t>> (path);
  else {
    matrix_check::require (vector == "simd_vector_backed", "unknown diagnostic vector");
    replay<posets::vectors::simd_vector_backed<int8_t>> (path);
  }
} catch (const std::exception& error) {
  if (roi) {
    roi->finish (false);
    roi.reset ();
  }
  std::fprintf (stderr, "profile_failure=%s\n", error.what ());
  return 1;
}
#endif
