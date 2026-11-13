#pragma once

#include <type_traits>

#include <algorithm>
#include <array>
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
  class generic : private sum_member<HasSum, typename Data::value_type::value_type>,
                  private malloc_member<EmbedsData, Data> {
    public:
      using block_type = Data::value_type;
      using value_type = block_type::value_type;
      static_assert (not HasSum or utils::EXACT_SUM_COORDINATE<value_type>,
                     "cached sums require signed integral coordinates of at most 64 bits");
      static constexpr auto uses_simd = IsDataSimd<Data>;

      static constexpr auto items_per_block = [] () {
        if constexpr (uses_simd)
          return block_type::size ();
        else
          return sizeof (block_type) / sizeof (value_type);
      }();

      static constexpr size_t blocks_for (size_t nelts) {
        return (nelts / items_per_block) + static_cast<size_t> (nelts % items_per_block != 0);
      }

    private:
      static constexpr bool is_resizable = requires (Data d) { d.resize (42); };

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

      static size_t checked_dimension (size_t k) {
        if constexpr (HasSum)
          utils::check_sum_dimension<value_type> (k);
        return k;
      }

      void clear_back () {
        // Cached vectors start at zero; uncached vectors only need zero padding.
        for (size_t block = HasSum ? 0 : k / items_per_block; block < data_size (); ++block)
          if constexpr (uses_simd)
            data ()[block] = block_type (0);
          else
            data ()[block].fill (0);
      }

    public:
      generic (size_t k)
        requires is_resizable
        : k {checked_dimension (k)},
          datap {blocks_for (this->k)} {
        clear_back ();
      }

      generic (size_t k)
        requires (not is_resizable)
        : k {checked_dimension (k)} {
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
        if constexpr (uses_simd) {
          if (offset < v.size ()) {
            std::array<value_type, items_per_block> tail {};
            std::copy_n (v.data () + offset, v.size () - offset, tail.data ());
            data ()[offset / items_per_block].copy_from (tail.data (),
                                                         std::experimental::element_aligned);
          }
        }
        else
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
        if (this == &other)
          return *this;
        if constexpr (not EmbedsData)
          if (datap)
            this->malloc.destroy (datap);
        k = other.k;
        datap = std::move (other.datap);
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
            const auto different = data ()[i] != rhs.data ()[i];
            if (std::experimental::none_of (different))
              continue;
            // find_first_set has a nonempty-mask precondition.
            const auto first = std::experimental::find_first_set (different);
            return data ()[i][first] < rhs.data ()[i][first];
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
        // NOLINTNEXTLINE(misc-const-correctness): mutated only in HasSum variants.
        utils::sum_t<value_type> updated_sum = 0;
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
        // NOLINTNEXTLINE(misc-const-correctness): mutated only in HasSum variants.
        utils::sum_t<value_type> updated_sum = 0;
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
          os << static_cast<int> ((*this)[i]) << " ";
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

          using owner_type = std::conditional_t<IsConst or HasSum, const generic, generic>;

        public:
          using iterator_concept = std::random_access_iterator_tag;
          using iterator_category = std::random_access_iterator_tag;
          using value_type = generic::value_type;
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

      using iterator = basic_iterator<HasSum>;
      using const_iterator = basic_iterator<true>;

      iterator begin () { return iterator (this, 0); }
      [[nodiscard]] const_iterator begin () const { return const_iterator (this, 0); }
      iterator end () { return iterator (this, static_cast<std::ptrdiff_t> (k)); }
      [[nodiscard]] const_iterator end () const {
        return const_iterator (this, static_cast<std::ptrdiff_t> (k));
      }

      template <typename Index = size_t>
      [[nodiscard]] Index bin () const {
        static_assert (std::is_integral_v<Index> and std::is_unsigned_v<Index> and
                       std::numeric_limits<Index>::digits <= 64);
        // Clamping to zero preserves monotonicity on signed coordinates and
        // representable old bins on nonnegative inputs. Saturation preserves
        // order when the quotient exceeds Index. Nonintegral vectors use one bin.
        if constexpr (utils::EXACT_SUM_COORDINATE<value_type>) {
          if (k == 0)
            return 0;
          const utils::sum_t<value_type> key = [this] {
            if constexpr (HasSum)
              return this->sum;
            else
              return static_cast<utils::sum_t<value_type>> ((*this)[0]);
          }();
          if (key <= 0)
            return 0;
          const auto quotient = key / k;
          if (quotient >= std::numeric_limits<Index>::max ())
            return std::numeric_limits<Index>::max ();
          return static_cast<Index> (quotient);
        }
        return 0;
      }

      [[nodiscard]] utils::sum_t<value_type> cached_sum () const
        requires HasSum
      {
        return this->sum;
      }

    private:
      friend generic_partial_order<generic>;

      size_t k;
      std::conditional_t<EmbedsData, Data, Data*> datap;
  };
}
