#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>

#ifndef POSETS_FILTERED_VECTOR_MIN_SIZE
# define POSETS_FILTERED_VECTOR_MIN_SIZE 128
#endif

#ifndef POSETS_FILTERED_VECTOR_DIMS
# define POSETS_FILTERED_VECTOR_DIMS 6
#endif

#ifndef POSETS_FILTERED_VECTOR_MAX_THRESHOLDS_PER_DIM
# define POSETS_FILTERED_VECTOR_MAX_THRESHOLDS_PER_DIM 256
#endif

namespace posets::downsets {
  template <Vector V>
  class filtered_vector_backed {
      using self = filtered_vector_backed;
      using coord_t = V::value_type;
      using word_t = std::uint64_t;
      static constexpr size_t word_bits = 64;

    public:
      using value_type = V;

      filtered_vector_backed (V&& v) { insert (std::move (v)); }

      filtered_vector_backed (std::vector<V>&& elements) noexcept {
        assert (not elements.empty ());
        for (auto&& e : elements)
          insert (std::move (e));
        filter_dirty = true;
      }

    private:
      struct dim_filter {
          size_t dim;
          std::vector<coord_t> thresholds;
          std::vector<std::vector<word_t>> bits_ge;
      };

      struct dim_score {
          size_t dim;
          size_t distinct;
      };

      filtered_vector_backed () = default;

      std::vector<V> vector_set;
      mutable std::vector<dim_filter> filters;
      mutable bool filter_dirty = true;

      static size_t words_for (size_t nbits) { return (nbits + word_bits - 1) / word_bits; }

      static void set_bit (std::vector<word_t>& bits, size_t idx) {
        bits[idx / word_bits] or_eq (word_t {1} << (idx % word_bits));
      }

      static std::vector<word_t> all_ones (size_t nbits) {
        std::vector<word_t> bits (words_for (nbits), ~word_t {0});
        const size_t excess = (bits.size () * word_bits) - nbits;
        if (excess != 0)
          bits.back () >>= excess;
        return bits;
      }

      static bool empty_bits (const std::vector<word_t>& bits) {
        return std::ranges::all_of (bits, [] (word_t w) { return w == 0; });
      }

      static void and_into (std::vector<word_t>& dst, const std::vector<word_t>& src) {
        assert (dst.size () == src.size ());
        for (size_t i = 0; i < dst.size (); ++i)
          dst[i] and_eq src[i];
      }

      [[nodiscard]] bool contains_linear (const V& v) const {
        return std::ranges::any_of (vector_set,
                                    [&v] (const V& e) { return v.partial_order (e).leq (); });
      }

      void build_one_dim_filter (size_t dim) const {
        const size_t n = vector_set.size ();

        dim_filter filter;
        filter.dim = dim;
        filter.thresholds.reserve (n);

        for (const auto& v : vector_set)
          filter.thresholds.push_back (v[dim]);

        std::sort (filter.thresholds.begin (), filter.thresholds.end ());
        filter.thresholds.erase (
            std::unique (filter.thresholds.begin (), filter.thresholds.end ()),
            filter.thresholds.end ());

        const size_t nt = filter.thresholds.size ();
        filter.bits_ge.assign (nt, std::vector<word_t> (words_for (n), word_t {0}));

        for (size_t threshold_idx = 0; threshold_idx < nt; ++threshold_idx) {
          const coord_t threshold = filter.thresholds[threshold_idx];
          for (size_t vector_idx = 0; vector_idx < n; ++vector_idx)
            if (vector_set[vector_idx][dim] >= threshold)
              set_bit (filter.bits_ge[threshold_idx], vector_idx);
        }

        filters.push_back (std::move (filter));
      }

