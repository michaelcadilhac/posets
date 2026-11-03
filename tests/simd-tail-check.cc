#include <posets/vectors.hh>
#include "matrix-check.hh"

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

int main () {
  using inner = posets::vectors::simd_vector_backed<std::int8_t>;
  using packed = posets::vectors::x_and_bitset<inner, 3>;
  for (size_t numeric : {0u, 1u, 15u, 16u, 17u, 31u, 63u, 64u, 65u, 128u}) {
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
      std::vector<std::int8_t> unpacked (a.size ());
      value.to_vector (unpacked);
      matrix_check::require (matrix_check::logical (unpacked.begin (), unpacked.end ()) == a,
                             "packed Boolean tail roundtrip");
    }
  }
}
