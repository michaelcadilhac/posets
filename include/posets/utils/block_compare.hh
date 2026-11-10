#pragma once

#include <posets/utils/simd_traits.hh>

namespace posets::utils {
  // All loads stay inside count active lanes; padding never supplies strictness.
  template <class T, size_t Lanes, bool Simd = true>
  [[nodiscard]] bool dominates_block (const T* label, const T* value, size_t count,
                                      bool& owe_strict) {
    size_t i = 0;
    if constexpr (Simd and Lanes > 1) {
      using simd = std::experimental::fixed_size_simd<T, Lanes>;
      for (; count - i >= Lanes; i += Lanes) {
        const simd a (label + i, std::experimental::element_aligned);
        const simd b (value + i, std::experimental::element_aligned);
        if (std::experimental::any_of (a < b))
          return false;
        owe_strict = owe_strict and std::experimental::all_of (a == b);
      }
    }
    for (; i < count; ++i) {
      if (label[i] < value[i])
        return false;
      owe_strict = owe_strict and label[i] == value[i];
    }
    return true;
  }
}
