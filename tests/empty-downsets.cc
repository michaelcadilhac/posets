#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/utils/sharingtrie.hh>
#include <posets/vectors.hh>

namespace {
  using value_type = std::int8_t;
  using vector_type = posets::vectors::vector_backed<value_type>;

  vector_type vector (value_type first, value_type second) {
    const std::array<value_type, 2> values {first, second};
    return vector_type {std::span<const value_type> {values}};
  }

  template <template <typename> typename Downset>
  void check_empty_downset () {
    using downset_type = Downset<vector_type>;

    auto empty = downset_type {std::vector<vector_type> {}};
    const auto query = vector (0, 0);
    assert (empty.size () == 0);
    assert (empty.begin () == empty.end ());
    assert (not empty.contains (query));

    auto mapped = empty.apply ([] (const auto& value) { return value.copy (); });
    assert (mapped.size () == 0);

    empty.union_with (downset_type {std::vector<vector_type> {}});
    assert (empty.size () == 0);
    empty.intersect_with (downset_type {std::vector<vector_type> {}});
    assert (empty.size () == 0);

    empty.union_with (downset_type {vector (1, 2)});
    assert (empty.size () == 1);
    assert (empty.contains (query));

    empty.union_with (downset_type {std::vector<vector_type> {}});
    assert (empty.size () == 1);

    const auto empty_rhs = downset_type {std::vector<vector_type> {}};
    empty.intersect_with (empty_rhs);
    assert (empty.size () == 0);

    const auto nonempty_rhs = downset_type {vector (2, 1)};
    empty.intersect_with (nonempty_rhs);
    assert (empty.size () == 0);
  }
}

int main () {
  auto trie = posets::utils::sharingtrie<vector_type> {std::vector<vector_type> {}};
  assert (trie.size () == 0);
  assert (trie.get_all ().empty ());
  assert (not trie.dominates (vector (0, 0)));

  check_empty_downset<posets::downsets::sharingtrie_backed> ();
  check_empty_downset<posets::downsets::rank_bucketed_vector_backed> ();
}
