#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>
#include <vector>

#include <posets/concepts.hh>

/*
 * This is Shrisha Rao's version of a kd-tree for variable dimension. The
 * basis for the algorithms comes from the Computational Geometry book
 * by de Berg et al.
 *
 * Coded by Guillermo A. Perez
 */
namespace posets::utils {
  // Forward definition for the operator<<
  template <Vector>
  class kdtree;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const utils::kdtree<V>& f);

  template <Vector V>
  class kdtree {
    private:
      struct kdtree_node;
      using kdtree_node_ptr = kdtree_node*;

      struct kdtree_node {
          // SIZE_MAX marks an internal node; any other value is a leaf index.
          // Using a sentinel instead of std::optional<size_t> saves 16 bytes
          // of overhead and avoids the extra boolean flag in the struct.
          size_t value_idx;   // leaf: index into vector_set; internal: SIZE_MAX
          int location;       // the value at which we split
          int axis;           // the dimension at which we split (fits in int)
          bool clean_split;   // whether the split is s.t. to the left all is
                              // smaller
      };

      size_t dim;
      kdtree_node_ptr tree;
      // Reused across dominates() calls to avoid a heap allocation per call.
      mutable std::vector<int> lbounds_cache;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const kdtree<V2>& f);

      // NOLINTBEGIN(misc-no-recursion)
      /*
       * This is one of the only interesting parts of the code: building the
       * kd-tree to make sure it is balanced.
       *
       * NOTE: This assumes that this->tree has been allocated enough memory to
       * hold the whole (balanced) tree
       */
      void recursive_build (size_t result,  // where to leave the new tree
                            const std::vector<size_t>::iterator& begin_it,
                            const std::vector<size_t>::iterator& end_it, size_t length,
                            size_t axis) {
        // sanity checks
        assert (this->tree != nullptr);
        assert (std::cmp_greater (4 << (int) (std::floor (std::log2 (this->vector_set.size ()))),
                                  result));
        assert (std::cmp_equal (std::distance (begin_it, end_it), length));
        assert (length > 0);
        assert (axis < this->dim);

        // if the list of elements is now a singleton, we make a leaf
        if (length == 1) {
          this->tree[result].value_idx = *begin_it;
          return;  // location/axis/clean_split unused for leaves
        }

        // Use a selection algorithm to get the median
        // NOTE: we actually get the item whose index is
        auto median_it = begin_it + static_cast<ssize_t> (length) / 2;
        std::nth_element (begin_it, median_it, end_it, [this, &axis] (size_t i1, size_t i2) {
          return this->vector_set[i1][axis] < this->vector_set[i2][axis];
        });
        const size_t median_idx = *median_it;
        const int loc = this->vector_set[median_idx][axis];

        // check whether the maximal element on the left is equal to loc
        // (with respect to dimension axis) to determine if the split is clean
        auto max_it = std::max_element (begin_it, median_it, [this, &axis] (size_t i1, size_t i2) {
          return this->vector_set[i1][axis] < this->vector_set[i2][axis];
        });
        const size_t max_idx = *max_it;
        const bool clean = (this->vector_set[max_idx][axis] < loc);

        // some sanity checks about the sublists
        assert (std::distance (begin_it, median_it) > 0);
        assert (std::distance (median_it, end_it) > 0);
        assert (static_cast<size_t> (std::distance (begin_it, median_it)) == length / 2);
        assert (static_cast<size_t> (std::distance (median_it, end_it)) == length - (length / 2));
        assert (static_cast<size_t> (std::distance (begin_it, median_it)) +
                    std::distance (median_it, end_it) ==
                length);
        assert (this->vector_set[max_idx][axis] <= loc);

        // the next axis is just the following dimension, wrapping around
        const size_t next_axis = (axis + 1) % this->dim;
        // we can now prepare the information of the root node and then
        // recursively prepare the left and right children
        this->tree[result].value_idx = std::numeric_limits<size_t>::max ();
        this->tree[result].location = loc;
        this->tree[result].axis = static_cast<int> (axis);
        this->tree[result].clean_split = clean;
        // now the recursive calls
        recursive_build ((result * 2) + 1, begin_it, median_it, length / 2, next_axis);
        recursive_build ((result * 2) + 2, median_it, end_it, length - (length / 2), next_axis);
      }

      /*
       * And this is the second interesting piece of code, using the
       * properties of how the kd-tree was constructed to look for an element
       * that dominates a given vector.
       * NOTE: Through the recursive calls we keep track of dim variables
       * which store the lower bounds of the region of the current node and a
       * counter dims_to_dom which records the dimensions on which the current
       * region is not yet dominating the region of v
       */
      bool recursive_dominates (const V& v, bool strict, size_t node_idx, int* lbounds,
                                size_t dims_to_dom) const {
        // sanity checks
        assert (this->tree != nullptr);
        assert (std::cmp_greater (4 << (int) (std::floor (std::log2 (this->vector_set.size ()))),
                                  node_idx));
        assert (dims_to_dom > 0);

        // from index to node pointer
        kdtree_node_ptr node = this->tree + node_idx;

        // if we are at a leaf, just check if it dominates
        if (node->value_idx != std::numeric_limits<size_t>::max ()) {
          auto po = v.partial_order (this->vector_set[node->value_idx]);
          if (strict)
            return po.leq () and not po.geq ();
          return po.leq ();
        }

        // so we're at an inner node!
        // let's check if the right subtree
        // is guaranteed to have a dominating vector
        const int old_bound = lbounds[node->axis];
        size_t still_to_dom = dims_to_dom;
        assert (node->location >= old_bound);
        if ((node->location > v[node->axis] and old_bound <= v[node->axis]) or
            (not strict and node->location >= v[node->axis] and old_bound < v[node->axis]))
          still_to_dom--;
        if (still_to_dom == 0)
          return true;
        lbounds[node->axis] = node->location;

        // if we got here, we need to check on the right recursively
        const bool r_succ =
            recursive_dominates (v, strict, (2 * node_idx) + 2, lbounds, still_to_dom);
        if (r_succ)
          return true;

        // all that's left is to check on the left recursively, if pertinent
        lbounds[node->axis] = old_bound;
        if (v[node->axis] > node->location or
            (v[node->axis] == node->location and node->clean_split)) {
          return false;
        }
        // it is pertinent after all
        return recursive_dominates (v, strict, (2 * node_idx) + 1, lbounds, dims_to_dom);
      }

      std::vector<V> vector_set;

    public:
      // NOTE: this works for any collection of vectors, not even set assumed
      template <std::ranges::input_range R, class Proj = std::identity>
      void relabel_tree (R&& elements, Proj proj = {}) {
        this->dim = (proj (*elements.begin ()).size ());

        // sanity checks
        assert (elements.size () > 0);
        assert (this->dim > 0);

        // Let n be the size of vector_set, the no. of leaves in the tree is
        // 2^{floor(lg(n)) + 1}, so this times 2 is the size of the full
        // binary tree we will be labelling
        const size_t oldsize = 4 << (size_t) (std::floor (std::log2 (this->vector_set.size ())));
        const size_t tsize = 4 << (size_t) (std::floor (std::log2 (elements.size ())));

        // moving the given elements to the internal data structure
        std::vector<V> newset;
        newset.reserve (elements.size ());
        for (auto&& e : elements | std::views::reverse)
          newset.push_back (proj (std::move (e)));
        this->vector_set = std::move (newset);

        // WARNING: moved elements, so we can't really use it below! instead,
        // use this->vector_set
        assert (this->vector_set.size () > 0);

        // We now prepare the list of indices to include in the tree
        std::vector<size_t> points (this->vector_set.size ());
        // NOLINTBEGIN(boost-use-ranges)
        std::iota (points.begin (), points.end (), 0);
        // NOLINTEND(boost-use-ranges)
        if (this->tree == nullptr)
          this->tree = new kdtree_node[tsize];
        if (this->tree != nullptr and oldsize < tsize) {
          delete[] this->tree;
          this->tree = new kdtree_node[tsize];
        }
        recursive_build (0, points.begin (), points.end (), points.size (), 0);
        this->lbounds_cache.resize (this->dim);
      }
      // NOLINTEND(misc-no-recursion)

      kdtree ()
        : tree (nullptr) {}  // FIXME: shall we delete this? it makes a kdtree
                             // without knowing the size of anything!
      kdtree (size_t dim) : dim (dim), tree (nullptr) {}
      kdtree (size_t dim, size_t initsize) : dim (dim) {
        const size_t tsize = 4 << (size_t) (std::floor (std::log2 (initsize)));
        this->tree = new kdtree_node[tsize];
      }

      template <std::ranges::input_range R, class Proj = std::identity>
      kdtree (R&& elements, Proj proj = {})
        : dim (proj (*elements.begin ()).size ()),
          tree (nullptr) {
        relabel_tree (std::forward<R> (elements), proj);
      }

      kdtree (const kdtree& other) = delete;
      kdtree (kdtree&& other) noexcept
        : dim (other.dim),
          tree (other.tree),
          lbounds_cache (std::move (other.lbounds_cache)),
          vector_set (std::move (other.vector_set)) {
        other.tree = nullptr;
      }

      ~kdtree () { delete[] this->tree; }

      kdtree& operator= (kdtree&& other) noexcept {
        this->dim = other.dim;
        this->vector_set = std::move (other.vector_set);
        this->lbounds_cache = std::move (other.lbounds_cache);
        // WARNING: 3 variable follows to make the whole thing safe for
        // self-assignment
        kdtree_node_ptr temp_tree = other.tree;
        other.tree = nullptr;
        // NOTE: this must be here, after having a local copy of the other
        // tree and before moving it to here, because of self-assignment
        // safety!
        delete[] this->tree;
        // we now copy things here and return
        this->tree = temp_tree;
        return *this;
      }

      [[nodiscard]] auto& get_backing_vector () { return vector_set; }

      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }

      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        // lbounds_cache is a mutable member pre-sized to dim in relabel_tree(),
        // so no heap allocation is needed here.
        std::fill (this->lbounds_cache.begin (), this->lbounds_cache.end (),
                   std::numeric_limits<int>::min ());
        return this->recursive_dominates (v, strict, 0, this->lbounds_cache.data (), this->dim);
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it = this->begin (); it != this->end (); ++it) {
          for (auto it2 = it + 1; it2 != this->end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        }
        return true;
      }
      [[nodiscard]] bool operator== (const kdtree& other) const {
        return this->vector_set == other.vector_set;
      }
      [[nodiscard]] auto size () const { return this->vector_set.size (); }
      [[nodiscard]] bool empty () { return this->vector_set.empty (); }
      [[nodiscard]] auto begin () noexcept { return this->vector_set.begin (); }
      [[nodiscard]] auto begin () const noexcept { return this->vector_set.begin (); }
      [[nodiscard]] auto end () noexcept { return this->vector_set.end (); }
      [[nodiscard]] auto end () const noexcept { return this->vector_set.end (); }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const kdtree<V>& f) {
    for (auto&& el : f.vector_set)
      os << el << std::endl;

    return os;
  }
}
