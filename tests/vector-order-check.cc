#include "matrix-check.hh"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include <posets/utils/sharingtrie.hh>
#include <posets/vectors.hh>

template <class V>
void check () {
  using T = V::value_type;
  constexpr auto low = std::numeric_limits<T>::min ();
  constexpr auto high = std::numeric_limits<T>::max ();
  for (size_t dimension : {0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u})
    for (T first : {low, T (-1), T (0), high})
      for (T second : {low, T (-1), T (0), high}) {
        std::vector<T> a (dimension, first), b (dimension, second);
        for (int mixed = 0; mixed < 2; ++mixed) {
          if (mixed and not a.empty ())
            a.back () = second;
          const V lhs {std::span<const T> (a)}, rhs {std::span<const T> (b)};
          const bool leq = std::equal (a.begin (), a.end (), b.begin (), std::less_equal<T> {});
          const bool geq = std::equal (a.begin (), a.end (), b.begin (), std::greater_equal<T> {});
          matrix_check::require ((lhs < rhs) == std::lexicographical_compare (
                                                    a.begin (), a.end (), b.begin (), b.end ()),
                                 "signed lexicographic order including empty comparison masks");
          auto forward = lhs.partial_order (rhs);
          auto backward = lhs.partial_order (rhs);
          matrix_check::require (forward.leq () == leq and forward.geq () == geq and
                                     backward.geq () == geq and backward.leq () == leq and
                                     forward.leq () == leq and backward.geq () == geq,
                                 "full-range signed lazy partial order");
        }
      }
}

template <class T>
void check_type () {
  check<posets::vectors::vector_backed<T>> ();
  check<posets::vectors::array_backed<T, 65>> ();
  check<posets::vectors::array_ptr_backed<T, 65>> ();
  check<posets::vectors::simd_vector_backed<T>> ();
  check<posets::vectors::simd_array_backed<T, 65>> ();
  check<posets::vectors::simd_array_ptr_backed<T, 65>> ();
}

struct scalar_block : std::array<int, 8> {
    auto operator<=> (const scalar_block&) const = default;
};

int main () {
  // generic<Data> also accepts scalar blocks without a tuple_size specialization.
  using custom = posets::vectors::generic<std::vector<scalar_block>, false, true>;
  static_assert (posets::Vector<custom>);
  check<custom> ();
  check_type<std::int8_t> ();
  // These full-range sums fit the cached int, including fixed-storage padding.
  check<posets::vectors::vector_backed_sum<std::int8_t>> ();
  check<posets::vectors::array_backed_sum<std::int8_t, 65>> ();
  check<posets::vectors::array_ptr_backed_sum<std::int8_t, 65>> ();
  check<posets::vectors::simd_vector_backed_sum<std::int8_t>> ();
  check<posets::vectors::simd_array_backed_sum<std::int8_t, 65>> ();
  check<posets::vectors::simd_array_ptr_backed_sum<std::int8_t, 65>> ();
  check_type<std::int16_t> ();
  check_type<std::int32_t> ();
  check_type<std::int64_t> ();
  // Equal low 32 bits must not merge distinct 64-bit suffix languages/colors.
  using V = posets::vectors::simd_vector_backed<std::int64_t>;
  const std::array<std::int64_t, 3> a {2, 0, 0}, b {1, 0, std::int64_t {1} << 40};
  std::vector<V> values;
  values.emplace_back (a);
  values.emplace_back (b);
  posets::utils::sharingtrie<V> trie (std::move (values));
  matrix_check::require (trie.dominates (V (b)), "64-bit trie color collision");
  matrix_check::require (not trie.dominates (V (b), true), "64-bit strict trie order");
}
