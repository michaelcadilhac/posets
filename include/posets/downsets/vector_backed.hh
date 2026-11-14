#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/antichain.hh>

namespace posets::downsets {
  // A forward definition to allow for friend status
  template <Vector V>
  class vector_or_kdtree_backed;

  template <Vector V>
  class vector_backed {
    public:
      using value_type = V;

      vector_backed (V&& v) { insert (std::move (v)); }

      vector_backed (std::vector<V>&& elements) noexcept {
        assert (not elements.empty ());
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      vector_backed () = default;
      std::vector<V> vector_set;

      [[nodiscard]] bool debug_is_antichain () const {
#ifndef NDEBUG
        return utils::is_antichain (vector_set);
#else
        return true;
#endif
      }

    public:
      vector_backed (const vector_backed&) = delete;
      vector_backed (vector_backed&&) = default;
      vector_backed& operator= (vector_backed&&) = default;
      vector_backed& operator= (const vector_backed&) = delete;

      // Trusted bulk construction.  `elements` must contain distinct,
      // pairwise-incomparable values.  This is useful for order isomorphisms,
      // which preserve an existing maximal-element antichain exactly.
      static vector_backed from_antichain_unchecked (std::vector<V>&& elements) {
        assert (not elements.empty ());
        vector_backed result;
        result.vector_set = std::move (elements);
        assert (result.debug_is_antichain ());
        return result;
      }

      bool operator== (const vector_backed& other) = delete;

      [[nodiscard]] bool contains (const V& v) const {
        return std::ranges::any_of (vector_set,
                                    [&v] (const V& e) { return v.partial_order (e).leq (); });
      }

      [[nodiscard]] auto size () const { return vector_set.size (); }

      bool insert (V&& v) { return utils::insert_maximum (vector_set, std::move (v)); }

      void union_with (vector_backed&& other) {
        for (auto&& e : other.vector_set)
          insert (std::move (e));
      }

      void intersect_with (const vector_backed& other) {
        vector_backed intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          bool dominated = false;

          for (auto& y : other.vector_set) {
            V v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            if (dominated)
              break;
          };
          // If x wasn't <= an element in other, then x is not in the
          // intersection, thus the set is updated.
          smaller_set = smaller_set or not dominated;
        }

        if (smaller_set)
          this->vector_set = std::move (intersection.vector_set);
      }

      template <typename F>
      [[nodiscard]] vector_backed apply (const F& lambda) const {
        vector_backed res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }

      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }

      template <Vector V2>
      friend class vector_or_kdtree_backed;
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const vector_backed<V>& f) {
    for (auto&& el : f)
      os << el << std::endl;

    return os;
  }
}
