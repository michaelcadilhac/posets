#pragma once

#include <experimental/simd>

namespace posets::utils {
  template <typename Elt>
  struct simd_traits {
#if defined(SIMD_IS_NATIVE) && SIMD_IS_NATIVE
      static constexpr auto simd_size =
          std::experimental::simd_size_v<Elt, std::experimental::simd_abi::native<Elt>>;
#elif SIMD_IS_MAX
      static constexpr auto simd_size = std::experimental::simd_abi::max_fixed_size<Elt>;
#else
      static constexpr auto simd_size = std::experimental::simd_size_v<Elt>;
#endif

      static constexpr auto simds_for (size_t k) { return (k + simd_size - 1) / simd_size; }

      using fssimd = std::experimental::fixed_size_simd<Elt, simd_size>;

      static constexpr auto memory_alignment_v = std::experimental::memory_alignment_v<fssimd>;
  };
}
