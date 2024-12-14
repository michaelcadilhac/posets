#pragma once

// https://stackoverflow.com/questions/8456236/how-is-a-vectors-data-aligned

#include <boost/align/aligned_allocator.hpp>

#include <vector>

#include <posets/utils/simd_traits.hh>

namespace posets::utils {
  template <typename T>
  using vector_mm =
      std::vector<T, boost::alignment::aligned_allocator<T, simd_traits<T>::memory_alignment_v>>;
}
