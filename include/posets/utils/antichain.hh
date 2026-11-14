#pragma once

#include <functional>
#include <iterator>
#include <ranges>
#include <utility>
#include <vector>

namespace posets::utils {
  // N7: maxima must already be an antichain. Once candidate dominates an old
  // maximum a, no other old maximum b can dominate candidate: transitivity
  // would give a <= b, contradicting prior incomparability. Equality rejects
  // before removal, retaining the old representative. Thus first rejection
  // performs no mutation or moves. Later comparisons/moves may throw after
  // compaction has begun; this scan deliberately retains that basic guarantee.
  template <class V, class BeforeMutation>
  [[nodiscard]] bool insert_maximum (std::vector<V>& maxima,
                                     typename std::vector<V>::value_type&& candidate,
                                     BeforeMutation before_mutation) {
    static_assert (noexcept (before_mutation ()));
    bool removing = false;
    auto output = maxima.begin ();
    const auto end = maxima.end ();
    for (auto current = output; current != end; ++current) {
      auto order = candidate.partial_order (*current);
      if (not removing and order.leq ())
        return false;
      if (order.geq ()) {
        removing = true;
        before_mutation ();
      }
      else {
        if (output != current)
          *output = std::move (*current);
        ++output;
      }
    }
    if (output != end)
      maxima.erase (output, end);
    before_mutation ();
    maxima.push_back (std::move (candidate));
    return true;
  }

  template <class V>
  [[nodiscard]] bool insert_maximum (std::vector<V>& maxima,
                                     typename std::vector<V>::value_type&& candidate) {
    return insert_maximum (maxima, std::move (candidate), [] () noexcept {});
  }

  // Diagnostic only: visit unordered pairs in storage order and reject the
  // first comparable pair (including duplicates), without mutation. Projections
  // only select values; comparison/projection failures propagate to the caller.
  template <std::ranges::forward_range R, class Proj = std::identity>
  [[nodiscard]] bool is_antichain (const R& values, Proj proj = {}) {
    for (auto first = std::ranges::begin (values); first != std::ranges::end (values); ++first)
      for (auto second = std::ranges::next (first); second != std::ranges::end (values);
           ++second) {
        auto&& left = proj (*first);
        auto&& right = proj (*second);
        auto order = left.partial_order (right);
        if (order.leq () or order.geq ())
          return false;
      }
    return true;
  }
}
