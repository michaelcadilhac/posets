#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/skiplist.hh>

namespace posets::downsets {
  template <Vector V>
  class skiplist_backed {
    public:
      using value_type = V;

      skiplist_backed (V&& v) { insert (std::move (v)); }

      skiplist_backed (std::vector<V>&& elements) noexcept {
        assert (not elements.empty ());
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      skiplist_backed () = default;
      utils::skiplist<V> sl;

    public:
      skiplist_backed (const skiplist_backed&) = delete;
      skiplist_backed (skiplist_backed&&) = default;
      skiplist_backed& operator= (skiplist_backed&&) = default;
      skiplist_backed& operator= (const skiplist_backed&) = delete;

      bool operator== (const skiplist_backed&) = delete;

      bool insert (V&& v) {
        if (sl.dominates (v))
          return false;
        sl.remove_dominated_by (v);
        sl.push (std::move (v));
        return true;
      }

      [[nodiscard]] bool contains (const V& v) const { return sl.dominates (v); }

      [[nodiscard]] auto size () const { return sl.size (); }

      void union_with (skiplist_backed&& other) {
        for (auto&& v : other.sl.drain ())
          insert (std::move (v));
      }

      void intersect_with (const skiplist_backed& other) {
        skiplist_backed intersection;
        bool smaller_set = false;

        for (const auto& x : sl) {
          bool dominated = false;

          for (const auto& y : other.sl) {
            V v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            if (dominated)
              break;
          }
          // If x wasn't <= an element in other, x is not in the intersection.
          smaller_set or_eq not dominated;
        }

        if (smaller_set)
          this->sl = std::move (intersection.sl);
      }

      template <typename F>
      skiplist_backed apply (const F& lambda) const {
        skiplist_backed res;
        for (const auto& el : sl)
          res.insert (lambda (el));
        return res;
      }

      [[nodiscard]] auto begin () const { return sl.begin (); }
      [[nodiscard]] auto end () const { return sl.end (); }

      [[nodiscard]] auto& get_backing_vector () { return sl; }
      [[nodiscard]] const auto& get_backing_vector () const { return sl; }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const skiplist_backed<V>& f) {
    for (const auto& el : f)
      os << el << '\n';
    return os;
  }
}
