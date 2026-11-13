#include "check.hh"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <posets/downsets/vector_backed.hh>
#include <posets/vectors.hh>

int main () {
  using vector = posets::vectors::vector_backed<std::int8_t>;
  using downset = posets::downsets::vector_backed<vector>;

  const std::array<std::int8_t, 2> a_data {1, 0};
  const std::array<std::int8_t, 2> b_data {0, 1};
  vector a {std::span<const std::int8_t> (a_data)};
  vector b {std::span<const std::int8_t> (b_data)};

  std::vector<vector> antichain;
  antichain.emplace_back (std::span<const std::int8_t> (a_data));
  antichain.emplace_back (std::span<const std::int8_t> (b_data));
  auto trusted = downset::from_antichain_unchecked (std::move (antichain));

  CHECK (trusted.size () == 2);
  CHECK (trusted.contains (a));
  CHECK (trusted.contains (b));

  std::vector<vector> reduced_input;
  reduced_input.emplace_back (std::span<const std::int8_t> (a_data));
  reduced_input.emplace_back (std::span<const std::int8_t> (b_data));
  downset reduced {std::move (reduced_input)};
  for (const auto& maximal : trusted)
    CHECK (reduced.contains (maximal));
  for (const auto& maximal : reduced)
    CHECK (trusted.contains (maximal));
}
