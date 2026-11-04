#include "matrix-check.hh"

#include <cstdint>
#include <iostream>
#include <memory>

#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

template <class T>
void check () {
  using traits = posets::utils::simd_traits<T>;
  using block = traits::fssimd;
  using dynamic = posets::vectors::simd_vector_backed<T>;
  using fixed = posets::vectors::simd_array_backed<T, 129>;
#if POSETS_EXPECT_WIDTH == POSETS_SIMD_WIDTH_NATIVE
  static_assert (traits::simd_size ==
                 std::experimental::simd_size_v<T, std::experimental::simd_abi::native<T>>);
#elif POSETS_EXPECT_WIDTH == POSETS_SIMD_WIDTH_COMPATIBLE
  static_assert (traits::simd_size == std::experimental::simd_size_v<T>);
#elif POSETS_EXPECT_WIDTH == POSETS_SIMD_WIDTH_MAX
  static_assert (traits::simd_size == std::experimental::simd_abi::max_fixed_size<T>);
#else
# error Missing expected width
#endif
  static_assert (dynamic::items_per_block == traits::simd_size);
  static_assert (sizeof (block) == traits::simd_size * sizeof (T));
  static_assert (alignof (block) == traits::memory_alignment_v);
#if POSETS_EXPECT_WIDTH == POSETS_SIMD_WIDTH_NATIVE
# if defined(__AVX512BW__) && defined(__AVX512F__)
  static_assert (sizeof (block) == 64);
# elif defined(__AVX2__) && !defined(__AVX512F__)
  static_assert (sizeof (block) == 32);
# endif
#endif
  auto aligned = [] (const void* p, size_t alignment) {
    matrix_check::require (reinterpret_cast<std::uintptr_t> (p) % alignment == 0,
                           "SIMD storage alignment");
  };
  posets::utils::vector_mm<T> scalar (129);
  aligned (scalar.data (), traits::memory_alignment_v);
  posets::vectors::simd_vector_t<T> blocks (3);
  aligned (blocks.data (), traits::memory_alignment_v);
  using storage = posets::vectors::simd_array_t<T, 129>;
  auto array = std::make_unique<storage[]> (3);
  aligned (array.get (), traits::memory_alignment_v);
  auto embedded = std::make_unique<fixed> (129);
  aligned (embedded.get (), traits::memory_alignment_v);
  matrix_check::vectors<dynamic> ();
  matrix_check::vectors<fixed> ();
  matrix_check::vectors<posets::vectors::simd_array_ptr_backed<T, 129>> ();
  matrix_check::vectors<posets::vectors::simd_array_backed_sum<T, 129>> ();
  matrix_check::vectors<posets::vectors::simd_array_ptr_backed_sum<T, 129>> ();
  matrix_check::vectors<posets::vectors::simd_vector_backed_sum<T>> ();
  std::cout << "element_bytes=" << sizeof (T) << " block_bytes=" << sizeof (block)
            << " alignment=" << traits::memory_alignment_v << '\n';
}

int main () {
  check<std::int8_t> ();
  check<std::int16_t> ();
  check<std::int32_t> ();
  check<std::int64_t> ();
}
