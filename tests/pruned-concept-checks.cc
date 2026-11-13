#define POSETS_CONFIGURED 1
#define POSETS_COMPILE_ALL_COMPONENTS 0
#define POSETS_ENABLE_DOWNSET_VECTOR_BACKED 1
#define POSETS_ENABLE_VECTOR_X_AND_BITSET 1

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace {
  using vec = posets::vectors::simd_vector_backed<int>;
  using bool_vec = posets::vectors::x_and_bitset<vec, 1>;
  using downset = posets::downsets::vector_backed<bool_vec>;

  static_assert (posets::Vector<vec>);
  static_assert (posets::Vector<bool_vec>);
  static_assert (posets::Downset<downset>);
}

int main () { return 0; }
