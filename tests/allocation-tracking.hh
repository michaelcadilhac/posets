#pragma once

#ifndef POSETS_TRACK_ALLOCATIONS
# define POSETS_TRACK_ALLOCATIONS 0
#endif
#if POSETS_TRACK_ALLOCATIONS

// Requested live C++ heap payload, excluding this hook's headers/alignment and
// allocator overhead. Direct malloc, mapped pages, stacks and RSS are not tracked.
# include <algorithm>
# include <cstdint>
# include <cstdlib>
# include <limits>
# include <new>

namespace allocation_tracking {
  inline size_t live = 0, peak = 0, count = 0;
  struct header {
      void* allocation;
      size_t bytes;
  };
  inline void* allocate (size_t bytes, size_t alignment) {
    if (bytes > std::numeric_limits<size_t>::max () - sizeof (header) - alignment)
      throw std::bad_alloc ();
    void* allocation = std::malloc (bytes + sizeof (header) + alignment);
    if (not allocation)
      throw std::bad_alloc ();
    const auto start = reinterpret_cast<std::uintptr_t> (allocation) + sizeof (header);
    const auto address = (start + alignment - 1) & ~(alignment - 1);
    auto* prefix = reinterpret_cast<header*> (address) - 1;
    prefix->allocation = allocation;
    prefix->bytes = bytes;
    live += bytes;
    peak = std::max (peak, live);
    ++count;
    return reinterpret_cast<void*> (address);
  }
  inline void release (void* memory) noexcept {
    if (not memory)
      return;
    const auto* prefix = static_cast<header*> (memory) - 1;
    live -= prefix->bytes;
    std::free (prefix->allocation);
  }
  inline void reset_peak () {
    peak = live;
    count = 0;
  }
}
void* operator new (size_t bytes) {
  return allocation_tracking::allocate (bytes, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new[] (size_t bytes) { return ::operator new (bytes); }
void* operator new (size_t bytes, std::align_val_t alignment) {
  return allocation_tracking::allocate (bytes, static_cast<size_t> (alignment));
}
void* operator new[] (size_t bytes, std::align_val_t alignment) {
  return ::operator new (bytes, alignment);
}
void* operator new (size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[] (size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes);
  } catch (...) {
    return nullptr;
  }
}
void* operator new (size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes, alignment);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[] (size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes, alignment);
  } catch (...) {
    return nullptr;
  }
}
void operator delete (void* p) noexcept { allocation_tracking::release (p); }
void operator delete[] (void* p) noexcept { allocation_tracking::release (p); }
void operator delete (void* p, size_t) noexcept { allocation_tracking::release (p); }
void operator delete[] (void* p, size_t) noexcept { allocation_tracking::release (p); }
void operator delete (void* p, std::align_val_t) noexcept { allocation_tracking::release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { allocation_tracking::release (p); }
void operator delete (void* p, size_t, std::align_val_t) noexcept {
  allocation_tracking::release (p);
}
void operator delete[] (void* p, size_t, std::align_val_t) noexcept {
  allocation_tracking::release (p);
}
void operator delete (void* p, const std::nothrow_t&) noexcept {
  allocation_tracking::release (p);
}
void operator delete[] (void* p, const std::nothrow_t&) noexcept {
  allocation_tracking::release (p);
}
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  allocation_tracking::release (p);
}
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  allocation_tracking::release (p);
}

#else
# include <cstddef>
namespace allocation_tracking {
  inline constexpr size_t live = 0, peak = 0, count = 0;
  inline void reset_peak () {}
}
#endif
