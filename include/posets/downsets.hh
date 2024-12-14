#pragma once

#include <posets/concepts.hh>

#include <posets/downsets/full_set.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/set_backed.hh>
#include <posets/downsets/vector_backed.hh>
#include <posets/downsets/vector_backed_bin.hh>
#include <posets/downsets/vector_backed_one_dim_split.hh>
#include <posets/downsets/vector_backed_one_dim_split_intersection_only.hh>
#include <posets/downsets/set_backed.hh>
#include <posets/downsets/sharingtree_backed.hh>
#include <posets/downsets/vector_or_kdtree_backed.hh>

#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

namespace posets::downsets {
  static_assert (Downset<full_set<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<kdtree_backed<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<vector_backed<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<vector_or_kdtree_backed<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<vector_backed_bin<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<vector_backed_one_dim_split<posets::vectors::vector_backed<int>>>);
  static_assert (
      Downset<vector_backed_one_dim_split_intersection_only<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<set_backed<posets::vectors::vector_backed<int>>>);
  static_assert (Downset<sharingtree_backed<posets::vectors::vector_backed<int>>>);
}
