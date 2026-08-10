#pragma once

#include <type_traits>

#include <algorithm>
#include <cassert>
#include <compare>
#include <experimental/simd>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <posets/concepts.hh>
#include <posets/utils/simd_traits.hh>
#include <posets/vectors/generic_helpers.hh>
#include <posets/vectors/generic_partial_order.hh>

namespace posets::vectors {
  template <typename Data, bool HasSum, bool EmbedsData>
    requires HasData<Data>
  class generic : private sum_member<HasSum>, private malloc_member<EmbedsData, Data> {
    public:
      using block_type = typename Data::value_type;
      using value_type = block_type::value_type;
      static const auto uses_simd = [] () { return IsDataSimd<Data>; }();

      static const auto items_per_block = sizeof (block_type) / sizeof (value_type);

      static constexpr size_t blocks_for (size_t nelts) {
        return (nelts + items_per_block - 1) / items_per_block;
      }

    private:
      static const bool is_resizable = ([] () { return requires (Data d) { d.resize (42); }; }) ();

      static_assert (not is_resizable or EmbedsData,
                     "Resizable data should be embeded, as they are already managed by pointers.");

      block_type* data () {
        if constexpr (EmbedsData)
          return datap.data ();
        else
          return datap->data ();
      }

      [[nodiscard]] const block_type* data () const {
        if constexpr (EmbedsData)
          return datap.data ();
        else
          return datap->data ();
      }

      [[nodiscard]] size_t data_size () const {
        if constexpr (EmbedsData)
          return datap.size ();
        else
          return datap->size ();
      }

      [[noreturn, gnu::cold, gnu::noinline]] static void throw_fixed_capacity_exceeded () {
        throw std::length_error {"vector size exceeds fixed storage capacity"};
      }

      void clear_back () {
        if constexpr (is_resizable) {
          if (k % items_per_block != 0) {
            if constexpr (uses_simd)
              data ()[data_size () - 1] = block_type (0);
            else
              data ()[data_size () - 1].fill (0);
          }
        }
        else {
          if (k == data_size () * items_per_block)
            return;
          for (size_t block = k / items_per_block; block < data_size (); ++block)
            if constexpr (uses_simd)
              data ()[block] = block_type (0);
            else
              data ()[block].fill (0);
        }
      }

    public:
      generic (size_t k)
        requires is_resizable
        : k {k},
          datap {blocks_for (k)} {
        clear_back ();
      }

      generic (size_t k)
        requires (not is_resizable)
        : k {k} {
        if constexpr (not EmbedsData)
          datap = this->malloc.construct ();
        if (k > data_size () * items_per_block) [[unlikely]] {
          if constexpr (not EmbedsData) {
            this->malloc.destroy (datap);
            datap = nullptr;
          }
          throw_fixed_capacity_exceeded ();
        }
        assert (data_size () >= blocks_for (k));
        clear_back ();
      }

      generic (std::span<const value_type> v) : generic (v.size ()) {
        if constexpr (HasSum) {
          this->sum = 0;
          for (auto&& c : v)
            this->sum += c;
        }
        size_t offset = 0;
        for (; offset + items_per_block <= v.size (); offset += items_per_block) {
          const size_t block = offset / items_per_block;
          if constexpr (uses_simd)
            data ()[block].copy_from (v.data () + offset, std::experimental::element_aligned);
          else {
            block_type loaded;
            std::copy_n (v.data () + offset, items_per_block, loaded.begin ());
            data ()[block] = loaded;
          }
        }
        for (; offset < v.size (); ++offset)
          at (offset) = v[offset];
      }

      generic () = delete;
      generic (const generic& other) = delete;
      generic (generic&& other) noexcept : k {other.k}, datap {std::move (other.datap)} {
        if constexpr (not EmbedsData)
          other.datap = nullptr;
        if constexpr (HasSum)
          this->sum = other.sum;
      }

      ~generic () {
        if constexpr (not EmbedsData)
          if (datap)
            this->malloc.destroy (datap);
      }

      // explicit copy operator
      [[nodiscard]] generic copy () const {
        auto res = generic (k);
        for (size_t i = 0; i < data_size (); ++i)
          res.data ()[i] = data ()[i];
        if constexpr (HasSum)
          res.sum = this->sum;
        return res;
      }

