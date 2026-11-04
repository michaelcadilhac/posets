#include "matrix-check.hh"

#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

template <class Inner, class Packed>
void check () {
  using packed = Packed;
  for (size_t numeric : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 128u}) {
    posets::vectors::bool_threshold = posets::vectors::bitset_threshold = numeric;
    for (size_t tail : {0u, 1u, 63u, 64u, 65u, 129u}) {
      matrix_check::logical a (numeric + tail), b (numeric + tail);
      for (size_t i = 0; i < numeric; ++i) {
        a[i] = int (i % 4) - 1;
        b[i] = int ((i + 1) % 4) - 1;
      }
      for (size_t i = numeric; i < a.size (); ++i) {
        a[i] = int (i % 2) - 1;
        b[i] = int ((i + 1) % 2) - 1;
      }
      matrix_check::pair<packed> (a, b);
      auto value = matrix_check::vector<packed> (a);
      std::vector<typename Inner::value_type> unpacked (a.size ());
      value.to_vector (unpacked);
      matrix_check::require (matrix_check::logical (unpacked.begin (), unpacked.end ()) == a,
                             "packed Boolean tail roundtrip");
    }
  }
}

template <class T>
void check_type () {
  using inner = posets::vectors::simd_vector_backed<T>;
  check<inner, posets::vectors::x_and_bitset<inner, 3>> ();
  check<inner, posets::vectors::x_and_boolvec<inner>> ();
  check<inner, posets::vectors::x_and_wordvec<inner>> ();
  // A threshold beyond the vector size must not create an oversized span.
  posets::vectors::bool_threshold = posets::vectors::bitset_threshold = 200;
  const matrix_check::logical short_row (7, -1);
  matrix_check::pair<posets::vectors::x_and_bitset<inner, 3>> (short_row, short_row);
  matrix_check::pair<posets::vectors::x_and_boolvec<inner>> (short_row, short_row);
  matrix_check::pair<posets::vectors::x_and_wordvec<inner>> (short_row, short_row);
  posets::vectors::bool_threshold = 3;
  posets::vectors::x_and_boolvec<inner> sized (70);
  for (size_t i = 3; i < sized.size (); ++i)
    matrix_check::require (sized[i] == -1, "size-constructed Boolean tail");
}

int main () {
  check_type<std::int8_t> ();
  check_type<std::int16_t> ();
  check_type<std::int32_t> ();
  check_type<std::int64_t> ();
}
