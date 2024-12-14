#pragma once

#include <cassert>
#include <cstring>
#include <experimental/simd>
#include <iostream>

#include <posets/concepts.hh>
#include <posets/utils/simd_traits.hh>
#include <posets/vectors/generic_helpers.hh>
#include <posets/vectors/generic_partial_order.hh>

namespace posets::vectors {
  template <typename Data, bool has_sum, bool embeds_data>
    requires HasData<Data>
  class generic : private sum_member<has_sum>, private malloc_member<embeds_data, Data> {
    public:
      using block_type = typename Data::value_type;
      using value_type = block_type::value_type;
      static const auto uses_simd = [] () {
        if constexpr (IsDataSimd<Data>)
          return true;
        else
          return false;
      }();

    public:
      static const auto items_per_block = sizeof (block_type) / sizeof (value_type);

      static constexpr size_t blocks_for (size_t nelts) {
        return (nelts + items_per_block - 1) / items_per_block;
      }

    private:
      static const bool is_resizable = ([] () {
        if constexpr (requires (Data d) { d.resize (42); })
          return true;
        else
          return false;
      }) ();

      static_assert (not(is_resizable and not embeds_data),
                     "Resizable data should be embeded, as they are already managed by pointers.");

      block_type* data () {
        if constexpr (embeds_data)
          return _data.data ();
        else
          return _data->data ();
      }

      const block_type* data () const {
        if constexpr (embeds_data)
          return _data.data ();
        else
          return _data->data ();
      }

      size_t data_size () const {
        if constexpr (embeds_data)
          return _data.size ();
        else
          return _data->size ();
      }

      void clear_back () {
        if (data_size () > blocks_for (k) or k % items_per_block) {
          char* start = reinterpret_cast<char*> (data () + (k / items_per_block));
          char* end = reinterpret_cast<char*> (data () + data_size ());
          bzero (start, end - start);
        }
      }

    public:
      generic (size_t k)
        requires is_resizable
        : k {k},
          _data {blocks_for (k)} {
        clear_back ();
      }

      generic (size_t k)
        requires (not is_resizable)
        : k {k} {
        if constexpr (not embeds_data)
          _data = this->malloc.construct ();
        assert (data_size () >= blocks_for (k));
        clear_back ();
      }

      generic (std::span<const value_type> v) : generic (v.size ()) {
        if constexpr (has_sum) {
          this->sum = 0;
          for (auto&& c : v)
            this->sum += c;
        }
        std::memcpy (reinterpret_cast<value_type*> (data ()), v.data (),
                     v.size () * sizeof (value_type));
      }

      generic () = delete;
      generic (const generic& other) = delete;
      generic (generic&& other) : k {other.k}, _data {other._data} {
        if constexpr (not embeds_data)
          other._data = nullptr;
        if constexpr (has_sum)
          this->sum = other.sum;
      }

      ~generic () {
        if constexpr (not embeds_data)
          if (_data)
            this->malloc.destroy (_data);
      }

      // explicit copy operator
      generic copy () const {
        if constexpr (embeds_data) {
          auto res = generic (k);
          res._data = _data;
          if constexpr (has_sum)
            res.sum = this->sum;
          return res;
        }
        else {
          auto res = generic (std::span ((value_type*) _data, k));
          if constexpr (has_sum)
            res.sum = this->sum;
          return res;
        }
      }

      generic& operator= (generic&& other) {
        if constexpr (not embeds_data)
          if (_data)
            this->malloc.destroy (_data);
        k = other.k;
        _data = other._data;
        if constexpr (not embeds_data)
          other._data = nullptr;
        if constexpr (has_sum)
          this->sum = other.sum;

        return *this;
      }

      generic& operator= (const generic& other) = delete;

      void to_vector (std::span<value_type> v) const {
        assert (v.size () >= k);
        std::memcpy (v.data (), data (), k * sizeof (value_type));
      }

      inline auto partial_order (const generic& rhs) const {
        return generic_partial_order (*this, rhs);
      }

      bool operator== (const generic& rhs) const {
        if constexpr (has_sum)
          if (this->sum != rhs.sum)
            return false;
        // Trust memcmp to DTRT
        return std::memcmp (rhs.data (), data (), k * sizeof (value_type)) == 0;
      }

      bool operator!= (const generic& rhs) const {
        if constexpr (has_sum)
          if (this->sum != rhs.sum)
            return true;
        // Trust memcmp to DTRT
        return std::memcmp (rhs.data (), data (), k * sizeof (value_type)) != 0;
      }

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

      generic meet (const generic& rhs) const {
        auto res = generic (k);
        if constexpr (not embeds_data)
          res._data = this->malloc.construct ();

        for (size_t i = 0; i < data_size (); ++i) {
          if constexpr (uses_simd)
            res.data ()[i] = std::experimental::min (data ()[i], rhs.data ()[i]);
          else
            for (size_t j = 0; j < items_per_block; ++j) {
              auto pos = i * items_per_block + j;
              res.at (pos) = std::min ((*this)[pos], rhs[pos]);
            }

          // In case of SIMD, this:
          //   res.sum += std::experimental::reduce (res.data ()[i]);
          // should NOT be used since this can lead to overflows over char.
          // instead, we manually loop through:
          if constexpr (has_sum)
            for (size_t j = 0; j < items_per_block; ++j)
              res.sum += res.data ()[i][j];
        }

        return res;
      }

      auto size () const { return k; }

      auto& print (std::ostream& os) const {
        os << "{ ";
        for (size_t i = 0; i < k; ++i)
          os << (int) (*this)[i] << " ";
        os << "}";
        return os;
      }

    private:
      value_type& at (size_t i) { return *(reinterpret_cast<value_type*> (data ()) + i); }

      const value_type& at (size_t i) const {
        return *(reinterpret_cast<const value_type*> (data ()) + i);
      }

    public:
      const value_type& operator[] (size_t i) const { return at (i); }

      using iterator = value_type*;
      using const_iterator = const value_type*;

      iterator begin () { return iterator (data ()); }
      const_iterator begin () const { return const_iterator (data ()); }
      iterator end () { return iterator (data ()) + k; }
      const_iterator end () const { return const_iterator (data ()) + k; }

    public:
      auto bin () const {
        if constexpr (has_sum)
          return std::abs (this->sum) / k;
        else
          return std::abs ((*this)[0]) / k;
      }

    private:
      friend generic_partial_order<generic>;

      // template <template <typename> typename D, typename T>
      // requires IsGenericVector<D<T>> or IsGenericVector<D<T, 42>>
      // friend struct traits<D, T>;

      size_t k;
      std::conditional_t<embeds_data, Data, Data*> _data;
  };
}