      void rebuild_filter () const {
        filters.clear ();
        filter_dirty = false;

        const size_t n = vector_set.size ();
        if (n < POSETS_FILTERED_VECTOR_MIN_SIZE)
          return;

        assert (not vector_set.empty ());
        const size_t k = vector_set[0].size ();
        std::vector<dim_score> scores;

        std::vector<coord_t> vals;
        vals.reserve (n);

        for (size_t dim = 0; dim < k; ++dim) {
          vals.clear ();
          for (const auto& v : vector_set)
            vals.push_back (v[dim]);

          std::sort (vals.begin (), vals.end ());
          vals.erase (std::unique (vals.begin (), vals.end ()), vals.end ());

          if (vals.size () <= 1)
            continue;
          if (vals.size () > POSETS_FILTERED_VECTOR_MAX_THRESHOLDS_PER_DIM)
            continue;

          scores.push_back (dim_score {dim, vals.size ()});
        }

        std::sort (scores.begin (), scores.end (),
                   [] (const dim_score& lhs, const dim_score& rhs) {
                     if (lhs.distinct != rhs.distinct)
                       return lhs.distinct > rhs.distinct;
                     return lhs.dim < rhs.dim;
                   });

        if (scores.size () > POSETS_FILTERED_VECTOR_DIMS)
          scores.resize (POSETS_FILTERED_VECTOR_DIMS);

        for (const auto& score : scores)
          build_one_dim_filter (score.dim);
      }

      void ensure_filter () const {
        if (filter_dirty)
          rebuild_filter ();
      }

    public:
      filtered_vector_backed (const self&) = delete;
      filtered_vector_backed (self&&) = default;
      self& operator= (const self&) = delete;
      self& operator= (self&&) = default;

      bool operator== (const self&) = delete;

      [[nodiscard]] bool contains (const V& v) const {
        if (vector_set.size () < POSETS_FILTERED_VECTOR_MIN_SIZE)
          return contains_linear (v);

        ensure_filter ();
        if (filters.empty ())
          return contains_linear (v);

        std::vector<word_t> candidates = all_ones (vector_set.size ());

        for (const auto& filter : filters) {
          const coord_t qv = v[filter.dim];
          auto it = std::ranges::lower_bound (filter.thresholds, qv);
          if (it == filter.thresholds.end ())
            return false;

          const auto threshold_idx = static_cast<size_t> (it - filter.thresholds.begin ());
          and_into (candidates, filter.bits_ge[threshold_idx]);
          if (empty_bits (candidates))
            return false;
        }

        for (size_t word_idx = 0; word_idx < candidates.size (); ++word_idx) {
          word_t word = candidates[word_idx];
          while (word != 0) {
            const unsigned bit = std::countr_zero (word);
            const size_t idx = (word_idx * word_bits) + bit;
            if (idx < vector_set.size () and v.partial_order (vector_set[idx]).leq ())
              return true;
            word and_eq word - 1;
          }
        }

        return false;
      }

      [[nodiscard]] auto size () const { return vector_set.size (); }

      bool insert (V&& v) {
        bool must_remove = false;

        auto result = vector_set.begin ();
        auto end = vector_set.end ();

        for (auto it = result; it != end; ++it) {
          auto res = v.partial_order (*it);
          if (not must_remove and res.leq ())
            return false;

          if (res.geq ())
            must_remove = true;
          else {
            if (result != it)
              *result = std::move (*it);
            ++result;
          }
        }

        if (result != vector_set.end ())
          vector_set.erase (result, vector_set.end ());

        vector_set.push_back (std::move (v));
        filter_dirty = true;
        return true;
      }

      void union_with (self&& other) {
        for (auto&& e : other.vector_set)
          insert (std::move (e));
        filter_dirty = true;
      }

      void intersect_with (const self& other) {
        self intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          bool dominated = false;

          for (const auto& y : other.vector_set) {
            V v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            if (dominated)
              break;
          }

          smaller_set or_eq not dominated;
        }

        if (smaller_set) {
          vector_set = std::move (intersection.vector_set);
          filter_dirty = true;
        }
      }

      template <typename F>
      self apply (const F& lambda) const {
        self res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        res.filter_dirty = true;
        return res;
      }

      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }

      [[nodiscard]] auto& get_backing_vector () {
        filter_dirty = true;
        return vector_set;
      }

      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const filtered_vector_backed<V>& f) {
    for (const auto& el : f)
      os << el << '\n';
    return os;
  }
}
