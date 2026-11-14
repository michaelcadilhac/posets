#pragma once

#include <cassert>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include <posets/utils/antichain.hh>
#include <posets/utils/reduce.hh>

namespace posets::utils {
  template <class V>
  struct trie_value_projection {
      [[nodiscard]] const V& operator() (const V* value) const { return *value; }
      [[nodiscard]] V&& operator() (V*&& value) const { return std::move (*value); }
  };

  template <class Trie, class V>
  void reset_trie_maxima (Trie& trie, std::vector<V>&& elements) {
    // N8: reduction and even debug comparisons precede publication/consumption.
    auto antichain = reduce_to_maxima (std::move (elements));
    assert (is_antichain (antichain));
    trie.relabel_trie (std::move (antichain));
  }

  // N8: finish every allocation, comparison and diagnostic before relabel_trie
  // can consume live sources. Its transaction resolves selecting projections
  // and builds unpublished storage first, then uses reserved nothrow moves (or
  // copies for throwing moves) and a nonthrowing swap. Do no throwing work after
  // that call. Canonical operands and strict-left/nonstrict-right filtering keep
  // each movable source unique and retain the left representative of duplicates.
  template <class Trie>
  void union_trie_maxima (Trie& left, Trie& right) {
    if (std::addressof (left) == std::addressof (right) or right.size () == 0)
      return;
    if (left.size () == 0) {
      left = std::move (right);
      return;
    }

    using v = std::ranges::range_value_t<Trie>;
    std::vector<v*> result;
    result.reserve (left.size () + right.size ());
    for (auto& value : left)
      if (not right.dominates (value, true))
        result.push_back (&value);
    for (auto& value : right)
      if (not left.dominates (value))
        result.push_back (&value);

    if (result.size () == left.size ()) {
      bool unchanged = true;
      for (size_t i = 0; i < result.size (); ++i)
        unchanged and_eq result[i] == &left.get_backing_vector ()[i];
      if (unchanged)
        return;
    }

    assert (not result.empty ());
    assert (is_antichain (result, trie_value_projection<v> {}));
    left.relabel_trie (std::move (result), trie_value_projection<v> {});
  }

  template <class Trie>
  void intersect_trie_maxima (Trie& left, const Trie& right) {
    if (std::addressof (left) == std::addressof (right) or left.size () == 0)
      return;
    using v = std::ranges::range_value_t<Trie>;
    if (right.size () == 0) {
      reset_trie_maxima (left, std::vector<v> {});
      return;
    }

    std::vector<v> intersection;
    bool smaller_set = false;
    // N8: meets/copies only populate unpublished storage; neither live operand
    // is consumed while these potentially throwing computations run.
    for (auto& x : left) {
      assert (x.size () > 0);
      // If x belongs to right, it dominates all its pairwise meets.
      const bool dominated = right.dominates (x);
      if (dominated)
        intersection.push_back (x.copy ());
      else
        for (const auto& y : right)
          intersection.push_back (x.meet (y));
      smaller_set or_eq not dominated;
    }
    if (smaller_set)
      reset_trie_maxima (left, std::move (intersection));
  }

  template <class Downset, class Trie, class F>
  [[nodiscard]] Downset apply_trie_maxima (const Trie& trie, const F& transform) {
    using v = std::ranges::range_value_t<Trie>;
    const auto& backing = trie.get_backing_vector ();
    std::vector<v> elements;
    elements.reserve (backing.size ());
    for (const auto& value : backing)
      elements.push_back (transform (value));
    return Downset (std::move (elements));
  }
}
