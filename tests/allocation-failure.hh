#pragma once

// Keep hooks enabled under Memcheck with somalloc=nouserintercepts (tests/meson.build).
// Standard replaceable C++ allocation functions plus POSIX posix_memalign work
// on both Linux and macOS; these hooks do not use glibc internals or interposition.
#include <cstdlib>
#include <new>

namespace allocation_failure {
  inline long allocations_left = -1;
  inline long aligned_attempts = 0;
  inline long aligned_failures = 0;
  inline std::size_t largest_failed_alignment = 0;

  inline void charge () {
    if (allocations_left == 0)
      throw std::bad_alloc ();
    if (allocations_left > 0)
      --allocations_left;
  }
}
using allocation_failure::allocations_left;

void* operator new (std::size_t bytes) {
  allocation_failure::charge ();
  if (void* memory = std::malloc (bytes ? bytes : 1))
    return memory;
  throw std::bad_alloc ();
}

void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void* operator new (std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[] (std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new[] (bytes);
  } catch (...) {
    return nullptr;
  }
}
void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete (void* memory, const std::nothrow_t&) noexcept { ::operator delete (memory); }
void operator delete[] (void* memory, const std::nothrow_t&) noexcept {
  ::operator delete[] (memory);
}

// Replace every aligned form too: std::vector<embedded SIMD> and SIMD block
// vectors use C++17 over-aligned new even on portable, non-AVX targets.
void* operator new (std::size_t bytes, std::align_val_t alignment) {
  ++allocation_failure::aligned_attempts;
  if (allocations_left == 0) {
    ++allocation_failure::aligned_failures;
    if (static_cast<std::size_t> (alignment) > allocation_failure::largest_failed_alignment)
      allocation_failure::largest_failed_alignment = static_cast<std::size_t> (alignment);
  }
  allocation_failure::charge ();
  void* memory = nullptr;
  if (posix_memalign (&memory, static_cast<std::size_t> (alignment), bytes ? bytes : 1) == 0)
    return memory;
  throw std::bad_alloc ();
}
void* operator new[] (std::size_t bytes, std::align_val_t alignment) {
  return ::operator new (bytes, alignment);
}
void* operator new (std::size_t bytes, std::align_val_t alignment,
                    const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes, alignment);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[] (std::size_t bytes, std::align_val_t alignment,
                      const std::nothrow_t&) noexcept {
  return ::operator new (bytes, alignment, std::nothrow);
}
void operator delete (void* memory, std::align_val_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept {
  std::free (memory);
}
void operator delete (void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ::operator delete (memory, alignment);
}
void operator delete[] (void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ::operator delete[] (memory, alignment);
}
