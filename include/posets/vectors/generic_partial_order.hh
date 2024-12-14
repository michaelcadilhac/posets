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
              auto diff =
                  lhs[up_to * Vec::items_per_block + i] - rhs[up_to * Vec::items_per_block + i];
              bgeq = bgeq and (diff >= 0);
              bleq = bleq and (diff <= 0);
            }
          }

          // An alternative way:
          // auto diff = lhs.data ()[up_to] - rhs.data ()[up_to];
          // bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
          // bleq = bleq and (std::experimental::reduce (-diff, std::bit_or ()) >= 0);
          if (not bgeq)
            has_bgeq = true;
          if (not bleq)
            has_bleq = true;
          if (has_bgeq or has_bgeq)
            return;
        }
        has_bgeq = true;
        has_bleq = true;
      }

      inline bool geq () {
        if (has_bgeq)
          return bgeq;
        assert (has_bleq);
        has_bgeq = true;
        for (; up_to < nblocks; ++up_to) {
          if constexpr (Vec::uses_simd)
            bgeq = bgeq and (std::experimental::all_of (lhs.data ()[up_to] >= rhs.data ()[up_to]));
          else
            for (size_t i = 0; i < Vec::items_per_block; ++i) {
              auto diff =
                  lhs[up_to * Vec::items_per_block + i] - rhs[up_to * Vec::items_per_block + i];
              bgeq = bgeq and (diff >= 0);
            }

          if (not bgeq)
            break;
        }
        return bgeq;
      }

      inline bool leq () {
        if (has_bleq)
          return bleq;
        assert (has_bgeq);
        has_bleq = true;
        for (; up_to < nblocks; ++up_to) {
          if constexpr (Vec::uses_simd)
            bleq = bleq and (std::experimental::all_of (lhs.data ()[up_to] <= rhs.data ()[up_to]));
          else
            for (size_t i = 0; i < Vec::items_per_block; ++i) {
              auto diff =
                  lhs[up_to * Vec::items_per_block + i] - rhs[up_to * Vec::items_per_block + i];
              bleq = bleq and (diff <= 0);
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
