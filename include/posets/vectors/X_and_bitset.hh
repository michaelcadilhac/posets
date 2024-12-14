#pragma once
#include <bitset>

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

  template <typename X, size_t NBitsets>
  class X_and_bitset {
      static constexpr auto Bools = nbitsets_to_nbools (NBitsets);

    public:
      using value_type = typename X::value_type;

      X_and_bitset (size_t k) : k {k}, x {std::min (bitset_threshold, k)} {}

      X_and_bitset (std::span<const value_type> v)
        : k {v.size ()},
          x {std::span (v.data (), std::min (bitset_threshold, k))},
          sum {0} {
        bools.reset ();
        for (size_t i = bitset_threshold; i < k; ++i)
          if ((bools[i - bitset_threshold] = (v[i] + 1)) == true)
            sum++;
        assert (sum == bools.count ());
      }

      X_and_bitset (std::initializer_list<value_type> v)
        : X_and_bitset (posets::utils::vector_mm<value_type> (v)) {}

      size_t size () const { return k; }

      X_and_bitset (X_and_bitset&& other) = default;

    private:
      X_and_bitset (size_t k, X&& x, std::bitset<Bools>&& bools)
        : k {k},
          x {std::move (x)},
          bools {std::move (bools)} {
        sum = this->bools.count ();
      }

      X_and_bitset (size_t k, X&& x, std::bitset<Bools>&& bools, size_t sum)
        : k {k},
          x {std::move (x)},
          bools {std::move (bools)},
          sum {sum} {
        assert (sum == this->bools.count ());
      }

    public:
      // explicit copy operator
      X_and_bitset copy () const {
        std::bitset<Bools> b = bools;
        return X_and_bitset (k, x.copy (), std::move (b), sum);
      }

      X_and_bitset& operator= (X_and_bitset&& other) = default;

      X_and_bitset& operator= (const X_and_bitset& other) = delete;

      void to_vector (std::span<value_type> v) const {
        x.to_vector (std::span (v.data (), bitset_threshold));
        for (size_t i = bitset_threshold; i < k; ++i)
          v[i] = bools[i - bitset_threshold] - 1;
      }

      class po_res {
        public:
          po_res (const X_and_bitset& lhs, const X_and_bitset& rhs) {
            // Note that we are putting the bitset first in that comparison.
            bgeq = (lhs.sum >= rhs.sum);
            bleq = (lhs.sum <= rhs.sum);

            if (bgeq or bleq) {
              auto diff = lhs.bools | rhs.bools;
              bgeq = bgeq and (diff == lhs.bools);
              bleq = bleq and (diff == rhs.bools);
            }

            if (not bgeq and not bleq)
              return;

            auto po = lhs.x.partial_order (rhs.x);
            bgeq = bgeq and po.geq ();
            bleq = bleq and po.leq ();
          }

          inline bool geq () { return bgeq; }

          inline bool leq () { return bleq; }

        private:
          bool bgeq, bleq;
      };

      inline auto partial_order (const X_and_bitset& rhs) const {
        assert (rhs.k == k);
        return po_res (*this, rhs);
      }

      bool operator== (const X_and_bitset& rhs) const {
        assert (not(sum != rhs.sum and bools == rhs.bools));
        return sum == rhs.sum and bools == rhs.bools and x == rhs.x;
      }

      bool operator!= (const X_and_bitset& rhs) const {
        assert (not(sum != rhs.sum and bools == rhs.bools));
        return sum != rhs.sum or bools != rhs.bools or x != rhs.x;
      }

      value_type operator[] (size_t i) const {
        if (i >= bitset_threshold)
          return bools[i - bitset_threshold] - 1;
        return x[i];
      }

    public:
      X_and_bitset meet (const X_and_bitset& rhs) const {
        assert (rhs.k == k);
        return X_and_bitset (k, x.meet (rhs.x), bools & rhs.bools);
      }

      bool operator< (const X_and_bitset& rhs) const {
        int cmp = std::memcmp (&bools, &rhs.bools, sizeof (bools));
        if (cmp == 0)
          return (x < rhs.x);
        return (cmp < 0);
      }

      auto bin () const {
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
  class X_and_bitset<X, 0> : public X {
      using X::X;

    public:
      X_and_bitset (X&& x) : X (std::move (x)) {}
      X_and_bitset copy () const { return X::copy (); }
      X_and_bitset meet (const X_and_bitset& other) const { return X::meet (other); }
  };
}
