#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include <posets/vectors.hh>

namespace {
  using value_type = std::int8_t;

  template <typename V>
  void check_move_assignment () {
    const std::array<value_type, 3> values {-2, 1, 3};
    const V expected {std::span<const value_type> {values}};
    V destination (4);
    {
      V source {std::span<const value_type> {values}};
      destination = std::move (source);
      source = expected.copy ();
      assert (source == expected);
    }
    V& alias = destination;
    destination = std::move (alias);
    assert (destination.size () == values.size ());
    for (size_t i = 0; i < values.size (); ++i)
      assert (destination[i] == values[i]);
    assert (destination == expected);
    if constexpr (requires { destination.cached_sum (); })
      assert (destination.cached_sum () == 2);

    V moved {std::move (destination)};
    assert (moved == expected);
  }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main () {
  using namespace posets::vectors;

  // Moving resizable storage must transfer its allocation, even into a nonempty vector.
  const std::array<value_type, 3> values {-2, 1, 3};
  vector_backed<value_type> destination (4);
  {
    vector_backed<value_type> source {std::span<const value_type> {values}};
    const auto* storage = &*source.begin ();
    destination = std::move (source);
    assert (&*destination.begin () == storage);
  }
  for (size_t i = 0; i < values.size (); ++i)
    assert (destination[i] == values[i]);

  check_move_assignment<vector_backed<value_type>> ();
  check_move_assignment<vector_backed_sum<value_type>> ();
  check_move_assignment<array_backed<value_type, 8>> ();
  check_move_assignment<array_backed_sum<value_type, 8>> ();
  check_move_assignment<array_ptr_backed<value_type, 8>> ();
  check_move_assignment<array_ptr_backed_sum<value_type, 8>> ();
  check_move_assignment<simd_vector_backed<value_type>> ();
  check_move_assignment<simd_vector_backed_sum<value_type>> ();
  check_move_assignment<simd_array_backed<value_type, 8>> ();
  check_move_assignment<simd_array_backed_sum<value_type, 8>> ();
  check_move_assignment<simd_array_ptr_backed<value_type, 8>> ();
  check_move_assignment<simd_array_ptr_backed_sum<value_type, 8>> ();
}
