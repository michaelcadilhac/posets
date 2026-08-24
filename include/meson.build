posets_dep = declare_dependency(include_directories: ['.', boost_inc])

header_files = [
  'posets/concepts.hh',
  'posets/config.hh',
  'posets/concept_checks/all.hh',
  'posets/concept_checks/downsets.hh',
  'posets/concept_checks/vectors.hh',
  'posets/downsets/cst_backed.hh',
  'posets/downsets/bboxtree_backed.hh',
  'posets/downsets/comptrie_backed.hh',
  'posets/downsets/filtered_vector_backed.hh',
  'posets/downsets/full_set.hh',
  'posets/downsets/kdtree_backed.hh',
  'posets/downsets/rank_bucketed_vector_backed.hh',
  'posets/downsets/set_backed.hh',
  'posets/downsets/sharingtree_backed.hh',
  'posets/downsets/sharingtrie_backed.hh',
  'posets/downsets/simple_sharingtree_backed.hh',
  'posets/downsets/skiplist_backed.hh',
  'posets/downsets/vector_backed.hh',
  'posets/downsets/vector_backed_bin.hh',
  'posets/downsets/vector_backed_one_dim_split_intersection_only.hh',
  'posets/downsets/vector_or_kdtree_backed.hh',
  'posets/downsets.hh',
  'posets/utils/cst.hh',
  'posets/utils/bboxtree.hh',
  'posets/utils/comptrie.hh',
  'posets/utils/kdtree.hh',
  'posets/utils/skiplist.hh',
  'posets/utils/sharingforest.hh',
  'posets/utils/sharingtrie.hh',
  'posets/utils/ref_ptr_cmp.hh',
  'posets/utils/reduce.hh',
  'posets/utils/simd_traits.hh',
  'posets/utils/vector_mm.hh',
  'posets/vectors/generic.hh',
  'posets/vectors/generic_partial_order.hh',
  'posets/vectors/generic_helpers.hh',
  'posets/vectors/traits.hh',
  'posets/vectors/X_and_bitset.hh',
  'posets/vectors/X_and_boolvec.hh',
  'posets/vectors/X_and_wordvec.hh',
  'posets/vectors.hh'
]

if not meson.is_subproject()
  install_headers(header_files, preserve_path: true)
endif
