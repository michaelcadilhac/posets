// Derived from the preserved nine-type oracle; runtime validation is pending.
// Original SHA256: 2b8c30002098249a8ba056425ff34a75dca0af43cdb0537ac60fba08dea15931
#include "configuration.hh"
#include "matrix-check.hh"
#include <posets/downsets/block_sharingtrie_backed.hh>
#include <posets/downsets/comptrie_backed.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/downsets/sharingtree_backed.hh>
#include <posets/downsets/simple_sharingtree_backed.hh>
#include <posets/vectors.hh>
#include <posets/vectors/X_and_bitset.hh>
#include <iostream>

// Four compiled modes cover the 14 frozen candidate configurations.
// Keep every layout-control setting identical to its individual solver arm.
#ifndef ACACIA_BOOLEAN_ORACLE_VARIANT
# define ACACIA_BOOLEAN_ORACLE_VARIANT 0
#endif
#ifdef NDEBUG
# error "This correctness oracle requires assertions enabled"
#endif
static_assert (ACACIA_BOOLEAN_ORACLE_VARIANT >= 0 and ACACIA_BOOLEAN_ORACLE_VARIANT <= 3);
static_assert (POSETS_KDTREE_NARROW_INDICES == (ACACIA_BOOLEAN_ORACLE_VARIANT == 1));
static_assert (POSETS_SHARINGTRIE_COMPACT_NODES == (ACACIA_BOOLEAN_ORACLE_VARIANT == 2));
static_assert (POSETS_COMPTRIE_SIMD_PATHS == (ACACIA_BOOLEAN_ORACLE_VARIANT == 3));
static_assert (POSETS_SIMD_WIDTH == POSETS_SIMD_WIDTH_COMPATIBLE);
static_assert (posets::utils::simd_traits<signed char>::simd_size == 16);

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

using namespace matrix_check;
template <class V, template <class> class D>
void check_all_boolean () {
  for (const size_t dimension : {1U, 17U, 33U, 65U}) {
    region left, right;
    for (size_t offset = 0; offset < 3; ++offset) {
      logical a (dimension, -1), b (dimension, -1);
      for (size_t i = 0; i < dimension; ++i) {
        a[i] = (i + offset) % 3 == 0 ? 0 : -1;
        b[i] = (i + offset) % 2 == 0 ? 0 : -1;
      }
      left.push_back (std::move (a)); right.push_back (std::move (b));
    }
    auto probes = left; probes.insert (probes.end (), right.begin (), right.end ());
    probes.emplace_back (dimension, -1); probes.emplace_back (dimension, 0);
    auto actual = build<D<V>> (left);
    check (actual, left, probes);
    auto copied = actual.apply ([] (const V& value) { return value.copy (); });
    actual.union_with (build<D<V>> (right));
    auto united = left; united.insert (united.end (), right.begin (), right.end ());
    check (actual, united, probes);
    copied.intersect_with (build<D<V>> (right));
    region met;
    for (const auto& a : left) for (const auto& b : right) {
      logical value (dimension);
      for (size_t i = 0; i < dimension; ++i) value[i] = std::min (a[i], b[i]);
      met.push_back (std::move (value));
    }
    check (copied, met, probes);
    require (actual.begin ()->size () == dimension, "all-Boolean V lost total dimension");
  }
}
// These three aliases are identical to src/solver/solve_game_vector.cc.
namespace posets::downsets {
  template <class V>
  using block_sharingtrie_b16 = block_sharingtrie_backed<V, 16>;
  template <class V>
  using block_sharingtrie_b8_uncached = block_sharingtrie_backed<V, 8, true, false>;
  template <class V>
  using lazy_kdtree_uncascaded = lazy_kdtree_backed<V, false>;
}
// Unary adapters preserve the default block8 and cascaded-lazy parameters.
template <class V> using block8 = posets::downsets::block_sharingtrie_backed<V>;
template <class V> using lazy_fc = posets::downsets::lazy_kdtree_backed<V>;

template <class V, template <class> class D>
void check_variant (const char* label, const char* flavor) {
  require (posets::vectors::bool_threshold == 0 and posets::vectors::bitset_threshold == 0,
           "oracle numeric prefix changed");
  check_all_boolean<V, D> ();
  std::cout << "variant=" << label << " vector=" << flavor
            << " numeric_prefix=0 dimensions=1,17,33,65 PASS\n";
}
template <class V>
void run (const char* flavor) {
#if ACACIA_BOOLEAN_ORACLE_VARIANT == 0
  check_variant<V, posets::downsets::rank_bucketed_vector_backed> ("candidate-default", flavor);
  check_variant<V, posets::downsets::kdtree_backed> ("kd", flavor);
  check_variant<V, posets::downsets::sharingtrie_backed> ("trie", flavor);
  check_variant<V, posets::downsets::comptrie_backed> ("comp", flavor);
  check_variant<V, block8> ("block8", flavor);
  check_variant<V, posets::downsets::block_sharingtrie_b16> ("block16", flavor);
  check_variant<V, posets::downsets::block_sharingtrie_b8_uncached> ("block8-uncached", flavor);
  check_variant<V, lazy_fc> ("lazy-fc", flavor);
  check_variant<V, posets::downsets::lazy_kdtree_uncascaded> ("lazy-search", flavor);
  check_variant<V, posets::downsets::sharingtree_backed> ("sharingtree", flavor);
  check_variant<V, posets::downsets::simple_sharingtree_backed> ("simple-sharingtree", flavor);
#elif ACACIA_BOOLEAN_ORACLE_VARIANT == 1
  check_variant<V, posets::downsets::kdtree_backed> ("kd-narrow", flavor);
#elif ACACIA_BOOLEAN_ORACLE_VARIANT == 2
  check_variant<V, posets::downsets::sharingtrie_backed> ("trie-compact", flavor);
#elif ACACIA_BOOLEAN_ORACLE_VARIANT == 3
  check_variant<V, posets::downsets::comptrie_backed> ("comp-simd", flavor);
#endif
}
int main () try {
  using base_vector = posets::vectors::simd_vector_backed<signed char>;
  run<base_vector> ("simd_vector_backed");
  run<posets::vectors::x_and_bitset<base_vector, 2>> ("x_and_bitset");
  std::cout << "All-Boolean frozen candidate oracle mode=" << ACACIA_BOOLEAN_ORACLE_VARIANT
            << " PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
