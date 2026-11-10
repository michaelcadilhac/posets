#pragma once

#include <posets/concept_checks/vectors.hh>
#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/block_sharingtrie_backed.hh>
#include <posets/downsets/comptrie_backed.hh>
#include <posets/downsets/cst_backed.hh>
#include <posets/downsets/filtered_vector_backed.hh>
#include <posets/downsets/full_set.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/set_backed.hh>
#include <posets/downsets/sharingtree_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/downsets/simple_sharingtree_backed.hh>
#include <posets/downsets/skiplist_backed.hh>
#include <posets/downsets/vector_backed.hh>
#include <posets/downsets/vector_backed_bin.hh>
#include <posets/downsets/vector_backed_one_dim_split_intersection_only.hh>
#include <posets/downsets/vector_or_kdtree_backed.hh>

namespace posets::downsets::concept_checks {
  using vector_test_int = posets::vectors::concept_checks::vector_test_int;

  static_assert (posets::Downset<block_sharingtrie_backed<vector_test_int>>);
  static_assert (posets::Downset<full_set<vector_test_int>>);
  static_assert (posets::Downset<bboxtree_backed<vector_test_int>>);
  static_assert (posets::Downset<comptrie_backed<vector_test_int>>);
  static_assert (posets::Downset<filtered_vector_backed<vector_test_int>>);
  static_assert (posets::Downset<kdtree_backed<vector_test_int>>);
  static_assert (posets::Downset<lazy_kdtree_backed<vector_test_int>>);
  static_assert (posets::Downset<rank_bucketed_vector_backed<vector_test_int>>);
  static_assert (posets::Downset<skiplist_backed<vector_test_int>>);
  static_assert (posets::Downset<vector_backed<vector_test_int>>);
  static_assert (posets::Downset<vector_or_kdtree_backed<vector_test_int>>);
  static_assert (posets::Downset<vector_backed_bin<vector_test_int>>);
  static_assert (posets::Downset<vector_backed_one_dim_split_intersection_only<vector_test_int>>);
  static_assert (posets::Downset<set_backed<vector_test_int>>);
  static_assert (posets::Downset<sharingtree_backed<vector_test_int>>);
  static_assert (posets::Downset<simple_sharingtree_backed<vector_test_int>>);
  static_assert (posets::Downset<sharingtrie_backed<vector_test_int>>);
  static_assert (posets::Downset<cst_backed<vector_test_int>>);
}
