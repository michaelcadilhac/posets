#pragma once

#include <type_traits>

#include <functional>
#include <span>

namespace posets {
  template <typename T>
  concept Vector =
      not std::is_default_constructible_v<T> and std::is_constructible_v<T, unsigned int> and
      not std::is_copy_constructible_v<T> and not std::is_copy_assignable_v<T> and
      std::is_move_constructible_v<T> and std::is_move_assignable_v<T> and
      std::is_constructible_v<T, std::span<const typename T::value_type>> and
      requires (T t1, T t2) { t1 == t2&& t1 != t2; } and
      requires (const T& t1, const T& t2, std::span<typename T::value_type> s, std::ostream& os) {
        { t1.copy () } -> std::same_as<T>;
        { t1.partial_order (t2).geq () } -> std::same_as<bool>;
        { t1.partial_order (t2).leq () } -> std::same_as<bool>;
        { t1.meet (t2) } -> std::same_as<T>;
        { t1.to_vector (s) };
        { t1.print (os) } -> std::same_as<std::ostream&>;
      };

  template <typename T, typename V = typename T::value_type>
  concept Downset = std::ranges::range<T> and
    not std::is_default_constructible_v<T> and
    std::is_constructible_v<T, V&&> and
    std::is_constructible_v<T, std::vector<V>&&> and
    not std::is_copy_constructible_v<T> and
    not std::is_copy_assignable_v<T> and
    std::is_move_constructible_v<T> and
    std::is_move_assignable_v<T> and
    requires (T set, T set2, V vec, std::function<V (const V&)> f) {
      { set.apply (f) } -> std::same_as<T>;
      { set.contains (vec) } -> std::same_as<bool>;
      set.union_with (std::move (set2));
      set.intersect_with (std::move (set2));
  };
}
