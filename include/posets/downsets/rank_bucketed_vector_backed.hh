#pragma once

#include <type_traits>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/rank_stats.hh>
#include <posets/utils/sum.hh>

namespace posets::downsets {
  template <Vector V>
  class rank_bucketed_vector_backed {
      // Indexed generators share a possibly zero dimension. Parallel arrays have
      // equal sizes, ranks[i] == rank_of(vector_set[i]), and nondecreasing ranks.
      // Const Vector operations preserve sources; copy() also preserves values.
      // Integral ranks are exact throughout utils::vector_sum_t<V>'s documented domain.
      // Nonintegral coordinates use rank zero and inclusive comparisons.
      // External raw mutation still invalidates the index.
      using self = rank_bucketed_vector_backed;
      using rank_t = utils::vector_sum_t<V>;
      static_assert (not std::is_integral_v<typename V::value_type> or
                     utils::EXACT_SUM_COORDINATE<typename V::value_type>);

    public:
      using value_type = V;

      rank_bucketed_vector_backed (V&& v) { insert (std::move (v)); }

      // Construction may consume an input prefix before an exception propagates.
      rank_bucketed_vector_backed (std::vector<V>&& elements) {
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      rank_bucketed_vector_backed () = default;

      std::vector<V> vector_set;
      std::vector<rank_t> ranks;
      static constexpr bool safe_moves =
          std::is_nothrow_move_constructible_v<V> and std::is_nothrow_move_assignable_v<V>;

      [[nodiscard]] static rank_t rank_of (const V& v) {
        if constexpr (utils::EXACT_SUM_COORDINATE<typename V::value_type>)
          return utils::coordinate_sum (v);
        else
          return 0;
      }

      [[nodiscard]] size_t lower_rank_pos (rank_t r) const {
        return static_cast<size_t> (std::ranges::lower_bound (ranks, r) - ranks.begin ());
      }

      [[nodiscard]] size_t upper_rank_pos (rank_t r) const {
        return static_cast<size_t> (std::ranges::upper_bound (ranks, r) - ranks.begin ());
      }

      [[nodiscard]] static constexpr bool exact_rank_domain () {
        return utils::EXACT_SUM_COORDINATE<typename V::value_type>;
      }

      template <typename T>
      static void reserve_growing (std::vector<T>& values, size_t required) {
        if (required > values.capacity ()) {
          const size_t capacity = values.capacity ();
          const size_t grown =
              capacity > values.max_size () / 2 ? values.max_size () : capacity * 2;
          values.reserve (std::max (required, grown));
        }
      }

      [[nodiscard]] bool contains_impl (const V& v, rank_t rv) const {
        const size_t start = lower_rank_pos (rv);

        // A dominator w of v has v <= w componentwise, so rank (w) >= rank (v):
        // the scan can start at the first element of rank rv, and there is no
        // upper bound to stop it early.
        for (size_t i = start; i < vector_set.size (); ++i) {
          POSETS_RANK_STAT_ADD (partial_order_calls, 1);
          if (v.partial_order (vector_set[i]).leq ())
            return true;
        }
        return false;
      }

      [[nodiscard]] bool contains_with_rank (const V& v, rank_t rv) const {
        POSETS_RANK_STAT_ADD (contains_calls, 1);
        return contains_impl (v, rv);
      }

      void insert_accepted (V&& v, rank_t rv, size_t first_possible_dominator) {
        // Equal integral sums imply equality for comparable vectors only in a
        // proven exact domain. Nonintegral coordinates keep the inclusive fallback.
        const size_t stop = exact_rank_domain () ? first_possible_dominator : upper_rank_pos (rv);
        size_t first_removed = 0;
        for (; first_removed < stop; ++first_removed) {
          POSETS_RANK_STAT_ADD (partial_order_calls, 1);
          if (v.partial_order (vector_set[first_removed]).geq ()) {
            POSETS_RANK_STAT_ADD (existing_elements_removed, 1);
            break;
          }
        }
        // With a victim, the final insertion fits existing capacities. Otherwise
        // paired growth precedes mutation (the first reserve may change addresses).
        if (first_removed == stop) {
          reserve_growing (vector_set, vector_set.size () + 1);
          reserve_growing (ranks, ranks.size () + 1);
        }
        bool mutated = false;
        try {
          size_t write = first_removed;
          auto removed_before = static_cast<size_t> (first_removed < first_possible_dominator);
          for (size_t read = first_removed + 1; read < stop; ++read) {
            POSETS_RANK_STAT_ADD (partial_order_calls, 1);
            if (v.partial_order (vector_set[read]).geq ()) {
              removed_before += static_cast<size_t> (read < first_possible_dominator);
              POSETS_RANK_STAT_ADD (existing_elements_removed, 1);
              continue;
            }
            if (write != read) {
              mutated = true;
              vector_set[write] = std::move (vector_set[read]);
              ranks[write] = ranks[read];
            }
            ++write;
          }
          if (write != stop) {
            for (size_t read = stop; read < vector_set.size (); ++read) {
              mutated = true;
              vector_set[write] = std::move (vector_set[read]);
              ranks[write++] = ranks[read];
            }
            mutated = true;
            vector_set.erase (vector_set.begin () + static_cast<std::ptrdiff_t> (write),
                              vector_set.end ());
            ranks.erase (ranks.begin () + static_cast<std::ptrdiff_t> (write), ranks.end ());
          }
          const size_t pos = first_possible_dominator - removed_before;
          mutated = true;
          vector_set.insert (vector_set.begin () + static_cast<std::ptrdiff_t> (pos),
                             std::move (v));
          ranks.insert (ranks.begin () + static_cast<std::ptrdiff_t> (pos), rv);
        } catch (...) {
          // A custom comparison may throw after compaction started. Keep both
          // arrays valid and reusable; this is a basic, valid-empty guarantee.
          if (mutated) {
            vector_set.clear ();
            ranks.clear ();
          }
          throw;
        }
        POSETS_RANK_STAT_SIZE (vector_set.size ());
      }

      bool insert_impl (V&& v, rank_t rv) {
        const size_t first_possible_dominator = lower_rank_pos (rv);
        for (size_t i = first_possible_dominator; i < vector_set.size (); ++i) {
          POSETS_RANK_STAT_ADD (partial_order_calls, 1);
          if (v.partial_order (vector_set[i]).leq ()) {
            POSETS_RANK_STAT_ADD (insert_rejected_dominated, 1);
            return false;
          }
        }

        if constexpr (safe_moves) {
          insert_accepted (std::move (v), rv, first_possible_dominator);
        }
        else {
          // Throwing moves operate on copies, never on the live arrays.
          self replacement;
          replacement.ranks = ranks;
          for (const auto& old : vector_set)
            replacement.vector_set.push_back (old.copy ());
          replacement.insert_accepted (std::move (v), rv, first_possible_dominator);
          vector_set.swap (replacement.vector_set);
          ranks.swap (replacement.ranks);
        }
        return true;
      }

      bool insert_with_rank (V&& v, rank_t rv) {
        POSETS_RANK_STAT_ADD (insert_attempts, 1);
        return insert_impl (std::move (v), rv);
      }

    public:
      rank_bucketed_vector_backed (const self&) = delete;
      rank_bucketed_vector_backed (self&&) = default;
      self& operator= (const self&) = delete;
      self& operator= (self&&) = default;

      bool operator== (const self&) = delete;

      [[nodiscard]] bool contains (const V& v) const {
        POSETS_RANK_STAT_ADD (contains_calls, 1);
        return contains_impl (v, rank_of (v));
      }

      [[nodiscard]] auto size () const { return vector_set.size (); }

      // Growth/rank/rejection failures preserve logical contents for noexcept
      // moves; earlier successful preflight reserves may change capacities and
      // invalidate iterator addresses even on failure. Throwing moves are staged;
      // a custom comparison exception after live compaction leaves a valid empty
      // set. The rvalue may be consumed.
      bool insert (V&& v) {
        POSETS_RANK_STAT_ADD (insert_attempts, 1);
        return insert_impl (std::move (v), rank_of (v));
      }

      // RHS is consumed, including on partial failure: only destroy/reassign it
      // afterwards. Whole-union rollback is not promised.
      void union_with (self&& other) {
        if (this == &other)
          return;
        POSETS_RANK_STAT_ADD (union_insertions, other.vector_set.size ());
        for (size_t i = 0; i < other.vector_set.size (); ++i)
          insert_with_rank (std::move (other.vector_set[i]), other.ranks[i]);
      }

      void intersect_with (const self& other) {
        POSETS_RANK_STAT_ADD (intersection_left_elements, vector_set.size ());
        if (this == &other) {
          POSETS_RANK_STAT_ADD (intersection_short_circuits, vector_set.size ());
          return;
        }
        if (vector_set.empty ())
          return;
        if (other.vector_set.empty ()) {
          *this = self {};
          return;
        }
        size_t first = 0;
        for (; first < vector_set.size (); ++first) {
          if (not other.contains_with_rank (vector_set[first], ranks[first]))
            break;
          POSETS_RANK_STAT_ADD (intersection_short_circuits, 1);
        }
        if (first == vector_set.size ())
          return;

        self intersection;
        for (size_t i = 0; i < first; ++i)
          intersection.insert_with_rank (vector_set[i].copy (), ranks[i]);
        for (size_t i = first; i < vector_set.size (); ++i) {
          const auto& x = vector_set[i];
          const bool dominated = i != first and other.contains_with_rank (x, ranks[i]);
          if (dominated) {
            POSETS_RANK_STAT_ADD (intersection_short_circuits, 1);
            intersection.insert_with_rank (x.copy (), ranks[i]);
          }
          else {
            POSETS_RANK_STAT_ADD (intersection_pair_meets, other.size ());
            for (const auto& y : other)
              intersection.insert (x.meet (y));
          }
        }
        *this = std::move (intersection);
      }

      template <typename F>
      [[nodiscard]] self apply (const F& lambda) const {
        self res;
        POSETS_RANK_STAT_ADD (apply_elements, vector_set.size ());
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }

      // Editing/reordering/extracting generators invalidates cached ranks.
      // Reassign the downset before further indexed operations after raw mutation.
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
