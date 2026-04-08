#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/kdtree.hh>

namespace posets::downsets {
  // Forward definition for the operator<<s.
  template <Vector>
  class kdtree_backed;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const kdtree_backed<V>& f);

  // Another forward def to have friend status
  template <Vector V>
  class vector_or_kdtree_backed;

  // Finally the actual class definition
  template <Vector V>
  class kdtree_backed {
    private:
      utils::kdtree<V> tree;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const kdtree_backed<V2>& f);

      struct proj {
          const V& operator() (const V* pv) { return *pv; }
          V&& operator() (V*&& pv) { return std::move (*pv); }
      };

      void reset_tree (std::vector<V>&& elements) noexcept {
        std::vector<V*> pelements;
        pelements.reserve (elements.size ());
        for (auto& e : elements)
          pelements.push_back (&e);

        std::sort (pelements.begin (), pelements.end (),
                   // A strict total order.
                   [] (const V* v1, const V* v2) {
                     // A quite costly thing to do.
                     for (size_t i = 0; i < v1->size (); ++i) {
                       if ((*v1)[i] > (*v2)[i])
                         return false;
                       if ((*v1)[i] < (*v2)[i])
                         return true;
                     }
                     // Equal MUST return false.
                     return false;
                   });

        // then remove duplicates
        size_t dups_pos = pelements.size ();
        for (size_t i = pelements.size () - 1; i > 0; --i)
          if (*pelements[i] == *pelements[i - 1])
            std::swap (pelements[i], pelements[--dups_pos]);
        if (dups_pos != pelements.size ())
          pelements.erase (pelements.begin () + dups_pos, pelements.end ());

        // now, we can make a tree out of the set to eliminate dominated
        // elements
        auto antichain = std::vector<V*> ();
        antichain.reserve (pelements.size ());
        this->tree.relabel_tree (std::move (pelements), proj ());

        for (V& e : this->tree)
          if (not this->tree.dominates (e, true))
            antichain.push_back (&e);

        this->tree.relabel_tree (std::move (antichain), proj ());
        assert (this->tree.is_antichain ());
      }

    public:
      using value_type = V;

      kdtree_backed () = delete;

      kdtree_backed (std::vector<V>&& elements) noexcept { reset_tree (std::move (elements)); }

      kdtree_backed (V&& e) : tree (std::array<V, 1> {std::move (e)}) {}

      template <typename F>
      auto apply (const F& lambda) const {
        const auto& backing_vector = tree.get_backing_vector ();
        std::vector<V> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return kdtree_backed (std::move (ss));
      }

      kdtree_backed (const kdtree_backed&) = delete;
      kdtree_backed (kdtree_backed&&) = default;
      kdtree_backed& operator= (const kdtree_backed&) = delete;
      kdtree_backed& operator= (kdtree_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const { return this->tree.dominates (v); }

      // Union in place
      void union_with (kdtree_backed&& other) {
        assert (other.size () > 0);
        std::vector<V*> result;
        result.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto& e : tree)
          if (not other.tree.dominates (e, true))
            result.push_back (&e);

        // for all elements in the other tree, if they are not dominated
        // (not necessarily strict) by this tree, we keep them
        for (auto& e : other.tree)
          if (not this->tree.dominates (e))
            result.push_back (&e);

        // ready to rebuild the tree now
        assert (not result.empty ());
        this->tree.relabel_tree (std::move (result), proj ());
        assert (this->tree.is_antichain ());
      }

      // Intersection in place
      void intersect_with (const kdtree_backed& other) {
        std::vector<V> intersection;
        bool smaller_set = false;

        for (auto& x : tree) {
          assert (x.size () > 0);

          // If x is part of the set of all meets, then x will dominate the
          // whole list! So we use this to short-circuit the computation: we
          // first check whether x will be there (which happens only if it is
          // itself dominated by some element in other)
          const bool dominated = other.tree.dominates (x);
          if (dominated)
            intersection.push_back (x.copy ());
          else
            for (auto& y : other)
              intersection.push_back (x.meet (y));

          // If x wasn't in the set of meets, dominated is false and
          // the set of minima is different than what is in this->tree
          smaller_set or_eq not dominated;
        }

        // We can skip building trees and all if this->tree is the antichain
        // of minimal elements
        if (not smaller_set)
          return;

        // Worst-case scenario: we do need to build trees
        reset_tree (std::move (intersection));
      }

      [[nodiscard]] auto size () const { return this->tree.size (); }

      [[nodiscard]] auto& get_backing_vector () { return tree.get_backing_vector (); }

      [[nodiscard]] const auto& get_backing_vector () const { return tree.get_backing_vector (); }

      [[nodiscard]] auto begin () { return this->tree.begin (); }
      [[nodiscard]] auto begin () const { return this->tree.begin (); }
      [[nodiscard]] auto end () { return this->tree.end (); }
      [[nodiscard]] auto end () const { return this->tree.end (); }

      friend class vector_or_kdtree_backed<V>;
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const kdtree_backed<V>& f) {
    os << f.tree << std::endl;
    return os;
  }
}
