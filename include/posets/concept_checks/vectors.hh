#pragma once

#include <posets/vectors.hh>
#include <posets/vectors/X_and_bitset.hh>
#include <posets/vectors/X_and_boolvec.hh>
#include <posets/vectors/X_and_wordvec.hh>

namespace posets::vectors::concept_checks {
  static_assert (posets::Vector<simd_array_backed<int, 128>>);
  static_assert (posets::Vector<simd_array_backed_sum<int, 128>>);
  static_assert (posets::Vector<simd_array_ptr_backed<int, 128>>);
  static_assert (posets::Vector<simd_array_ptr_backed_sum<int, 128>>);
  static_assert (posets::Vector<simd_vector_backed<int>>);
  static_assert (posets::Vector<simd_vector_backed_sum<int>>);

  static_assert (posets::Vector<array_backed<int, 128>>);
  static_assert (posets::Vector<array_backed_sum<int, 128>>);
  static_assert (posets::Vector<array_ptr_backed<int, 128>>);
  static_assert (posets::Vector<array_ptr_backed_sum<int, 128>>);
  static_assert (posets::Vector<vector_backed<int>>);
  static_assert (posets::Vector<vector_backed_sum<int>>);
  static_assert (not posets::Vector<vector_backed<unsigned int>>);

  using vector_test_int = vector_backed<int>;
  static_assert (posets::Vector<x_and_bitset<vector_test_int, 128>>);
  static_assert (posets::Vector<x_and_boolvec<vector_test_int>>);
  static_assert (posets::Vector<x_and_wordvec<vector_test_int>>);
}
