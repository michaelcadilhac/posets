#pragma once

namespace posets::vectors {
  template <typename T>
  concept HasData = requires (T t) {
    { t[0][0] };
    { t.data () } -> std::same_as<typename T::value_type*>;
    { t.size () } -> std::same_as<size_t>;
  };

  template <typename T>
  concept IsDataSimd = HasData<T> and requires (T t) {
    { new std::experimental::simd (t[0]) } -> std::same_as<typename T::value_type*>;
  };

  /// Optional cached sum for generic vectors.
  template <bool HasSum>
  struct sum_member {
      int sum = 0;
  };

  template <>
  struct sum_member<false> {};

  /// C++20 new/delete respect Data's alignment, including embedded SIMD arrays.
  template <typename Data>
  struct basic_malloc {
      static Data* construct () { return new Data (); }
      static void destroy (Data* d) { delete d; }
  };

  template <bool EmbedsData, typename Data>
  struct malloc_member {
      static inline basic_malloc<Data> malloc;
  };

  template <typename Data>
  struct malloc_member<true, Data> {};
}
