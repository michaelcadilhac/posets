#pragma once

// Scalar transfer buffers aligned to the selected SIMD block alignment.

#include <boost/align/aligned_allocator.hpp>

#include <vector>

#include <posets/utils/simd_traits.hh>

namespace posets::utils {
  template <typename T>
  using vector_mm =
      std::vector<T, boost::alignment::aligned_allocator<T, simd_traits<T>::memory_alignment_v>>;
}
