#pragma once

#include <boost/pool/object_pool.hpp>

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

  /// Conditional member when has_sum is set in @a simd_array_backed_.
  template <bool has_sum>
  struct sum_member {
      int sum = 0;
  };

  template <>
  struct sum_member<false> {};

  /// Conditional member when embeds_data is unset in @a simd_array_backed_.
  template <typename Data>
  struct basic_malloc {
      static Data* construct () { return new Data (); }
      static void destroy (Data* d) { delete (d); }
  };

  template <bool embeds_data, typename Data>
  struct malloc_member {
      // static inline boost::object_pool<Data> malloc {8192};
      static inline basic_malloc<Data> malloc;
  };

  template <typename Data>
  struct malloc_member<true, Data> {};
}
