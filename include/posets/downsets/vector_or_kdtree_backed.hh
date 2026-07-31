#pragma once

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/vector_backed.hh>

// FIXME? the theory says it should be (exp (dim) < m)
#define KD_THRESH(M, D) ((D) * 2 < (M))

#ifdef AC_DATA
# define data_do(acts...) \
   do {                   \
     acts;                \
   } while (0)
#else
# define data_do(x...)
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
        // four cases: 1. both are vectors, 2/3. one is a vector,
        // 4. both are kdtrees
        if (this->kdtree == nullptr and other.kdtree == nullptr) {
          // case 1. just work with vectors
          assert (other.vector != nullptr);
          assert (this->vector != nullptr);
          if (inter)
            this->vector->intersect_with (std::move (*(other.vector)));
          else
            this->vector->union_with (std::move (*(other.vector)));
        }
        else if (this->kdtree == nullptr and other.kdtree != nullptr) {
          // case 2. reinterpret kdtree as vector
          assert (this->vector != nullptr);
          assert (other.vector == nullptr);
          // this costs linear time already: reinterpret the kdtree as a
          // vector
          vector_backed<V> b = vector_backed<V> (std::move (other.kdtree->get_backing_vector ()));
          if (inter)
            this->vector->intersect_with (std::move (b));
          else
            this->vector->union_with (std::move (b));
        }
        else if (other.kdtree == nullptr and this->kdtree != nullptr) {
          // case 3. again reinterpret kdtree as vector
          assert (other.vector != nullptr);
          assert (this->vector == nullptr);
          this->vector =
              std::make_unique<vector_backed<V>> (std::move (this->kdtree->get_backing_vector ()));
          this->kdtree = nullptr;
          if (inter)
            this->vector->intersect_with (std::move (*(other.vector)));
          else
            this->vector->union_with (std::move (*(other.vector)));
        }
        else {
          // case 4. we have two kdtrees, though we still
          // need to check if reinterpreting them as vectors
          // is easier
          size_t m;
          size_t n;
          if (this->size () > other.size ()) {
            m = other.size ();
            n = this->size ();
          }
          else {
            m = this->size ();
            n = other.size ();
          }
          if (static_cast<double> (n) < exp (static_cast<double> (m))) {
            if (inter)
              this->kdtree->intersect_with (std::move (*(other.kdtree)));
            else
              this->kdtree->union_with (std::move (*(other.kdtree)));
          }
          else {
            this->vector = std::make_unique<vector_backed<V>> (
                std::move (this->kdtree->get_backing_vector ()));
            this->kdtree = nullptr;
            vector_backed<V> b =
                vector_backed<V> (std::move (other.kdtree->get_backing_vector ()));
            if (inter)
              this->vector->intersect_with (std::move (b));
            else
              this->vector->union_with (std::move (b));
          }
        }

        // one last thing to deal with: it could be that we ended up with a
        // list but it satisfies the condition for it to be upgraded to a
        // kd-tree
        const size_t m = this->size ();
        const size_t dim = this->vector_dim ();
        data_do (std::cout << "|VEKD: downset_size=" << dim << "," << m << "|" << std::endl);
        if (this->kdtree == nullptr and KD_THRESH (m, dim)) {
          this->kdtree =
              std::make_unique<kdtree_backed<V>> (std::move (this->vector->get_backing_vector ()));
          this->vector = nullptr;
          data_do (std::cout << "VEKD: upgraded to kd-tree downset" << std::endl);
        }
      }

    public:
      using value_type = V;

      vector_or_kdtree_backed () = delete;

      vector_or_kdtree_backed (std::vector<V>&& elements) {
        assert (not elements.empty ());
        const size_t m = elements.size ();
        const size_t dim = elements[0].size ();
        data_do (std::cout << "|VEKD: downset_size=" << dim << "," << m << "|" << std::endl);

        // NOTE: we are checking the size BEFORE we actually create the
        // downset container; their respective constructors may reduce the
        // size by removing dominated elements... it's easier and clearer
        // to do the check here though
        if (KD_THRESH (m, dim)) {
          this->kdtree = std::make_unique<kdtree_backed<V>> (std::move (elements));
          data_do (std::cout << "VEKD: created kd-tree downset" << std::endl);
        }
        else {
          this->vector = std::make_unique<vector_backed<V>> (std::move (elements));
          data_do (std::cout << "VEKD: created vector downset" << std::endl);
        }
      }

      vector_or_kdtree_backed (V&& el) {
        // too small, just use a vector
        this->vector = std::make_unique<vector_backed<V>> (std::move (el));
      }

      template <typename F>
      auto apply (const F& lambda) const {
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
