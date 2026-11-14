#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include <posets/concepts.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/vector_backed.hh>

// FIXME? the theory says it should be (exp (dim) < m)
#ifndef KD_THRESH
# define KD_THRESH(M, D) ((D) * 2 < (M))
#endif

namespace posets::downsets {
  // Forward definition for the operator<<s.
  template <Vector>
  class vector_or_kdtree_backed;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const vector_or_kdtree_backed<V>& f);

  template <Vector V>
  class vector_or_kdtree_backed {
    private:
      std::unique_ptr<vector_backed<V>> vector;
      std::unique_ptr<kdtree_backed<V>> kdtree;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const vector_or_kdtree_backed<V2>& f);

      [[nodiscard]] size_t vector_dim () const {
        const auto& backing = get_backing_vector ();
        return backing.empty () ? 0 : backing[0].size ();
      }

      void boolop_with (vector_or_kdtree_backed&& other, bool inter = false) {
        // Invariant: each unconsumed operand owns exactly one backing, and either
        // backing exposes a maximal antichain. Normalize only the vector path,
        // converting the LHS before consuming the RHS (also on exceptions).
        const auto combine = [inter] (auto& lhs, auto& rhs) {
          if (inter)
            lhs.intersect_with (std::move (rhs));
          else
            lhs.union_with (std::move (rhs));
        };
        if (kdtree and other.kdtree and
            static_cast<double> (std::max (size (), other.size ())) <
                exp (static_cast<double> (std::min (size (), other.size ()))))
          combine (*kdtree, *other.kdtree);
        else {
          if (kdtree) {
            vector =
                std::make_unique<vector_backed<V>> (std::move (kdtree->get_backing_vector ()));
            kdtree = nullptr;
          }
          if (other.kdtree) {
            vector_backed<V> rhs (std::move (other.kdtree->get_backing_vector ()));
            combine (*vector, rhs);
          }
          else
            combine (*vector, *other.vector);
        }

        // one last thing to deal with: it could be that we ended up with a
        // list but it satisfies the condition for it to be upgraded to a
        // kd-tree
        const size_t m = this->size ();
        const size_t dim = this->vector_dim ();
        if (this->kdtree == nullptr and KD_THRESH (m, dim)) {
          this->kdtree =
              std::make_unique<kdtree_backed<V>> (std::move (this->vector->get_backing_vector ()));
          this->vector = nullptr;
        }
      }

    public:
      using value_type = V;

      vector_or_kdtree_backed () = delete;

      vector_or_kdtree_backed (std::vector<V>&& elements) {
        assert (not elements.empty ());
        const size_t m = elements.size ();
        const size_t dim = elements[0].size ();

        // NOTE: we are checking the size BEFORE we actually create the
        // downset container; their respective constructors may reduce the
        // size by removing dominated elements... it's easier and clearer
        // to do the check here though
        if (KD_THRESH (m, dim))
          this->kdtree = std::make_unique<kdtree_backed<V>> (std::move (elements));
        else
          this->vector = std::make_unique<vector_backed<V>> (std::move (elements));
      }

      vector_or_kdtree_backed (V&& el) {
        // too small, just use a vector
        this->vector = std::make_unique<vector_backed<V>> (std::move (el));
      }

      template <typename F>
      [[nodiscard]] auto apply (const F& lambda) const {
        const std::vector<V>& backing_vector = get_backing_vector ();
        std::vector<V> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return vector_or_kdtree_backed (std::move (ss));
      }

      vector_or_kdtree_backed (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed (vector_or_kdtree_backed&&) = default;
      vector_or_kdtree_backed& operator= (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed& operator= (vector_or_kdtree_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const {
        if (this->kdtree != nullptr)
          return this->kdtree->contains (v);
        return this->vector->contains (v);
      }

      /* Union in place
       * We use kd-trees only if both are already given as kd-trees
       * and their difference in size is subexponential
       */
      void union_with (vector_or_kdtree_backed&& other) {
        boolop_with (std::move (other), false);  // not intersection
      }

      /* Intersection in place
       */
      void intersect_with (vector_or_kdtree_backed&& other) {
        boolop_with (std::move (other), true);  // it is intersection
      }

      [[nodiscard]] auto size () const {
        return this->kdtree != nullptr ? this->kdtree->size () : this->vector->size ();
      }

      [[nodiscard]] auto& get_backing_vector () {
        return vector ? vector->get_backing_vector () : kdtree->get_backing_vector ();
      }

      [[nodiscard]] const auto& get_backing_vector () const {
        return vector ? vector->get_backing_vector () : kdtree->get_backing_vector ();
      }

      [[nodiscard]] auto begin () {
        return this->kdtree != nullptr ? this->kdtree->begin () : this->vector->begin ();
      }
      [[nodiscard]] auto begin () const {
        return this->kdtree != nullptr ? this->kdtree->begin () : this->vector->begin ();
      }
      [[nodiscard]] auto end () {
        return this->kdtree != nullptr ? this->kdtree->end () : this->vector->end ();
      }
      [[nodiscard]] auto end () const {
        return this->kdtree != nullptr ? this->kdtree->end () : this->vector->end ();
      }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const vector_or_kdtree_backed<V>& f) {
    if (f.kdtree != nullptr)
      os << *(f.kdtree) << std::endl;
    else
      for (auto&& el : *(f.vector))
        os << el << std::endl;
    return os;
  }
}
