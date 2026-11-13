#include <string_view>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/skiplist_backed.hh>
#include <posets/downsets/vector_backed_bin.hh>
#include <posets/utils/reduce.hh>
#include <posets/vectors.hh>

size_t posets::vectors::bool_threshold = 2;
size_t posets::vectors::bitset_threshold = 2;

namespace {
  void require (bool ok, const char* message) {
    if (not ok) {
      std::cerr << message << '\n';
      std::exit (1);
    }
  }

  template <class V, class R>
  [[nodiscard]] V vector (const R& raw) {
    return V (std::span<const typename V::value_type> (raw));
  }

  template <class V>
  void bins (bool regression) {
    using T = V::value_type;
    using point = std::array<T, 2>;
    using set = posets::downsets::vector_backed_bin<V>;
    if (regression) {
      set zero (vector<V> (point {0, 0}));
      require (zero.contains (vector<V> (point {-1, -1})), "#48: (-1,-1) below (0,0)");
    }
    std::vector<point> points;
    for (int x = -2; x <= 2; ++x)
      for (int y = -2; y <= 2; ++y)
        points.push_back ({T (x), T (y)});
    const auto leq = [] (const point& a, const point& b) { return a[0] <= b[0] and a[1] <= b[1]; };
    for (const auto& a : points)
      for (const auto& b : points) {
        set subject (vector<V> (a));
        const bool accepted = subject.insert (vector<V> (b));
        require (accepted == not leq (b, a), "signed bin insertion vs scalar oracle");
        set intersection (vector<V> (a));
        intersection.intersect_with (set (vector<V> (b)));
        set united (vector<V> (a));
        united.union_with (set (vector<V> (b)));
        for (const auto& q : points) {
          const bool in_a = leq (q, a), in_b = leq (q, b);
          require (subject.contains (vector<V> (q)) == (in_a or in_b),
                   "signed bin containment vs scalar oracle");
          require (united.contains (vector<V> (q)) == (in_a or in_b),
                   "signed bin union vs scalar oracle");
          require (intersection.contains (vector<V> (q)) == (in_a and in_b),
                   "signed bin intersection vs scalar oracle");
        }
      }
  }

  void narrow_width () {
    using V = posets::vectors::vector_backed_sum<int8_t>;
    const size_t dimension = (1U << 24) + 1;
    const std::vector<int8_t> raw (dimension, -128);
    auto v = vector<V> (raw);
    const auto expected = -__int128_t (dimension) * 128;
    require (v.cached_sum () == expected, "wide signed-char cached sum");
    require (v.copy ().cached_sum () == expected, "wide signed-char copy");
    v.join_with (v);
    require (v.cached_sum () == expected, "wide signed-char in-place sum");
  }

  // Run the production bin conversion with a simulated 32-bit size_t. The
  // fallback reproduces the old conversion when checking origin/main headers.
  template <class V>
  uint32_t bin32 (const V& v) {
    if constexpr (requires { v.template bin<uint32_t> (); })
      return v.template bin<uint32_t> ();
    else
      return static_cast<uint32_t> (v.bin ());
  }

  template <class V>
  void narrowing () {
    using T = V::value_type;
    const auto make = [] (T coordinate) { return vector<V> (std::array<T, 1> {coordinate}); };
    const T boundary = T {1} << 32;
    auto stored = make (boundary), query = make (boundary - 1);
    require (query.partial_order (stored).leq (), "narrow bin fixture orientation");
    require (bin32 (query) <= bin32 (stored), "32-bit bin narrowing must be monotone");
    require (bin32 (stored) == UINT32_MAX, "32-bit bins saturate rather than truncate");
    require (bin32 (make (boundary - 2)) == UINT32_MAX - 1, "representable bin unchanged");
    require (bin32 (make (std::numeric_limits<T>::max ())) == UINT32_MAX,
             "extreme positive bin saturates");
    require (bin32 (make (std::numeric_limits<T>::min ())) == 0, "negative bin clamps");
  }

  template <class F>
  void length_error (F&& operation) {
    try {
      operation ();
    } catch (const std::length_error&) {
      return;
    }
    require (false, "unsupported dimension or bin count must throw length_error");
  }

  // A custom Vector with a logical dimension and no payload. Its exact cached
  // sum is zero. Rejected dimensions must never reach coordinate access.
  template <class T>
  class dimension_probe : public posets::vectors::vector_backed<T> {
      using base = posets::vectors::vector_backed<T>;
      size_t dimension;