      generic& operator= (generic&& other) noexcept {
        if constexpr (not EmbedsData)
          if (datap)
            this->malloc.destroy (datap);
        k = other.k;
        datap = other.datap;
        if constexpr (not EmbedsData)
          other.datap = nullptr;
        if constexpr (HasSum)
          this->sum = other.sum;

        return *this;
      }

      generic& operator= (const generic& other) = delete;

      void to_vector (std::span<value_type> v) const {
        assert (v.size () >= k);
        size_t offset = 0;
        for (; offset + items_per_block <= k; offset += items_per_block) {
          const size_t block = offset / items_per_block;
          if constexpr (uses_simd)
            data ()[block].copy_to (v.data () + offset, std::experimental::element_aligned);
          else {
            const block_type stored = data ()[block];
            std::copy_n (stored.begin (), items_per_block, v.data () + offset);
          }
        }
        for (; offset < k; ++offset)
          v[offset] = (*this)[offset];
      }

      [[nodiscard]] auto partial_order (const generic& rhs) const {
        return generic_partial_order (*this, rhs);
      }

      bool operator== (const generic& rhs) const {
        assert (k == rhs.k);
        if constexpr (HasSum)
          if (this->sum != rhs.sum)
            return false;
        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd) {
            if (not std::experimental::all_of (data ()[i] == rhs.data ()[i]))
              return false;
          }
          else if (data ()[i] != rhs.data ()[i])
            return false;
        }
        return true;
      }

      bool operator!= (const generic& rhs) const { return not(*this == rhs); }

      // Used by Sets, should be a total order.  Do not use.
      bool operator< (const generic& rhs) const {
        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd) {
            auto lhs_lt_rhs = data ()[i] < rhs.data ()[i];
            auto rhs_lt_lhs = rhs.data ()[i] < data ()[i];
            auto p1 = find_first_set (lhs_lt_rhs);
            auto p2 = find_first_set (rhs_lt_lhs);
            if (p1 == p2)
              continue;
            return (p1 < p2);
          }
          else {
            // This is the lexicographical order.
            auto order = data ()[i] <=> rhs.data ()[i];
            if (order == std::weak_ordering::equivalent)
              continue;
            return order == std::weak_ordering::less;
          }
        }
        return false;
      }

      [[nodiscard]] generic meet (const generic& rhs) const {
        auto res = generic (k);

        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd)
            res.data ()[i] = std::experimental::min (data ()[i], rhs.data ()[i]);
          else
            for (size_t j = 0; j < items_per_block; ++j)
              res.data ()[i][j] = std::min (data ()[i][j], rhs.data ()[i][j]);

          // In case of SIMD, this:
          //   res.sum += std::experimental::reduce (res.data ()[i]);
          // should NOT be used since this can lead to overflows over char.
          // instead, we manually loop through:
          if constexpr (HasSum)
            for (size_t j = 0; j < items_per_block; ++j)
              res.sum += res.data ()[i][j];
        }

        return res;
      }

      [[nodiscard]] generic join (const generic& rhs) const {
        auto res = generic (k);

        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd)
            res.data ()[i] = std::experimental::max (data ()[i], rhs.data ()[i]);
          else
            for (size_t j = 0; j < items_per_block; ++j)
              res.data ()[i][j] = std::max (data ()[i][j], rhs.data ()[i][j]);

          // SIMD reductions over narrow element types can overflow, so keep
          // the scalar accumulation used by meet().
          if constexpr (HasSum)
            for (size_t j = 0; j < items_per_block; ++j)
              res.sum += res.data ()[i][j];
        }

        return res;
      }

      void meet_with (const generic& rhs) {
        assert (k == rhs.k);
        int updated_sum = 0;  // NOLINT(misc-const-correctness): mutated only in HasSum variants.
        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd)
            data ()[i] = std::experimental::min (data ()[i], rhs.data ()[i]);
          else
            for (size_t j = 0; j < items_per_block; ++j)
              data ()[i][j] = std::min (data ()[i][j], rhs.data ()[i][j]);

          if constexpr (HasSum)
            for (size_t j = 0; j < items_per_block; ++j)
              updated_sum += data ()[i][j];
        }
        if constexpr (HasSum)
          this->sum = updated_sum;
      }

      void join_with (const generic& rhs) {
        assert (k == rhs.k);
        int updated_sum = 0;  // NOLINT(misc-const-correctness): mutated only in HasSum variants.
        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd)
            data ()[i] = std::experimental::max (data ()[i], rhs.data ()[i]);
          else
            for (size_t j = 0; j < items_per_block; ++j)
              data ()[i][j] = std::max (data ()[i][j], rhs.data ()[i][j]);

          if constexpr (HasSum)
            for (size_t j = 0; j < items_per_block; ++j)
              updated_sum += data ()[i][j];
        }
        if constexpr (HasSum)
          this->sum = updated_sum;
      }

      [[nodiscard]] auto size () const { return k; }

      auto& print (std::ostream& os) const {
        os << "{ ";
        for (size_t i = 0; i < k; ++i)
          os << (int) (*this)[i] << " ";
        os << "}";
        return os;
      }

    private:
      decltype (auto) at (size_t i) { return data ()[i / items_per_block][i % items_per_block]; }

      [[nodiscard]] decltype (auto) at (size_t i) const {
        return data ()[i / items_per_block][i % items_per_block];
      }

    public:
      [[nodiscard]] value_type operator[] (size_t i) const { return at (i); }

      template <bool IsConst>
      class basic_iterator {
          template <bool>
          friend class basic_iterator;

          using owner_type = std::conditional_t<IsConst, const generic, generic>;

        public:
          using iterator_concept = std::random_access_iterator_tag;
          using iterator_category = std::random_access_iterator_tag;
          using value_type = typename generic::value_type;
          using difference_type = std::ptrdiff_t;
          using reference = decltype (std::declval<owner_type&> ().at (size_t {}));
          using pointer = void;

          basic_iterator () = default;
          basic_iterator (owner_type* owner, difference_type position)
            : owner {owner},
              position {position} {}

          basic_iterator (const basic_iterator<false>& other)
            requires IsConst
            : owner {other.owner},
              position {other.position} {}

          reference operator* () const { return owner->at (static_cast<size_t> (position)); }
          reference operator[] (difference_type offset) const { return *(*this + offset); }

          basic_iterator& operator++ () {
            ++position;
            return *this;
          }
          basic_iterator operator++ (int) {
            auto previous = *this;
            ++*this;
            return previous;
          }
          basic_iterator& operator-- () {
            --position;
            return *this;
          }
          basic_iterator operator-- (int) {
            auto previous = *this;
            --*this;
            return previous;
          }
          basic_iterator& operator+= (difference_type offset) {
            position += offset;
            return *this;
          }
          basic_iterator& operator-= (difference_type offset) {
            position -= offset;
            return *this;
          }

          friend basic_iterator operator+ (basic_iterator iterator, difference_type offset) {
            iterator += offset;
            return iterator;
          }
          friend basic_iterator operator+ (difference_type offset, basic_iterator iterator) {
            return iterator + offset;
          }
          friend basic_iterator operator- (basic_iterator iterator, difference_type offset) {
            iterator -= offset;
            return iterator;
          }
          friend difference_type operator- (const basic_iterator& lhs, const basic_iterator& rhs) {
            assert (lhs.owner == rhs.owner);
            return lhs.position - rhs.position;
          }
          friend bool operator== (const basic_iterator&, const basic_iterator&) = default;
          friend auto operator<=> (const basic_iterator& lhs, const basic_iterator& rhs) {
            assert (lhs.owner == rhs.owner);
            return lhs.position <=> rhs.position;
          }

        private:
          owner_type* owner {nullptr};
          difference_type position {0};
      };

      using iterator = basic_iterator<false>;
      using const_iterator = basic_iterator<true>;

      iterator begin () { return iterator (this, 0); }
      [[nodiscard]] const_iterator begin () const { return const_iterator (this, 0); }
      iterator end () { return iterator (this, static_cast<std::ptrdiff_t> (k)); }
      [[nodiscard]] const_iterator end () const {
        return const_iterator (this, static_cast<std::ptrdiff_t> (k));
      }

      [[nodiscard]] auto bin () const {
        if constexpr (HasSum)
          return std::abs (this->sum) / k;
        else
          return std::abs ((*this)[0]) / k;  // NOLINT(clang-diagnostic-absolute-value)
      }

      [[nodiscard]] int cached_sum () const
        requires HasSum
      {
        return this->sum;
      }

    private:
      friend generic_partial_order<generic>;

      // template <template <typename> typename D, typename T>
      // requires IsGenericVector<D<T>> or IsGenericVector<D<T, 42>>
      // friend struct traits<D, T>;

      size_t k;
      std::conditional_t<EmbedsData, Data, Data*> datap;
  };
}
