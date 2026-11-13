#include "check.hh"

#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include <posets/utils/comptrie.hh>
#include <posets/utils/reduce.hh>
#include <posets/vectors.hh>

using value_type = std::int8_t;
using V = posets::vectors::simd_vector_backed_sum<value_type>;

std::vector<V> make_vectors (const std::vector<std::vector<value_type>>& raw) {
  std::vector<V> result;
  result.reserve (raw.size ());
  for (const auto& value : raw)
    result.emplace_back (std::span<const value_type> (value));
  return result;
}

int main () {
  constexpr size_t dim = 128;
  std::mt19937 rng (9);
  std::bernoulli_distribution bit (0.5);
  std::vector<std::vector<value_type>> raw (64, std::vector<value_type> (dim));
  for (auto& value : raw)
    for (auto& item : value)
      item = bit (rng) ? 0 : -1;

  auto maxima = posets::utils::reduce_to_maxima (make_vectors (raw));
  std::vector<V> reference;
  reference.reserve (maxima.size ());
  for (const auto& value : maxima)
    reference.push_back (value.copy ());

  posets::utils::comptrie<V> trie (std::move (maxima));
  CHECK (trie.get_all ().size () == reference.size ());
  CHECK (not trie.has_single_child_chain ());
  CHECK (trie.node_count () * 4 < reference.size () * dim);

  for (size_t round = 0; round < 1000; ++round) {
    std::vector<value_type> coords (dim);
    for (auto& item : coords)
      item = bit (rng) ? 0 : -1;
    V query {std::span<const value_type> (coords)};
    bool dominated = false;
    bool strictly_dominated = false;
    for (const auto& value : reference) {
      auto po = query.partial_order (value);
      dominated or_eq po.leq ();
      strictly_dominated or_eq po.leq () and not po.geq ();
    }
    CHECK (trie.dominates (query) == dominated);
    CHECK (trie.dominates (query, true) == strictly_dominated);
  }
}