    public:
      explicit dimension_probe (size_t k) : base (0U), dimension {k} {}
      explicit dimension_probe (std::span<const T> v) : dimension_probe (v.size ()) {}
      dimension_probe (dimension_probe&&) = default;
      dimension_probe& operator= (dimension_probe&&) = default;
      size_t size () const { return dimension; }
      int64_t cached_sum () const { return 0; }
      T operator[] (size_t) const {
        require (false, "dimension guard must precede coordinate access");
        return 0;
      }
      dimension_probe copy () const { return dimension_probe (dimension); }
      dimension_probe meet (const dimension_probe&) const { return copy (); }
      dimension_probe join (const dimension_probe&) const { return copy (); }
  };

  template <class T>
  void dimension_guards () {
    using namespace posets::vectors;
    using V = dimension_probe<T>;
    static_assert (posets::Vector<V>);
    constexpr int exponent = 63 - std::numeric_limits<T>::digits;
    if constexpr (std::numeric_limits<size_t>::digits > exponent) {
      const auto bound = size_t {1} << exponent;
      require (posets::utils::sum_key (V (bound)) == 0, "exact dimension bound accepted");
      const auto unsupported = bound + 1;
      length_error ([&] { vector_backed_sum<T> value (unsupported); });
      length_error ([&] { simd_vector_backed_sum<T> value (unsupported); });
      length_error ([&] { array_backed_sum<T, 8> value (unsupported); });
      length_error ([&] { array_ptr_backed_sum<T, 8> value (unsupported); });
      length_error ([&] { simd_array_backed_sum<T, 8> value (unsupported); });
      length_error ([&] { simd_array_ptr_backed_sum<T, 8> value (unsupported); });
      // Their small cached prefixes fit; the full composite must be checked.
      length_error ([&] { x_and_boolvec<vector_backed_sum<T>> value (unsupported); });
      length_error ([&] { x_and_wordvec<vector_backed_sum<T>> value (unsupported); });
      length_error ([&] { x_and_bitset<vector_backed_sum<T>, 1> value (unsupported); });
      length_error ([&] { (void) posets::utils::sum_key (V (unsupported)); });
      length_error (
          [&] { posets::downsets::rank_bucketed_vector_backed<V> value (V {unsupported}); });
      length_error ([&] { posets::downsets::skiplist_backed<V> value (V {unsupported}); });
      length_error ([&] { posets::downsets::bboxtree_backed<V> value (V {unsupported}); });
      length_error ([&] {
        std::vector<V> values;
        values.emplace_back (unsupported);
        (void) posets::utils::reduce_to_maxima (std::move (values));
      });
      length_error ([&] {
        posets::utils::bboxtree<V> tree;
        std::vector<V> values;
        values.emplace_back (unsupported);
        std::vector<decltype (posets::utils::sum_key (values.front ()))> keys {0};
        tree.assign_and_build (std::move (values), std::move (keys));
      });
    }
  }

  void bin_capacity () {
    using V = posets::vectors::vector_backed<int64_t>;
    const auto high = vector<V> (std::array<int64_t, 1> {INT64_MAX});
    posets::downsets::vector_backed_bin<V> subject (vector<V> (std::array<int64_t, 1> {0}));
    length_error ([&] { subject.insert (high.copy ()); });
    require (subject.size () == 1 and subject.contains (vector<V> (std::array<int64_t, 1> {0})),
             "unrepresentable bin count must preserve existing generators");
  }

  template <class V>
  void sums (size_t dimension) {
    using T = V::value_type;
    using wide = __int128_t;  // Independent expected arithmetic, including pre-fix builds.
    const T lo = std::numeric_limits<T>::min (), hi = std::numeric_limits<T>::max ();
    const std::vector<T> low (dimension, lo), high (dimension, hi);
    auto a = vector<V> (low), b = vector<V> (high);
    const auto check = [dimension] (const V& v, T coordinate) {
      const wide expected = wide (dimension) * coordinate;
      if constexpr (requires { v.cached_sum (); })
        require (v.cached_sum () == expected, "extreme cached sum is exact");
      require (posets::utils::sum_key (v) == expected, "extreme reducer sum is exact");
    };
    check (a, lo);
    check (b, hi);
    require (a.partial_order (b).leq (), "extreme cached order is monotone");
    check (a.copy (), lo);
    check (a.meet (b), lo);
    check (a.join (b), hi);
    auto changed = a.copy ();
    changed.join_with (b);
    check (changed, hi);
    changed.meet_with (a);
    check (changed, lo);
    changed = b.copy ();
    check (changed, hi);
    std::vector<V> candidates;
    candidates.push_back (a.copy ());
    candidates.push_back (b.copy ());
    candidates.push_back (b.copy ());
    auto reduced = posets::utils::reduce_to_maxima (std::move (candidates));
    require (reduced.size () == 1 and reduced.front () == b, "extreme reducer maxima");
    posets::downsets::rank_bucketed_vector_backed<V> ranked (b.copy ());
    require (ranked.contains (a), "extreme rank containment");
    require (not ranked.insert (a.copy ()), "extreme rank rejects dominated point");
    posets::downsets::rank_bucketed_vector_backed<V> growing (a.copy ());
    require (growing.insert (b.copy ()) and growing.size () == 1, "extreme rank removes victim");
    posets::downsets::skiplist_backed<V> skip (b.copy ());
    require (skip.contains (a), "extreme skiplist containment");
    require (not skip.insert (a.copy ()), "extreme skiplist rejects dominated point");
    posets::downsets::bboxtree_backed<V> bbox (b.copy ());
    require (bbox.contains (a), "extreme bbox key containment");
  }

