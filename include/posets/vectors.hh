#pragma once

#include <posets/concepts.hh>
#include <posets/vectors/X_and_bitset.hh>
#include <posets/vectors/generic.hh>

namespace posets::vectors {

  /// Instantiate all vector types stemming from generic:
  template <typename T, size_t K>
  using simd_array_t =
      std::array<typename utils::simd_traits<T>::fssimd, utils::simd_traits<T>::simds_for (K)>;
  static_assert (IsDataSimd<simd_array_t<char, 8>>);

  template <typename T, size_t K>
  using simd_array_backed = generic<simd_array_t<T, K>, false, true>;

  template <typename T, size_t K>
  using simd_array_backed_sum = generic<simd_array_t<T, K>, true, true>;

  template <typename T, size_t K>
  using simd_array_ptr_backed = generic<simd_array_t<T, K>, false, false>;

  template <typename T, size_t K>
  using simd_array_ptr_backed_sum = generic<simd_array_t<T, K>, true, false>;

  template <typename T>
  using simd_vector_t = std::vector<typename utils::simd_traits<T>::fssimd>;

  static_assert (IsDataSimd<simd_vector_t<char>>);

  template <typename T>
  using simd_vector_backed = generic<simd_vector_t<T>, false, true>;

  template <typename T>
  using simd_vector_backed_sum = generic<simd_vector_t<T>, true, true>;

#define ITEMS_PER_BLOCK 8

  template <typename T, size_t K>
  using array_t =
      std::array<std::array<T, ITEMS_PER_BLOCK>, (K + ITEMS_PER_BLOCK - 1) / ITEMS_PER_BLOCK>;

  static_assert (not IsDataSimd<array_t<char, 8>>);

  template <typename T, size_t K>
  using array_backed = generic<array_t<T, K>, false, true>;

  template <typename T, size_t K>
  using array_backed_sum = generic<array_t<T, K>, true, true>;

  template <typename T, size_t K>
  using array_ptr_backed = generic<array_t<T, K>, false, false>;

  template <typename T, size_t K>
  using array_ptr_backed_sum = generic<array_t<T, K>, true, false>;

  template <typename T>
  using vector_t = std::vector<std::array<T, ITEMS_PER_BLOCK>>;

  static_assert (not IsDataSimd<vector_t<char>>);

  template <typename T>
  using vector_backed = generic<vector_t<T>, false, true>;

  template <typename T>
  using vector_backed_sum = generic<vector_t<T>, true, true>;

  template <typename T>
  concept IsGenericVector = requires (T&& t) {
    { new generic (std::move (t)) } -> std::same_as<T*>;
  };

  template <template <typename, auto...> typename D, typename T>
    requires IsGenericVector<D<T>> or IsGenericVector<D<T, 42>>
  struct traits<D, T> {
      using qualified_type_t = decltype ([] () {
        if constexpr (requires (D<T> d) { true; })
          return D<T> (0);
        else
          return D<T, 42> (0);
      }());

      static constexpr auto capacity_for (size_t elts) {
        return qualified_type_t::blocks_for (elts) * qualified_type_t::items_per_block;
      }
  };

  static_assert (Vector<simd_array_backed<int, 128>>);
  static_assert (Vector<simd_array_backed_sum<int, 128>>);
  static_assert (Vector<simd_array_ptr_backed<int, 128>>);
  static_assert (Vector<simd_array_ptr_backed_sum<int, 128>>);
  static_assert (Vector<simd_vector_backed<int>>);
  static_assert (Vector<simd_vector_backed_sum<int>>);

  static_assert (Vector<array_backed<int, 128>>);
  static_assert (Vector<array_backed_sum<int, 128>>);
  static_assert (Vector<array_ptr_backed<int, 128>>);
  static_assert (Vector<array_ptr_backed_sum<int, 128>>);
  static_assert (Vector<vector_backed<int>>);
  static_assert (Vector<vector_backed_sum<int>>);

  static_assert (Vector<X_and_bitset<vector_backed<int>, 128>>);
}

namespace std {
  template <posets::Vector V>
  inline std::ostream& operator<< (std::ostream& os, const V& v) {
    return v.print (os);
  }
}
