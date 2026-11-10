// Private PMU entry lifecycle around the original selected-rank full CLI.
#include "downset-bm-pmu.cc"

#ifndef POSETS_MATRIX_ONLY
#error "Use the unchanged selected-rank matrix build flags"
#endif
#ifndef POSETS_MATRIX_BACKEND
#error "Use the unchanged rank backend selector"
#endif
#ifdef POSETS_RANK_STATS
#error "PMU requires rank statistics undefined"
#endif
static_assert(POSETS_LAZY_KDTREE_STATS == 0);

int main(int argc, char* argv[]) try {
  rank_pmu::initialize();
  std::cout << "rank_pmu_width_selector=" << int(POSETS_SIMD_WIDTH)
            << " selected_lanes=" << posets::utils::simd_traits<std::int8_t>::simd_size
            << " original_vector_flavors=2 rank_stats_enabled=0 allocation_hooks_enabled=0\n";
  return rank_pmu::finish(rank_pmu_original_main(argc, argv));
} catch (const std::exception& error) {
  rank_pmu::failed();
  std::cerr << "rank_pmu_failure=" << error.what() << '\n';
  return 1;
}
