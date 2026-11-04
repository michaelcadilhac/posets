#pragma once

#include <functional>
#include <iterator>
#include <ranges>

namespace posets::utils {
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
