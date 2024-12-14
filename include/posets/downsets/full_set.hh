#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <list>
#include <set>
#include <vector>

#include <posets/concepts.hh>

namespace posets::downsets {
  template <Vector V>
  class full_set {
    public:
      typedef V value_type;

      full_set () = delete;

      full_set (const full_set&) = delete;
      full_set (full_set&&) = default;
      full_set& operator= (full_set&&) = default;

      full_set (V&& v) { insert (std::move (v)); }

      full_set (std::vector<V>&& elements) noexcept {
        assert (elements.size () > 0);
        for (auto&& e : elements)
          insert (std::move (e));
      }

      bool contains (const V& v) const { return vector_set.find (v) != vector_set.end (); }

      auto size () const { return vector_set.size (); }

      bool insert (V&& v) {
        if (vector_set.insert (std::move (v)).second) {
          downward_close ();
          return true;
        }
        return false;
      }

    private:
      // Extraordinarily wasteful.  This computes the closure by taking
      // vector_set, then everything at distance 1
      void downward_close () {
        auto old_size = vector_set.size ();

        while (1) {
          std::list<V> newelts;
          std::vector<typename V::value_type> elcopy (vector_set.front ().size ());
          elcopy.reserve (V::capacity_for (elcopy.size ()));

          for (auto& el : vector_set) {  // Iterate while making copies.
            el.to_vector (elcopy);
            for (size_t i = 0; i < el.size (); ++i)
              if (elcopy[i] > -1) {
                elcopy[i]--;
                V v = V (elcopy);
                elcopy[i]++;
                if (vector_set.find (v) == vector_set.end ())
                  newelts.push_back (std::move (v));
              }
          }
          if (newelts.empty ())
            break;
          for (auto& el : newelts)
            vector_set.insert (std::move (el));
        }
      }

    public:
      void union_with (const full_set& other) {
        for (auto&& el : other)
          vector_set.insert (el.copy ());
        downward_close ();
      }

      void intersect_with (const full_set& other) {
        std::list<std::reference_wrapper<const V>> intersection;
        std::set_intersection (vector_set.cbegin (), vector_set.cend (),
                               other.vector_set.cbegin (), other.vector_set.cend (),
                               std::inserter (intersection, intersection.begin ()));
        if (intersection.size () != vector_set.size ()) {
          vector_set.clear ();
          for (auto& r : intersection)
            vector_set.insert (r.get ().copy ());
        }
        downward_close ();
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
      full_set apply (const F& lambda) const {
        full_set res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        res.downward_close ();
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
  inline std::ostream& operator<< (std::ostream& os, const full_set<V>& f) {
    for (auto&& el : f)
      os << el << std::endl;

    return os;
  }
}