  template <class V>
  void composite () {
    using T = V::value_type;
    const auto hi = std::numeric_limits<T>::max ();
    const std::array<T, 4> raw {hi, hi, -1, 0};
    const auto v = vector<V> (raw);
    require (v.cached_sum () == __int128_t (hi) * 2 - 1, "composite cached sum is exact");
    require (posets::utils::sum_key (v) == __int128_t (hi) * 2 - 1,
             "composite reducer sum is exact");
  }

  template <template <class> class Set, class V>
  void rank_cutoff () {
    using T = V::value_type;
    const std::array<T, 2> raw {std::numeric_limits<T>::max (), std::numeric_limits<T>::max ()};
    const std::array<T, 2> zero {0, 0};
    Set<V> subject (vector<V> (raw));
    require (subject.contains (vector<V> (zero)), "overflowing rank must not prune zero query");
    require (not subject.insert (vector<V> (zero)), "overflowing rank rejects dominated zero");
  }
}

int main (int argc, char** argv) {
  using namespace posets::vectors;
  const std::string_view suite = argc > 1 ? argv[1] : "all";
  if (suite == "all" or suite == "narrow-bin") {
    narrowing<vector_backed<int64_t>> ();
    narrowing<vector_backed_sum<int64_t>> ();
    narrowing<simd_vector_backed<int64_t>> ();
    narrowing<simd_vector_backed_sum<int64_t>> ();
  }
  if (suite == "all" or suite == "dimension") {
    require (
        sizeof (decltype (std::declval<const vector_backed_sum<int8_t>&> ().cached_sum ())) == 8,
        "narrow sums retain int64 storage");
    dimension_guards<int8_t> ();
    dimension_guards<int16_t> ();
    dimension_guards<int32_t> ();
    require (posets::utils::sum_key (dimension_probe<int64_t> (SIZE_MAX)) == 0,
             "int64 coordinates admit every size_t dimension");
  }
  if (suite == "all" or suite == "bin-capacity")
    bin_capacity ();
  if (suite == "all" or suite == "bin" or suite == "oracle") {
    bins<vector_backed_sum<int8_t>> (suite != "oracle");
    bins<vector_backed<int8_t>> (suite != "oracle");
    bins<simd_vector_backed_sum<int8_t>> (suite != "oracle");
    bins<simd_vector_backed<int8_t>> (suite != "oracle");
    // Empty coordinate domains still have one usable bin.
    using V = vector_backed_sum<int8_t>;
    posets::downsets::vector_backed_bin<V> empty_point (V (0U));
    require (empty_point.contains (V (0U)), "zero-dimensional bin");
  }
  if (suite == "all" or suite == "sum") {
    sums<vector_backed_sum<int8_t>> (65);
    sums<simd_vector_backed_sum<int16_t>> (65);
    sums<vector_backed_sum<int32_t>> (3);
    sums<simd_vector_backed_sum<int64_t>> (257);
    sums<array_backed_sum<int64_t, 257>> (257);
    sums<array_ptr_backed_sum<int64_t, 257>> (257);
    sums<simd_array_backed_sum<int64_t, 257>> (257);
    sums<simd_array_ptr_backed_sum<int64_t, 257>> (257);
    // The narrow-coordinate cached int boundary, just beyond 2^31 / 128.
    narrow_width ();
  }
  if (suite == "all" or suite == "uncached") {
    sums<vector_backed<int64_t>> (257);
    sums<simd_vector_backed<int64_t>> (257);
  }
  if (suite == "all" or suite == "composite") {
    using inner = vector_backed_sum<int64_t>;
    composite<x_and_boolvec<inner>> ();
    composite<x_and_bitset<inner, 1>> ();
    composite<x_and_wordvec<inner>> ();
  }
  if (suite == "all" or suite == "rank")
    rank_cutoff<posets::downsets::rank_bucketed_vector_backed, vector_backed<int64_t>> ();
  if (suite == "all" or suite == "skip")
    rank_cutoff<posets::downsets::skiplist_backed, vector_backed<int32_t>> ();
}
