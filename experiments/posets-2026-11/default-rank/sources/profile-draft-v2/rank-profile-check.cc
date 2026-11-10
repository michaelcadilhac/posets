// Small future correctness/positive-hook check; not a timing workload.
#define RANK_PROFILE_NO_MAIN
#include "rank-profile.cc"

template <class V>
void check_profile () {
  using vector_type = measured_vector<V>;
  using backend = posets::downsets::rank_bucketed_vector_backed<vector_type>;
  const matrix_check::region rows {{2, 0}, {0, 2}};
  const auto entry_live = allocation_tracking::live;
  {
    auto left = matrix_check::build<backend> (rows);
    auto equal = matrix_check::build<backend> (rows);
    auto restricted = matrix_check::build<backend> ({{0, 2}});
    phase_scope equal_phase ("check_equal_intersection");
    left.intersect_with (equal);
    const auto equal_allocations = allocation_tracking::count;
    equal_phase.finish (left);
    matrix_check::require (rank_probe::work.intersections_completed == 1 and
                           rank_probe::work.intersections_noop == 1 and
                           rank_probe::work.last_queries == 2 and
                           not rank_probe::work.last_has_failed_query,
                           "equal-intersection diagnostic events");
#ifdef PROFILE_EXPECT_CANDIDATE
    matrix_check::require (rank_probe::work.vectors.copy_attempts == 0 and
                           equal_allocations == 0,
                           "candidate covered intersection copied or allocated");
#else
    matrix_check::require (rank_probe::work.vectors.copy_attempts == 2 and
                           equal_allocations > 0,
                           "baseline covered-intersection positive sentinel missing");
#endif
    phase_scope changed_phase ("check_late_failure");
    left.intersect_with (restricted);
    changed_phase.finish (left);
    matrix_check::require (rank_probe::work.intersections_changed == 1 and
                           rank_probe::work.last_has_failed_query and
                           rank_probe::work.last_first_failed_query == 1 and
                           rank_probe::work.last_queries == 2,
                           "late failed-query diagnostic position");
    matrix_check::check (left, {{0, 2}}, {{0, 2}, {2, 0}});
  }
  matrix_check::require (allocation_tracking::live == entry_live,
                         "backend/caller cleanup retained requested heap");
  {
    phase_scope build_phase ("check_normalized_build");
    auto constructed = matrix_check::build<backend> (rows);
    const auto build_allocations = allocation_tracking::count;
    build_phase.finish (constructed);
    matrix_check::require (rank_probe::work.rank_started == 2 and
                           rank_probe::work.rank_completed == 2 and
                           rank_probe::work.rank_active_coordinates_planned == 4 and
                           rank_probe::work.rank_active_coordinates_completed == 4 and
                           build_allocations > 0,
                           "rank-entry or allocation-hook positive sentinel missing");
  }
  matrix_check::require (allocation_tracking::live == entry_live,
                         "normalized build/caller cleanup retained requested heap");
}

int main () try {
  check_profile<posets::vectors::vector_backed<int8_t>> ();
  check_profile<posets::vectors::simd_vector_backed<int8_t>> ();
  std::printf ("profile_selfcheck=pass\n");
} catch (const std::exception& error) {
  std::fprintf (stderr, "profile_selfcheck_failure=%s\n", error.what ());
  return 1;
}
