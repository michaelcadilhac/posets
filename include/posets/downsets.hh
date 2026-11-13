#pragma once

#include <posets/concepts.hh>
#include <posets/config.hh>
#include <posets/vectors.hh>

#if POSETS_ENABLE_DOWNSET_BLOCK_SHARINGTRIE_BACKED
# include <posets/downsets/block_sharingtrie_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_BBOXTREE_BACKED
# include <posets/downsets/bboxtree_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_COMPTRIE_BACKED
# include <posets/downsets/comptrie_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_CST_BACKED
# include <posets/downsets/cst_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_FILTERED_VECTOR_BACKED
# include <posets/downsets/filtered_vector_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_LAZY_KDTREE_BACKED
# include <posets/downsets/lazy_kdtree_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_KDTREE_BACKED
# include <posets/downsets/kdtree_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_SET_BACKED
# include <posets/downsets/set_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_SHARINGTREE_BACKED
# include <posets/downsets/sharingtree_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_SHARINGTRIE_BACKED
# include <posets/downsets/sharingtrie_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_SIMPLE_SHARINGTREE_BACKED
# include <posets/downsets/simple_sharingtree_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_SKIPLIST_BACKED
# include <posets/downsets/skiplist_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_RANK_BUCKETED_VECTOR_BACKED
# include <posets/downsets/rank_bucketed_vector_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_VECTOR_BACKED
# include <posets/downsets/vector_backed.hh>
#endif
#if POSETS_ENABLE_DOWNSET_VECTOR_BACKED_BIN
# include <posets/downsets/vector_backed_bin.hh>
#endif
#if POSETS_ENABLE_DOWNSET_VECTOR_BACKED_ONE_DIM_SPLIT_INTERSECTION_ONLY
# include <posets/downsets/vector_backed_one_dim_split_intersection_only.hh>
#endif
#if POSETS_ENABLE_DOWNSET_VECTOR_OR_KDTREE_BACKED
# include <posets/downsets/vector_or_kdtree_backed.hh>
#endif
