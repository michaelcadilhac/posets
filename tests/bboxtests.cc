#include "check.hh"

#include <cstdint>
#include <span>
#include <vector>

#include <posets/downsets/bboxtree_backed.hh>
#include <posets/utils/bboxtree.hh>
#include <posets/utils/reduce.hh>
#include <posets/vectors.hh>

using V = posets::vectors::simd_vector_backed_sum<std::int8_t>;

V make (std::initializer_list<std::int8_t> value) {
  std::vector<std::int8_t> storage (value);
  return V (std::span<const std::int8_t> (storage));
}

std::vector<V> make_antichain (size_t count) {
  std::vector<V> result;
  result.reserve (count);
  for (size_t i = 0; i < count; ++i)
    result.push_back (
        make ({static_cast<std::int8_t> (i), static_cast<std::int8_t> (count - i - 1)}));
  return result;
}

template <size_t B>
bool brute_dominates (const posets::utils::bboxtree<V, B>& tree, const V& query, bool strict) {
  for (const auto& value : tree) {
    auto order = query.partial_order (value);
    if (order.leq () and (not strict or not order.geq ()))
      return true;
  }
  return false;
}

int main () {
  std::vector<V> values;
  values.push_back (make ({6, 3, 2}));
  values.push_back (make ({5, 5, 4}));
  values.push_back (make ({2, 6, 2}));
  values = posets::utils::reduce_to_maxima (std::move (values));

  posets::utils::bboxtree<V, 2> tree;
  tree.assign_and_build (std::move (values));
  CHECK (tree.size () == 3);
  CHECK (tree.is_antichain ());
  CHECK (not tree.corners_built ());
  CHECK (tree.node_count () == 0);
  auto low = make ({2, 2, 2});
  auto exact = make ({6, 3, 2});
  auto high = make ({7, 7, 7});
  CHECK (tree.dominates (low));
  CHECK (tree.dominates (low, true));
  CHECK (tree.dominates (exact));
  CHECK (not tree.dominates (exact, true));
  CHECK (not tree.dominates (high));
  auto forces_tree = make ({0, 7, 0});
  CHECK (not tree.dominates (forces_tree));
  CHECK (tree.corners_built ());
  CHECK (tree.node_count () > 0);

  std::vector<V> queries;
  queries.push_back (make ({5, 4, 3}));
  queries.push_back (make ({4, 5, 2}));
  queries.push_back (make ({6, 2, 2}));
  queries.push_back (make ({3, 6, 1}));
  queries.push_back (make ({7, 3, 2}));
  for (const auto& query : queries)
    for (const bool strict : {false, true})
      CHECK (tree.dominates (query, strict) == brute_dominates (tree, query, strict));

  auto small_values = make_antichain (8);
  std::vector<posets::utils::vector_sum_t<V>> small_keys;
  small_values = posets::utils::reduce_to_maxima (std::move (small_values), &small_keys);
  posets::utils::bboxtree<V> small_tree;
  small_tree.assign_and_build (std::move (small_values), std::move (small_keys));
  CHECK (small_tree.corners_built ());
  CHECK (small_tree.node_count () == 0);
  auto equal_sum = make ({3, 4});
  auto above_max_sum = make ({8, 0});
  auto below_all = make ({0, 0});
  CHECK (small_tree.dominates (equal_sum));
  CHECK (not small_tree.dominates (equal_sum, true));
  CHECK (not small_tree.dominates (above_max_sum));
  CHECK (small_tree.dominates (below_all));

  using downset = posets::downsets::bboxtree_backed<V>;
  downset applied_source (make_antichain (17));
  CHECK (not applied_source.corners_built ());
  auto applied = applied_source.apply ([] (const V& value) { return value.copy (); });
  CHECK (not applied.corners_built ());
  CHECK (applied.contains (below_all));
  auto forces_applied_tree = make ({-1, 17});
  CHECK (not applied.contains (forces_applied_tree));
  CHECK (applied.corners_built ());

  downset running_union (make_antichain (17));
  downset dominated (make ({0, 0}));
  running_union.union_with (std::move (dominated));
  CHECK (running_union.size () == 17);
  CHECK (running_union.contains (below_all));

  downset steal_target (make ({-1, -1}));
  downset steal_source (make_antichain (17));
  steal_target.union_with (std::move (steal_source));
  CHECK (steal_target.size () == 17);
  CHECK (steal_target.contains (below_all));
}
