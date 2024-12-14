#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/ref_ptr_cmp.hh>

namespace posets::downsets {
  template <Vector V>
  class set_backed {
    public:
      typedef V value_type;

    private:
      set_backed () {}

    public:
      set_backed (std::vector<V>&& elements) {
        for (auto&& v : elements)
          insert (std::move (v));
      }

      set_backed (V&& v) noexcept { insert (std::move (v)); }

      set_backed (const set_backed&) = delete;
      set_backed (set_backed&&) = default;
      set_backed& operator= (set_backed&&) = default;

      bool contains (const V& v) const {
        for (const auto& e : vector_set)
          if (v.partial_order (e).leq ())
            return true;
        return false;
      }

      auto size () const { return vector_set.size (); }

      bool insert (V&& v) {
        utils::reference_wrapper_set<const V> to_remove;
        bool should_be_inserted = true;

        for (const auto& e : vector_set) {
          auto po = v.partial_order (e);
          if (po.leq ()) {
            should_be_inserted = false;
            break;
          }
          else if (po.geq ()) {
            should_be_inserted = true;
            to_remove.insert (std::cref (e));
          }
        }

        for (const auto& elt : to_remove)
          vector_set.erase (elt.get ());

        if (should_be_inserted)
          vector_set.insert (std::move (v));

        return should_be_inserted;
      }

      void union_with (set_backed&& other) {
        for (auto it = other.begin (); it != other.end (); /* in-body */)
          insert (std::move (other.vector_set.extract (it++).value ()));
      }

      void intersect_with (const set_backed& other) {
        set_backed intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          bool dominated = false;

          for (const auto& y : other.vector_set) {
            V&& v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            if (dominated)
              break;
          }
          // If x wasn't <= an element in other, then x is not in the
          // intersection, thus the set is updated.
          smaller_set |= not dominated;
        }

        if (smaller_set)
          this->vector_set = std::move (intersection.vector_set);
      }

      template <typename F>
      void apply_inplace (const F& lambda) {
        std::set<V> new_set;
        for (auto&& el : vector_set) {
          auto&& changed_el = lambda (el);
          new_set.insert (changed_el);
        }
        vector_set = std::move (new_set);
      }

      template <typename F>
      set_backed apply (const F& lambda) const {
        set_backed res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      auto begin () { return vector_set.begin (); }
      const auto begin () const { return vector_set.begin (); }
      auto end () { return vector_set.end (); }
      const auto end () const { return vector_set.end (); }

    private:
      std::set<V> vector_set;
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const set_backed<V>& f) {
    for (auto&& el : f)
      os << el << std::endl;

    return os;
  }
}
