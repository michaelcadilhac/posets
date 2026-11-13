#pragma once

#include <compare>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/sum.hh>
#include <posets/utils/vector_mm.hh>
#include <posets/vectors/traits.hh>

namespace posets::vectors {

  template <typename X>
  class x_and_boolvec {
    public:
      using value_type = X::value_type;

      x_and_boolvec (size_t k)
        : k {checked_dimension (k)},
          x {std::min (bool_threshold, k)},
          bools (k - std::min (bool_threshold, k), false),
          sum {0} {}

      x_and_boolvec (std::span<const value_type> v)
        : k {checked_dimension (v.size ())},
          x {std::span (v.data (), std::min (k, bool_threshold))},
          sum {0} {
        bools.reserve (k - std::min (k, bool_threshold));
        for (size_t i = bool_threshold; i < k; ++i) {
          bools.push_back (v[i] + 1);
          if (bools[i - bool_threshold])
            sum++;
        }
      }

      x_and_boolvec (std::initializer_list<value_type> v)
        : x_and_boolvec (posets::utils::vector_mm<value_type> (v)) {}

      [[nodiscard]] size_t size () const { return k; }

      x_and_boolvec (x_and_boolvec&& other) = default;

    private:
      static size_t checked_dimension (size_t k) {
        if constexpr (requires (const X& inner) { inner.cached_sum (); })
          utils::check_sum_dimension<value_type> (k);
        return k;
      }

      x_and_boolvec (size_t k, X&& x, std::vector<bool>&& bs)
        : k {checked_dimension (k)},
          x {std::move (x)},
          bools {std::move (bs)} {
        // NOLINTNEXTLINE(boost-use-ranges)
        sum = std::accumulate (bools.begin (), bools.end (), size_t {0});
      }

      x_and_boolvec (size_t k, X&& x, std::vector<bool>&& bs, size_t sum)
        : k {checked_dimension (k)},
          x {std::move (x)},
          bools {std::move (bs)},
          sum {sum} {
        // NOLINTNEXTLINE(boost-use-ranges)
        assert (sum == std::accumulate (bools.begin (), bools.end (), size_t {0}));
      }

    public:
      // explicit copy operator
      [[nodiscard]] x_and_boolvec copy () const {
        std::vector<bool> b = bools;
        return x_and_boolvec (k, x.copy (), std::move (b), sum);
      }

      x_and_boolvec& operator= (x_and_boolvec&& other) noexcept (
          std::is_nothrow_move_assignable_v<X>) {
        if (this != &other) {
          k = other.k;
          x = std::move (other.x);
          bools = std::move (other.bools);
          sum = other.sum;
        }
        return *this;
      }

      x_and_boolvec& operator= (const x_and_boolvec& other) = delete;

      void to_vector (std::span<value_type> v) const {
        x.to_vector (std::span (v.data (), std::min (bool_threshold, k)));
        for (size_t i = bool_threshold; i < k; ++i)
          v[i] = static_cast<int> (bools[i - bool_threshold]) - 1;
      }

      class po_res {
          using inner_order =
              decltype (std::declval<const X&> ().partial_order (std::declval<const X&> ()));

        public:
          po_res (const x_and_boolvec& lhs, const x_and_boolvec& rhs)
            : lhs_x {lhs.x},
              rhs_x {rhs.x} {
            // Note that we are putting the bitset first in that comparison.
            bgeq = (lhs.sum >= rhs.sum);
            bleq = (lhs.sum <= rhs.sum);

            for (size_t i = bool_threshold; (bgeq or bleq) and i < lhs.k; ++i) {
              bgeq =
                  bgeq and (lhs.bools[i - bool_threshold] or (not rhs.bools[i - bool_threshold]));
              bleq =
                  bleq and ((not lhs.bools[i - bool_threshold]) or rhs.bools[i - bool_threshold]);
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

      [[nodiscard]] auto partial_order (const x_and_boolvec& rhs) const {
        assert (rhs.k == k);
        return po_res (*this, rhs);
      }

      bool operator== (const x_and_boolvec& rhs) const {
        return sum == rhs.sum and bools == rhs.bools and x == rhs.x;
      }

      bool operator!= (const x_and_boolvec& rhs) const {
        return sum != rhs.sum or bools != rhs.bools or x != rhs.x;
      }

      value_type operator[] (size_t i) const {
        if (i >= bool_threshold)
          return static_cast<int> (bools[i - bool_threshold]) - 1;
        return x[i];
      }

      [[nodiscard]] x_and_boolvec meet (const x_and_boolvec& rhs) const {
        assert (rhs.k == k);
        std::vector<bool> res (bools.size (), false);
        std::transform (bools.cbegin (), bools.cend (), rhs.bools.cbegin (), res.begin (),
                        [] (bool x, bool y) { return x and y; });
        return x_and_boolvec (k, x.meet (rhs.x), std::move (res));
      }

      [[nodiscard]] x_and_boolvec join (const x_and_boolvec& rhs) const {
        assert (rhs.k == k);
        std::vector<bool> res (bools.size (), false);
        std::transform (bools.cbegin (), bools.cend (), rhs.bools.cbegin (), res.begin (),
                        [] (bool lhs, bool rhs) { return lhs or rhs; });
        return x_and_boolvec (k, x.join (rhs.x), std::move (res));
      }

      void meet_with (const x_and_boolvec& rhs) {
        assert (rhs.k == k);
        x.meet_with (rhs.x);
        for (size_t i = 0; i < bools.size (); ++i)
          bools[i] = bools[i] and rhs.bools[i];
        // NOLINTNEXTLINE(boost-use-ranges)
        sum = std::accumulate (bools.begin (), bools.end (), size_t {0});
      }

      void join_with (const x_and_boolvec& rhs) {
        assert (rhs.k == k);
        x.join_with (rhs.x);
        for (size_t i = 0; i < bools.size (); ++i)
          bools[i] = bools[i] or rhs.bools[i];
        // NOLINTNEXTLINE(boost-use-ranges)
        sum = std::accumulate (bools.begin (), bools.end (), size_t {0});
      }

      [[nodiscard]] utils::sum_t<value_type> cached_sum () const
        requires requires (const X& inner) { inner.cached_sum (); }
      {
        // Active tail bits encode 0; inactive bits encode -1.
        return static_cast<utils::sum_t<value_type>> (x.cached_sum ()) -
               static_cast<utils::sum_t<value_type>> ((k - x.size ()) - sum);
      }

      bool operator< (const x_and_boolvec& rhs) const {
        auto cmp = bools <=> rhs.bools;  // three-way comparison
        if (std::is_eq (cmp))
          return (x < rhs.x);
        return std::is_lt (cmp);
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
          os << static_cast<int> ((*this)[i]) << " ";
        os << "}";
        return os;
      }

    private:
      size_t k;
      X x;
      std::vector<bool> bools;
      size_t sum;  // The sum of all the elements of bools (not of X) seen as 0/1 values.
  };
}
