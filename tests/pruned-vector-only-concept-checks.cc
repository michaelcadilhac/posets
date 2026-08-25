#define POSETS_CONFIGURED 1
#define POSETS_COMPILE_ALL_COMPONENTS 0
#define POSETS_ENABLE_DOWNSET_VECTOR_BACKED 1
#define POSETS_ENABLE_VECTOR_SIMD_VECTOR_BACKED 1

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace {
  using vec = posets::vectors::simd_vector_backed<int>;
  using downset = posets::downsets::vector_backed<vec>;

  static_assert (posets::Vector<vec>);
  static_assert (posets::Downset<downset>);
  static_assert (posets::vectors::traits<posets::vectors::simd_vector_backed, int>::capacity_for (
                     43) >= 43);
}

int main () { return 0; }
