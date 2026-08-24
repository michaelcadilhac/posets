#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/vector_mm.hh>
#include <posets/vectors/traits.hh>

namespace posets::vectors {

  template <typename X>
  class x_and_wordvec {
    public:
      using value_type = typename X::value_type;

      x_and_wordvec (size_t k)
        : k {k},
          x {std::min (bool_threshold, k)},
          bool_count {k - std::min (bool_threshold, k)},
          bools (words_for (bool_count), 0),
          sum {0} {
        clear_unused_bits ();
      }

      x_and_wordvec (std::span<const value_type> v)
        : k {v.size ()},
          x {std::span (v.data (), std::min (k, bool_threshold))},
          bool_count {k - std::min (k, bool_threshold)},
          bools (words_for (bool_count), 0),
          sum {0} {
        for (size_t i = 0; i < bool_count; ++i) {
          if (v[inner_size () + i] + 1) {
            bools[word_for (i)] or_eq mask_for (i);
            ++sum;
          }
        }
        clear_unused_bits ();
        assert (sum == popcount ());
      }

      x_and_wordvec (std::initializer_list<value_type> v)
        : x_and_wordvec (posets::utils::vector_mm<value_type> (v)) {}

      [[nodiscard]] size_t size () const { return k; }

      x_and_wordvec (x_and_wordvec&& other) = default;

    private:
      x_and_wordvec (size_t k, size_t bool_count, X&& x, std::vector<uint64_t>&& bs)
        : k {k},
          x {std::move (x)},
          bool_count {bool_count},
          bools {std::move (bs)},
          sum {0} {
        assert (bools.size () == words_for (bool_count));
        clear_unused_bits ();
        sum = popcount ();
      }

      x_and_wordvec (size_t k, size_t bool_count, X&& x, std::vector<uint64_t>&& bs, size_t sum)
        : k {k},
          x {std::move (x)},
          bool_count {bool_count},
          bools {std::move (bs)},
          sum {sum} {
        assert (bools.size () == words_for (bool_count));
        clear_unused_bits ();
        assert (sum == popcount ());
      }

    public:
      // explicit copy operator
      [[nodiscard]] x_and_wordvec copy () const {
        std::vector<uint64_t> b = bools;
        return x_and_wordvec (k, bool_count, x.copy (), std::move (b), sum);
      }

      x_and_wordvec& operator= (x_and_wordvec&& other) = default;

      x_and_wordvec& operator= (const x_and_wordvec& other) = delete;

      void to_vector (std::span<value_type> v) const {
        x.to_vector (std::span (v.data (), inner_size ()));
        for (size_t i = 0; i < bool_count; ++i)
          v[inner_size () + i] = static_cast<int> (tail_bit (i)) - 1;
      }

      class po_res {
          using inner_order =
              decltype (std::declval<const X&> ().partial_order (std::declval<const X&> ()));

