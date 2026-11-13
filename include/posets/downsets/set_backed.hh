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
      using value_type = V;

    private:
      set_backed () = default;

    public:
      set_backed (std::vector<V>&& elements) {
        for (auto&& v : elements)
          insert (std::move (v));
      }

      set_backed (V&& v) noexcept { insert (std::move (v)); }

      set_backed (const set_backed&) = delete;
      set_backed (set_backed&&) = default;
      set_backed& operator= (set_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const {
        return std::ranges::any_of (vector_set,
                                    [&] (const auto& e) { return v.partial_order (e).leq (); });
      }

      [[nodiscard]] auto size () const { return vector_set.size (); }

      bool insert (V&& v) {
        utils::reference_wrapper_set<const V> to_remove;

        for (const auto& e : vector_set) {
          auto po = v.partial_order (e);
          if (po.leq ())
            return false;
          if (po.geq ())
            to_remove.insert (std::cref (e));
        }

        for (const auto& elt : to_remove)
          vector_set.erase (elt.get ());

        vector_set.insert (std::move (v));

        return true;
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
          smaller_set = smaller_set or not dominated;
        }

        if (smaller_set)
          this->vector_set = std::move (intersection.vector_set);
      }

      template <typename F>
      [[nodiscard]] set_backed apply (const F& lambda) const {
        set_backed res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      [[nodiscard]] auto& get_backing_vector () { return vector_set; }

      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }

      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }

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
