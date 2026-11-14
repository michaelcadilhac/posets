#include "check.hh"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/cst_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/sharingtree_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/downsets/simple_sharingtree_backed.hh>
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
    CHECK (empty.size () == 0);
    CHECK (empty.begin () == empty.end ());
    CHECK (empty.get_backing_vector ().empty ());
    CHECK (not empty.contains (query));

    auto mapped = empty.apply ([] (const auto& value) { return value.copy (); });
    CHECK (mapped.size () == 0);

    empty.union_with (downset_type {std::vector<vector_type> {}});
    CHECK (empty.size () == 0);
    empty.intersect_with (downset_type {std::vector<vector_type> {}});
    CHECK (empty.size () == 0);

    empty.union_with (downset_type {vector (1, 2)});
    CHECK (empty.size () == 1);
    CHECK (empty.contains (query));

    empty.union_with (downset_type {std::vector<vector_type> {}});
    CHECK (empty.size () == 1);

    const auto empty_rhs = downset_type {std::vector<vector_type> {}};
    empty.intersect_with (empty_rhs);
    CHECK (empty.size () == 0);
    CHECK (empty.begin () == empty.end ());
    CHECK (not empty.contains (query));

    const auto nonempty_rhs = downset_type {vector (2, 1)};
    empty.intersect_with (nonempty_rhs);
    CHECK (empty.size () == 0);

    {
      auto bottom = downset_type {vector (-1, -1)};
      empty.union_with (std::move (bottom));
    }
    CHECK (empty.size () == 1);
    CHECK (empty.contains (vector (-1, -1)));
    CHECK (not empty.contains (query));
    auto owned = empty.apply ([] (const auto& value) { return value.copy (); });
    empty.intersect_with (empty_rhs);
    CHECK (owned.size () == 1);
    CHECK (owned.contains (vector (-1, -1)));
    CHECK (empty.size () == 0);
    CHECK (not empty.contains (vector (-1, -1)));

    for (const size_t dimension : {1U, 3U}) {
      const auto point = vector_type {std::vector<value_type> (dimension, 1)};
      CHECK (not empty.contains (point));
      empty.union_with (downset_type {point.copy ()});
      CHECK (empty.size () == 1);
      CHECK (empty.contains (point));
      CHECK (empty.begin ()->size () == dimension);
      empty.intersect_with (empty_rhs);
      CHECK (empty.size () == 0);
      CHECK (not empty.contains (point));
    }
  }
}

int main () try {
  auto trie = posets::utils::sharingtrie<vector_type> {std::vector<vector_type> {}};
  CHECK (trie.size () == 0);
  CHECK (trie.get_all ().empty ());
  CHECK (not trie.dominates (vector (0, 0)));

  check_empty_downset<posets::downsets::simple_sharingtree_backed> ();
  check_empty_downset<posets::downsets::sharingtree_backed> ();
  check_empty_downset<posets::downsets::cst_backed> ();
  check_empty_downset<posets::downsets::sharingtrie_backed> ();
  check_empty_downset<posets::downsets::rank_bucketed_vector_backed> ();
  check_empty_downset<posets::downsets::bboxtree_backed> ();
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
