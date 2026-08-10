#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>

namespace posets::downsets {
  template <Vector V>
  class rank_bucketed_vector_backed {
      using self = rank_bucketed_vector_backed;
      using rank_t = std::int64_t;

    public:
      using value_type = V;

      rank_bucketed_vector_backed (V&& v) { insert (std::move (v)); }

      rank_bucketed_vector_backed (std::vector<V>&& elements) noexcept {
        assert (not elements.empty ());
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      struct bucket {
          rank_t rank;
          size_t begin;
          size_t end;
      };

      rank_bucketed_vector_backed () = default;

      std::vector<V> vector_set;
      std::vector<rank_t> ranks;
      std::vector<bucket> buckets;

      static rank_t rank_of (const V& v) {
        rank_t sum = 0;
        for (size_t i = 0; i < v.size (); ++i)
          sum += static_cast<rank_t> (v[i]);
        return sum;
      }

      void rebuild_buckets () {
        buckets.clear ();
        if (vector_set.empty ())
          return;

        size_t begin = 0;
        while (begin < ranks.size ()) {
          size_t end = begin + 1;
          while (end < ranks.size () and ranks[end] == ranks[begin])
            ++end;
          buckets.push_back (bucket {ranks[begin], begin, end});
          begin = end;
        }
      }

      [[nodiscard]] size_t lower_rank_pos (rank_t r) const {
        return static_cast<size_t> (std::ranges::lower_bound (ranks, r) - ranks.begin ());
      }

      [[nodiscard]] size_t upper_rank_pos (rank_t r) const {
        return static_cast<size_t> (std::ranges::upper_bound (ranks, r) - ranks.begin ());
      }

    public:
      rank_bucketed_vector_backed (const self&) = delete;
      rank_bucketed_vector_backed (self&&) = default;
      self& operator= (const self&) = delete;
      self& operator= (self&&) = default;

      bool operator== (const self&) = delete;

      [[nodiscard]] bool contains (const V& v) const {
        const rank_t rv = rank_of (v);
        const size_t start = lower_rank_pos (rv);

        for (size_t i = start; i < vector_set.size (); ++i)
          if (v.partial_order (vector_set[i]).leq ())
            return true;
        return false;
      }

      [[nodiscard]] auto size () const { return vector_set.size (); }

      bool insert (V&& v) {
        const rank_t rv = rank_of (v);

        const size_t first_possible_dominator = lower_rank_pos (rv);
        for (size_t i = first_possible_dominator; i < vector_set.size (); ++i)
          if (v.partial_order (vector_set[i]).leq ())
            return false;

        const size_t stop = upper_rank_pos (rv);
        size_t write = 0;
        bool removed_any = false;

        for (size_t read = 0; read < vector_set.size (); ++read) {
          bool remove = false;
          if (read < stop)
            remove = v.partial_order (vector_set[read]).geq ();

          if (remove) {
            removed_any = true;
            continue;
          }

          if (write != read) {
            vector_set[write] = std::move (vector_set[read]);
            ranks[write] = ranks[read];
          }
          ++write;
        }

        if (removed_any) {
          vector_set.erase (vector_set.begin () + static_cast<std::ptrdiff_t> (write),
                            vector_set.end ());
          ranks.erase (ranks.begin () + static_cast<std::ptrdiff_t> (write), ranks.end ());
        }

        const size_t pos = lower_rank_pos (rv);
        vector_set.insert (vector_set.begin () + static_cast<std::ptrdiff_t> (pos), std::move (v));
        ranks.insert (ranks.begin () + static_cast<std::ptrdiff_t> (pos), rv);
        rebuild_buckets ();
        return true;
      }

      void union_with (self&& other) {
        for (auto&& e : other.vector_set)
          insert (std::move (e));
      }

      void intersect_with (const self& other) {
        self intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          const bool dominated = other.contains (x);
          if (dominated)
            intersection.insert (x.copy ());
          else
            for (const auto& y : other)
              intersection.insert (x.meet (y));

          smaller_set or_eq not dominated;
        }

        if (smaller_set)
          *this = std::move (intersection);
      }

      template <typename F>
      [[nodiscard]] self apply (const F& lambda) const {
        self res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }

      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const rank_bucketed_vector_backed<V>& f) {
    for (const auto& el : f)
      os << el << '\n';
    return os;
  }
}
