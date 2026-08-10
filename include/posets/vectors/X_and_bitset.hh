#pragma once
#include <bitset>
#include <optional>
#include <utility>

#include <posets/concepts.hh>
#include <posets/utils/vector_mm.hh>
#include <posets/vectors/traits.hh>

namespace posets::vectors {

  static constexpr size_t nbools_to_nbitsets (size_t nbools) {
    return (nbools + sizeof (unsigned long) * 8 - 1) / (sizeof (unsigned long) * 8);
  }

  static constexpr size_t nbitsets_to_nbools (size_t nbitsets) {
    return nbitsets * sizeof (unsigned long) * 8;
  }

  template <typename X, size_t NBitsets, size_t Bools = nbitsets_to_nbools (NBitsets)>
  class x_and_bitset {
    public:
      using value_type = typename X::value_type;

      x_and_bitset (size_t k) : k {k}, x {std::min (bitset_threshold, k)} {}

      x_and_bitset (std::span<const value_type> v)
        : k {v.size ()},
          x {std::span (v.data (), std::min (bitset_threshold, k))},
          sum {0} {
        bools.reset ();
        for (size_t i = bitset_threshold; i < k; ++i) {
          bools[i - bitset_threshold] = v[i] + 1;
          if (bools[i - bitset_threshold])
            sum++;
        }
        assert (sum == bools.count ());
      }

      x_and_bitset (std::initializer_list<value_type> v)
        : x_and_bitset (posets::utils::vector_mm<value_type> (v)) {}

      [[nodiscard]] size_t size () const { return k; }

      x_and_bitset (x_and_bitset&& other) = default;

    private:
      x_and_bitset (size_t k, X&& x, std::bitset<Bools>&& bools)
        : k {k},
          x {std::move (x)},
          bools {std::move (bools)} {
        sum = this->bools.count ();
      }

      x_and_bitset (size_t k, X&& x, std::bitset<Bools>&& bools, size_t sum)
        : k {k},
          x {std::move (x)},
          bools {std::move (bools)},
          sum {sum} {
        assert (sum == this->bools.count ());
      }

    public:
      // explicit copy operator
      [[nodiscard]] x_and_bitset copy () const {
        std::bitset<Bools> b = bools;
        return x_and_bitset (k, x.copy (), std::move (b), sum);
      }

      x_and_bitset& operator= (x_and_bitset&& other) = default;

      x_and_bitset& operator= (const x_and_bitset& other) = delete;

      void to_vector (std::span<value_type> v) const {
        x.to_vector (std::span (v.data (), bitset_threshold));
        for (size_t i = bitset_threshold; i < k; ++i)
          v[i] = bools[i - bitset_threshold] - 1;
      }

      class po_res {
          using inner_order =
              decltype (std::declval<const X&> ().partial_order (std::declval<const X&> ()));

        public:
          po_res (const x_and_bitset& lhs, const x_and_bitset& rhs)
            : lhs_x {lhs.x},
              rhs_x {rhs.x} {
            // Note that we are putting the bitset first in that comparison.
            bgeq = (lhs.sum >= rhs.sum);
            bleq = (lhs.sum <= rhs.sum);

            if (bgeq or bleq) {
              auto diff = lhs.bools | rhs.bools;
              bgeq = bgeq and (diff == lhs.bools);
              bleq = bleq and (diff == rhs.bools);
            }

            has_bgeq = not bgeq;
            has_bleq = not bleq;
          }

          bool geq () {
            if (not has_bgeq) {
              has_bgeq = true;
              bgeq = order ().geq ();
            }
            return bgeq;
          }

          bool leq () {
            if (not has_bleq) {
              has_bleq = true;
              bleq = order ().leq ();
            }
            return bleq;
          }

        private:
          inner_order& order () {
            if (not inner.has_value ())
              inner.emplace (lhs_x.partial_order (rhs_x));
            return *inner;
          }

          const X& lhs_x;
          const X& rhs_x;
          std::optional<inner_order> inner;
          bool bgeq;
          bool bleq;
          bool has_bgeq;
          bool has_bleq;
      };

      [[nodiscard]] auto partial_order (const x_and_bitset& rhs) const {
        assert (rhs.k == k);
        return po_res (*this, rhs);
      }

      bool operator== (const x_and_bitset& rhs) const {
        assert (not(sum != rhs.sum and bools == rhs.bools));
        return sum == rhs.sum and bools == rhs.bools and x == rhs.x;
      }

      bool operator!= (const x_and_bitset& rhs) const {
        assert (not(sum != rhs.sum and bools == rhs.bools));
        return sum != rhs.sum or bools != rhs.bools or x != rhs.x;
      }

      value_type operator[] (size_t i) const {
        if (i >= bitset_threshold)
          return bools[i - bitset_threshold] - 1;
        return x[i];
      }

      [[nodiscard]] x_and_bitset meet (const x_and_bitset& rhs) const {
        assert (rhs.k == k);
        return x_and_bitset (k, x.meet (rhs.x), bools bitand rhs.bools);
      }

      [[nodiscard]] x_and_bitset join (const x_and_bitset& rhs) const {
        assert (rhs.k == k);
        return x_and_bitset (k, x.join (rhs.x), bools bitor rhs.bools);
      }

      void meet_with (const x_and_bitset& rhs) {
        assert (rhs.k == k);
        x.meet_with (rhs.x);
        bools and_eq rhs.bools;
        sum = bools.count ();
      }

      void join_with (const x_and_bitset& rhs) {
        assert (rhs.k == k);
        x.join_with (rhs.x);
        bools or_eq rhs.bools;
        sum = bools.count ();
      }

      [[nodiscard]] long cached_sum () const
        requires requires (const X& inner) { inner.cached_sum (); }
      {
        return static_cast<long> (sum) + static_cast<long> (x.cached_sum ());
      }

      bool operator< (const x_and_bitset& rhs) const {
        const int cmp = std::memcmp (&bools, &rhs.bools, sizeof (bools));
        if (cmp == 0)
          return (x < rhs.x);
        return (cmp < 0);
      }

      [[nodiscard]] auto bin () const {
        auto bitset_bin = sum;  // / (k - bitset_threshold);

        // Even if X doesn't have bin (), our local sum is valid, in that:
        //   if u dominates v, then in particular, it dominates it over the boolean part, so u.sum
        //   >= v.sum.
        if constexpr (has_bin<X>::value)
          bitset_bin += x.bin ();

        return bitset_bin;
      }

      std::ostream& print (std::ostream& os) const {
        os << "{ ";
        for (size_t i = 0; i < this->size (); ++i)
          os << (int) (*this)[i] << " ";
        os << "}";
        return os;
      }

    private:
      size_t k;  // used to be const, but it does not add much, and forces
                 // explicit definition of move operators.
      X x;
      std::bitset<Bools> bools;
      size_t sum;  // The sum of all the elements of bools (not of X) seen as 0/1 values.
  };

  template <class X>
  class x_and_bitset<X, 0> : public X {
      using X::X;

    public:
      x_and_bitset (X&& x) : X (std::move (x)) {}
      x_and_bitset copy () const { return X::copy (); }
      x_and_bitset meet (const x_and_bitset& other) const { return X::meet (other); }
      x_and_bitset join (const x_and_bitset& other) const { return X::join (other); }
      void meet_with (const x_and_bitset& other) { X::meet_with (other); }
      void join_with (const x_and_bitset& other) { X::join_with (other); }
  };
}
