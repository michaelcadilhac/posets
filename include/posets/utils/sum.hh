#pragma once

#include <type_traits>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace posets::utils {
  static_assert (std::numeric_limits<size_t>::digits <= 64);
  static_assert (std::numeric_limits<__int128_t>::digits >= 127);

  template <typename T>
  inline constexpr bool EXACT_SUM_COORDINATE =
      std::is_integral_v<T> and std::is_signed_v<T> and std::numeric_limits<T>::digits <= 63;

  // Narrow coordinates retain 8-byte keys/caches. Full-width coordinates need
  // GCC/Clang's signed 128-bit integer for every size_t dimension.
  template <typename T>
  using sum_t = std::conditional_t<(std::is_integral_v<T> and std::numeric_limits<T>::digits > 31),
                                   __int128_t, int64_t>;

  template <typename V>
  using vector_sum_t = sum_t<typename V::value_type>;

  // With d coordinate value bits and p sum value bits, k <= 2^(p-d)
  // bounds every prefix in [-2^p, 2^p-1]. For i8/i16/i32 this gives
  // 2^56/2^48/2^32. For i64 the bound is 2^64, admitting every size_t.
  template <typename T>
  size_t check_sum_dimension (size_t k) {
    static_assert (EXACT_SUM_COORDINATE<T>,
                   "exact sums require signed integral coordinates of at most 64 bits");
    constexpr int exponent =
        std::numeric_limits<sum_t<T>>::digits - std::numeric_limits<T>::digits;
    if constexpr (std::numeric_limits<size_t>::digits > exponent)
      if (k > (uint64_t {1} << exponent)) [[unlikely]]
        throw std::length_error {"vector dimension exceeds exact sum domain"};
    return k;
  }

  template <typename V>
  [[nodiscard]] vector_sum_t<V> coordinate_sum (const V& v) {
    check_sum_dimension<typename V::value_type> (v.size ());
    vector_sum_t<V> sum = 0;
    for (size_t i = 0; i < v.size (); ++i)
      sum += static_cast<vector_sum_t<V>> (v[i]);
    return sum;
  }
}
