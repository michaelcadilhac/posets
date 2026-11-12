#pragma once

namespace posets::vectors {
  // Both directions are computed lazily and cached, so a caller that asks for
  // one direction pays for one pass with early exit, and a caller that asks
  // for both pays for at most two.  Antichain scans ask for one direction and
  // mostly find incomparable pairs, so the first violating block decides.
  template <typename Vec>
  class generic_partial_order {
    public:
      generic_partial_order (const Vec& lhs, const Vec& rhs) : lhs {lhs}, rhs {rhs} {}

      [[gnu::always_inline]] bool geq () {
        if (not has_bgeq) {
          has_bgeq = true;
          bgeq = compute_geq ();
        }
        return bgeq;
      }

      [[gnu::always_inline]] bool leq () {
        if (not has_bleq) {
          has_bleq = true;
          bleq = compute_leq ();
        }
        return bleq;
      }

      // Synthesized bounds may overflow a cached sum even when their points do not.
      [[nodiscard, gnu::always_inline]] bool leq_coordinates () const {
        const size_t nblocks = lhs.data_size ();
        const auto* const l = lhs.data ();
        const auto* const r = rhs.data ();
        for (size_t b = 0; b < nblocks; ++b) {
          if constexpr (Vec::uses_simd) {
            if (not std::experimental::all_of (l[b] <= r[b]))
              return false;
          }
          else {
            for (size_t i = 0; i < Vec::items_per_block; ++i)
              if (not(l[b][i] <= r[b][i]))
                return false;
          }
        }
        return true;
      }

    private:
      [[nodiscard, gnu::always_inline]] bool compute_geq () const {
        if constexpr (requires { lhs.sum; })
          if (lhs.sum < rhs.sum)
            return false;
        const size_t nblocks = lhs.data_size ();
        const auto* const l = lhs.data ();
        const auto* const r = rhs.data ();
        for (size_t b = 0; b < nblocks; ++b) {
          if constexpr (Vec::uses_simd) {
            if (not std::experimental::all_of (l[b] >= r[b]))
              return false;
          }
          else {
            for (size_t i = 0; i < Vec::items_per_block; ++i)
              if (not(l[b][i] >= r[b][i]))
                return false;
          }
        }
        return true;
      }

      [[nodiscard, gnu::always_inline]] bool compute_leq () const {
        if constexpr (requires { lhs.sum; })
          if (lhs.sum > rhs.sum)
            return false;
        return leq_coordinates ();
      }

      const Vec& lhs;
      const Vec& rhs;
      bool bgeq = true, bleq = true;
      bool has_bgeq = false, has_bleq = false;
  };
}
