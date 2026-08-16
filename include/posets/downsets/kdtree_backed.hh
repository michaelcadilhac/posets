#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/kdtree.hh>
#include <posets/utils/reduce.hh>

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

      void reset_tree (std::vector<V>&& elements) {
        auto antichain = utils::reduce_to_maxima (std::move (elements));
        assert (not antichain.empty ());
        this->tree.relabel_tree (std::move (antichain));
        assert (this->tree.is_antichain ());
      }

    public:
      using value_type = V;

      kdtree_backed () = delete;

      kdtree_backed (std::vector<V>&& elements) { reset_tree (std::move (elements)); }

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

        if (result.size () == this->size ()) {
          bool unchanged = true;
          for (size_t i = 0; i < result.size (); ++i)
            unchanged and_eq result[i] == &this->tree.get_backing_vector ()[i];
          if (unchanged)
            return;
        }

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
