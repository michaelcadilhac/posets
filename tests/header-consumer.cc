#include <array>
#include <cstdint>
#include <span>

#include <posets/concept_checks/all.hh>
#include <posets/downsets.hh>
#include <posets/vectors.hh>

int main () {
  using V = posets::vectors::simd_vector_backed_sum<std::int8_t>;
  const std::array<std::int8_t, 2> coordinates {1, 2};
  V value {std::span<const std::int8_t> (coordinates)};
  posets::downsets::rank_bucketed_vector_backed<V> set (value.copy ());
  if (set.size () != 1 or not set.contains (value))
    return 1;
  auto copy = set.apply ([] (const V& v) { return v.copy (); });
  set.intersect_with (copy);
  set.union_with (std::move (copy));
  return set.size () != 1 or not set.contains (value);
}
