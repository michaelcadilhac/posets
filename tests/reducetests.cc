#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include <posets/utils/reduce.hh>
#include <posets/vectors.hh>

using value_type = std::int8_t;

size_t posets::vectors::bool_threshold = 4;
size_t posets::vectors::bitset_threshold = 4;

template <typename V>
std::vector<V> make_vectors (const std::vector<std::vector<value_type>>& raw) {
  std::vector<V> result;
  result.reserve (raw.size ());
  for (const auto& value : raw)
    result.emplace_back (std::span<const value_type> (value));
  return result;
}

template <typename V>
void check_reduce (const std::vector<std::vector<value_type>>& raw) {
  std::vector<long> keys;
  auto reduced = posets::utils::reduce_to_maxima (make_vectors<V> (raw), &keys);
  assert (keys.size () == reduced.size ());
  assert (std::ranges::is_sorted (keys, std::greater<> {}));
  for (size_t i = 0; i < reduced.size (); ++i)
    assert (keys[i] == posets::utils::sum_key (reduced[i]));
  std::vector<size_t> expected;
  for (size_t i = 0; i < raw.size (); ++i) {
    V candidate {std::span<const value_type> (raw[i])};
    bool dominated = false;
    bool duplicate_before = false;
    for (size_t j = 0; j < raw.size (); ++j) {
      V other {std::span<const value_type> (raw[j])};
      auto po = candidate.partial_order (other);
      if (po.leq () and not po.geq ())
        dominated = true;
      if (j < i and candidate == other)
        duplicate_before = true;
    }
    if (not dominated and not duplicate_before)
      expected.push_back (i);
  }
  assert (reduced.size () == expected.size ());
  for (size_t i : expected) {
    V value {std::span<const value_type> (raw[i])};
    bool found = false;
    for (const auto& kept : reduced)
      found or_eq kept == value;
    assert (found);
  }
}

template <typename V>
void run_suite () {
  check_reduce<V> ({{-1, -1}, {-1, -1}, {0, 0}, {1, 1}});
  check_reduce<V> ({{2, -2}, {-2, 2}, {0, 0}, {2, -2}});
  check_reduce<V> ({{-3, 1, 0}, {-2, 0, 0}, {-3, 2, -1}, {-1, 1, 1}});

  std::mt19937 rng (42);
  std::uniform_int_distribution<int> coord (-3, 5);
  for (size_t round = 0; round < 50; ++round) {
    std::vector<std::vector<value_type>> raw (40, std::vector<value_type> (8));
    for (auto& value : raw)
      for (auto& item : value)
        item = coord (rng);
    raw.push_back (raw.front ());
    check_reduce<V> (raw);
  }
}

int main () {
  using no_sum = posets::vectors::simd_vector_backed<value_type>;
  using with_sum = posets::vectors::simd_vector_backed_sum<value_type>;
  run_suite<no_sum> ();
  run_suite<with_sum> ();

  posets::vectors::bitset_threshold = 4;
  using composite_no_sum = posets::vectors::x_and_bitset<no_sum, 1>;
  using composite_sum = posets::vectors::x_and_bitset<with_sum, 1>;
  run_suite<composite_no_sum> ();
  run_suite<composite_sum> ();

  // Exercise the structure-assisted path above POSETS_REDUCE_SCAN_THRESHOLD.
  std::vector<std::vector<value_type>> large;
  large.reserve (POSETS_REDUCE_SCAN_THRESHOLD + 64);
  for (size_t i = 0; i < POSETS_REDUCE_SCAN_THRESHOLD + 64; ++i)
    large.push_back ({{static_cast<value_type> (i % 4), static_cast<value_type> ((i / 4) % 4),
                       static_cast<value_type> ((i / 16) % 4)}});
  // The generic oracle in check_reduce is intentionally quadratic and would
  // dwarf the structure-assisted path for this threshold-sized input,
  // especially under Valgrind.  This data contains every point of {0..3}^3
  // repeatedly, so its unique maximum is known exactly.
  std::vector<long> keys;
  auto reduced = posets::utils::reduce_to_maxima (make_vectors<with_sum> (large), &keys);
  constexpr std::array<value_type, 3> expected_data {3, 3, 3};
  with_sum expected {std::span<const value_type> (expected_data)};
  assert (reduced.size () == 1);
  assert (reduced.front () == expected);
  assert (keys == std::vector<long> {9});

  const std::vector<std::vector<value_type>> relabel_raw {
      {1, 0, 0},
      {0, 1, 0},
      {0, 0, 1},
  };
  posets::utils::kdtree<with_sum> default_tree;
  default_tree.relabel_tree (make_vectors<with_sum> (relabel_raw));
  assert (default_tree.size () == relabel_raw.size ());

  posets::utils::kdtree<with_sum> zero_hint_tree (3, 0);
  zero_hint_tree.relabel_tree (make_vectors<with_sum> (relabel_raw));
  assert (zero_hint_tree.size () == relabel_raw.size ());
}
