#include <posets/utils/simd_traits.hh>
#include <cstdint>
#include <experimental/simd>

using value = std::int8_t;
static_assert (posets::utils::simd_traits<value>::simd_size ==
               std::experimental::simd_size_v<value, std::experimental::simd_abi::native<value>>);
int main () {}