        public:
          po_res (const x_and_wordvec& lhs, const x_and_wordvec& rhs)
            : lhs_x {lhs.x},
              rhs_x {rhs.x} {
            assert (lhs.bool_count == rhs.bool_count);
            assert (lhs.unused_trailing_bits_are_zero ());
            assert (rhs.unused_trailing_bits_are_zero ());

            // Note that we are putting the word-packed Boolean tail first in that comparison.
            bgeq = (lhs.sum >= rhs.sum);
            bleq = (lhs.sum <= rhs.sum);

            for (size_t i = 0; (bgeq or bleq) and i < lhs.bools.size (); ++i) {
              const uint64_t diff = lhs.bools[i] | rhs.bools[i];
              bgeq = bgeq and (diff == lhs.bools[i]);
              bleq = bleq and (diff == rhs.bools[i]);
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

      [[nodiscard]] auto partial_order (const x_and_wordvec& rhs) const {
        assert (rhs.k == k);
        assert (rhs.bool_count == bool_count);
        return po_res (*this, rhs);
      }

      bool operator== (const x_and_wordvec& rhs) const {
        return sum == rhs.sum and bool_count == rhs.bool_count and bools == rhs.bools and
               x == rhs.x;
      }

      bool operator!= (const x_and_wordvec& rhs) const {
        return sum != rhs.sum or bool_count != rhs.bool_count or bools != rhs.bools or x != rhs.x;
      }

      value_type operator[] (size_t i) const {
        if (i >= inner_size ())
          return static_cast<int> (tail_bit (i - inner_size ())) - 1;
        return x[i];
      }

      [[nodiscard]] x_and_wordvec meet (const x_and_wordvec& rhs) const {
        assert_compatible (rhs);
        std::vector<uint64_t> res (bools.size (), 0);
        for (size_t i = 0; i < bools.size (); ++i)
          res[i] = bools[i] & rhs.bools[i];
        return x_and_wordvec (k, bool_count, x.meet (rhs.x), std::move (res));
      }

      [[nodiscard]] x_and_wordvec join (const x_and_wordvec& rhs) const {
        assert_compatible (rhs);
        std::vector<uint64_t> res (bools.size (), 0);
        for (size_t i = 0; i < bools.size (); ++i)
          res[i] = bools[i] | rhs.bools[i];
        return x_and_wordvec (k, bool_count, x.join (rhs.x), std::move (res));
      }

      void meet_with (const x_and_wordvec& rhs) {
        assert_compatible (rhs);
        x.meet_with (rhs.x);
        for (size_t i = 0; i < bools.size (); ++i)
          bools[i] and_eq rhs.bools[i];
        clear_unused_bits ();
        sum = popcount ();
      }

      void join_with (const x_and_wordvec& rhs) {
        assert_compatible (rhs);
        x.join_with (rhs.x);
        for (size_t i = 0; i < bools.size (); ++i)
          bools[i] or_eq rhs.bools[i];
        clear_unused_bits ();
        sum = popcount ();
      }

      [[nodiscard]] long cached_sum () const
        requires requires (const X& inner) { inner.cached_sum (); }
      {
        return static_cast<long> (sum) + static_cast<long> (x.cached_sum ());
      }

      bool operator< (const x_and_wordvec& rhs) const {
        const size_t common_bits = std::min (bool_count, rhs.bool_count);
        for (size_t i = 0; i < words_for (common_bits); ++i) {
          uint64_t diff = bools[i] ^ rhs.bools[i];
          if (i + 1 == words_for (common_bits) and common_bits % bits_per_word != 0)
            diff and_eq low_bits_mask (common_bits % bits_per_word);
          if (diff != 0) {
            const unsigned int bit = std::countr_zero (diff);
            return (bools[i] & (uint64_t {1} << bit)) == 0;
          }
        }

        if (bool_count != rhs.bool_count)
          return bool_count < rhs.bool_count;
        return x < rhs.x;
      }

      [[nodiscard]] auto bin () const {
        auto tail_bin = sum;

        // Even if X doesn't have bin (), our local sum is valid, in that:
        //   if u dominates v, then in particular, it dominates it over the boolean part, so u.sum
        //   >= v.sum.
        if constexpr (has_bin<X>::value)
          tail_bin += x.bin ();

        return tail_bin;
      }

      std::ostream& print (std::ostream& os) const {
        os << "{ ";
        for (size_t i = 0; i < this->size (); ++i)
          os << (int) (*this)[i] << " ";
        os << "}";
        return os;
      }

      [[nodiscard]] bool unused_trailing_bits_are_zero () const {
        if (bools.empty () or bool_count % bits_per_word == 0)
          return true;
        return (bools.back () & ~low_bits_mask (bool_count % bits_per_word)) == 0;
      }

    private:
      static constexpr size_t bits_per_word = 64;

      [[nodiscard]] static constexpr size_t words_for (size_t bits) {
        return bits / bits_per_word + static_cast<size_t> (bits % bits_per_word != 0);
      }

      [[nodiscard]] static constexpr size_t word_for (size_t bit) { return bit / bits_per_word; }

      [[nodiscard]] static constexpr uint64_t mask_for (size_t bit) {
        return uint64_t {1} << (bit % bits_per_word);
      }

      [[nodiscard]] static constexpr uint64_t low_bits_mask (size_t bits) {
        assert (bits > 0 and bits < bits_per_word);
        return (uint64_t {1} << bits) - 1;
      }

      [[nodiscard]] size_t inner_size () const { return k - bool_count; }

      [[nodiscard]] bool tail_bit (size_t i) const {
        return (bools[word_for (i)] & mask_for (i)) != 0;
      }

      [[nodiscard]] size_t popcount () const {
        size_t result = 0;
        for (const uint64_t word : bools)
          result += static_cast<size_t> (std::popcount (word));
        return result;
      }

      // Every path that writes or accepts words calls this helper.  Keeping the unused high bits
      // of the last word clear makes whole-word order, equality, and popcount operations exact.
      void clear_unused_bits () {
        if (not bools.empty () and bool_count % bits_per_word != 0)
          bools.back () and_eq low_bits_mask (bool_count % bits_per_word);
        assert (unused_trailing_bits_are_zero ());
      }

      void assert_compatible (const x_and_wordvec& rhs) const {
        assert (rhs.k == k);
        assert (rhs.bool_count == bool_count);
        assert (rhs.bools.size () == bools.size ());
        assert (unused_trailing_bits_are_zero ());
        assert (rhs.unused_trailing_bits_are_zero ());
      }

      size_t k;
      X x;
      size_t bool_count;
      std::vector<uint64_t> bools;
      size_t sum;  // The sum of all active tail bits, seen as 0/1 values.
  };
}
