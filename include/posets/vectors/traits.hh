#pragma once

namespace posets::vectors {
  // This is the position at and after which all state counters are boolean.
  // Set to the vector size to disable this.
  extern size_t bool_threshold;

  // The boolean threshold may lay at an inconvenient position.  For instance,
  // it may be at 7, in which case a vector type that implements a split between
  // boolean and nonboolean may want to use 8 dimensions as nonbools, and the
  // rest as bool.  This is this threshold:
  extern size_t bitset_threshold;

  // Vs implementing bin() should satisfy:
  //       if u.bin () < v.bin (), then u can't dominate v.
  // or equivalently:
  //       if u dominates v, then u.bin () >= v.bin ()
  template <class T, class = void>
  struct has_bin : std::false_type {};

  template <class T>
  struct has_bin<T, std::void_t<decltype (std::declval<T> ().bin ())>> : std::true_type {};

  /// First argument of T is a type, the rest are literals.
  template <template <typename, auto...> typename T, typename Elt>
  struct traits;
}
