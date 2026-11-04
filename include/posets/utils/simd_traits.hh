#pragma once

#include <experimental/simd>

#include <posets/config.hh>

namespace posets::utils {
  template <typename Elt>
  struct simd_traits {
#if POSETS_SIMD_WIDTH == POSETS_SIMD_WIDTH_NATIVE
      static constexpr auto simd_size =
          std::experimental::simd_size_v<Elt, std::experimental::simd_abi::native<Elt>>;
#elif POSETS_SIMD_WIDTH == POSETS_SIMD_WIDTH_MAX
      static constexpr auto simd_size = std::experimental::simd_abi::max_fixed_size<Elt>;
#else
      static constexpr auto simd_size = std::experimental::simd_size_v<Elt>;
#endif

      static constexpr auto simds_for (size_t k) { return k / simd_size + (k % simd_size != 0); }

      using fssimd = std::experimental::fixed_size_simd<Elt, simd_size>;

      static constexpr auto memory_alignment_v = std::experimental::memory_alignment_v<fssimd>;
  };
}
