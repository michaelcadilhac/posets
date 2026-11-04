#pragma once

namespace posets::vectors {
  template <typename Vec>
  class generic_partial_order {
    public:
      generic_partial_order (const Vec& lhs, const Vec& rhs)
        : lhs {lhs},
          rhs {rhs},
          nblocks {lhs.data_size ()} {
        if constexpr (requires { lhs.sum; }) {
          bgeq = (lhs.sum >= rhs.sum);
          if (not bgeq)
            has_bgeq = true;

          bleq = (lhs.sum <= rhs.sum);
          if (not bleq)
            has_bleq = true;

          if (has_bgeq or has_bleq)
            return;
        }
        for (up_to = 0; up_to < nblocks; ++up_to) {
          if constexpr (Vec::uses_simd) {
            bgeq = bgeq and (std::experimental::all_of (lhs.data ()[up_to] >= rhs.data ()[up_to]));
            bleq = bleq and (std::experimental::all_of (lhs.data ()[up_to] <= rhs.data ()[up_to]));
          }
          else {
            for (size_t i = 0; i < Vec::items_per_block; ++i) {
              const auto left = lhs.data ()[up_to][i];
              const auto right = rhs.data ()[up_to][i];
              bgeq = bgeq and (left >= right);
              bleq = bleq and (left <= right);
            }
          }

          if (not bgeq)
            has_bgeq = true;
          if (not bleq)
            has_bleq = true;
          if (has_bgeq or has_bleq)
            return;
        }
        has_bgeq = true;
        has_bleq = true;
      }

      bool geq () {
        if (has_bgeq)
          return bgeq;
        assert (has_bleq);
        has_bgeq = true;
        for (; up_to < nblocks; ++up_to) {
          if constexpr (Vec::uses_simd)
            bgeq = bgeq and (std::experimental::all_of (lhs.data ()[up_to] >= rhs.data ()[up_to]));
          else
            for (size_t i = 0; i < Vec::items_per_block; ++i) {
              const auto left = lhs.data ()[up_to][i];
              const auto right = rhs.data ()[up_to][i];
              bgeq = bgeq and (left >= right);
            }

          if (not bgeq)
            break;
        }
        return bgeq;
      }

      bool leq () {
        if (has_bleq)
          return bleq;
        assert (has_bgeq);
        has_bleq = true;
        for (; up_to < nblocks; ++up_to) {
          if constexpr (Vec::uses_simd)
            bleq = bleq and (std::experimental::all_of (lhs.data ()[up_to] <= rhs.data ()[up_to]));
          else
            for (size_t i = 0; i < Vec::items_per_block; ++i) {
              const auto left = lhs.data ()[up_to][i];
              const auto right = rhs.data ()[up_to][i];
              bleq = bleq and (left <= right);
            }

          if (not bleq)
            break;
        }
        return bleq;
      }

    private:
      const Vec& lhs;
      const Vec& rhs;
      const size_t nblocks;
      bool bgeq = true, bleq = true;
      bool has_bgeq = false, has_bleq = false;
      size_t up_to = 0;
  };
}
