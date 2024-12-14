#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <ranges>
#include <stack>
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
          std::optional<size_t> value_idx;  // only for leaves: the index of
                                            // the element from the list
          int location;                     // the value at which we split
          size_t axis;                      // the dimension at which we
                                            // split
          bool clean_split;                 // whether the split is s.t.
                                            // to the left all is smaller
      };

      size_t dim;
      kdtree_node_ptr tree;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const kdtree<V2>& f);

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
        assert (static_cast<size_t> (
                    4 << (int) (std::floor (std::log2 (this->vector_set.size ())))) > result);
        assert (static_cast<size_t> (std::distance (begin_it, end_it)) == length);
        assert (length > 0);
        assert (axis < this->dim);

        // if the list of elements is now a singleton, we make a leaf
        if (length == 1) {
          this->tree[result].value_idx = *begin_it;
          return;
        }

        // Use a selection algorithm to get the median
        // NOTE: we actually get the item whose index is
        auto median_it = begin_it + length / 2;
        std::nth_element (begin_it, median_it, end_it, [this, &axis] (size_t i1, size_t i2) {
          return this->vector_set[i1][axis] < this->vector_set[i2][axis];
        });
        size_t median_idx = *median_it;
        int loc = this->vector_set[median_idx][axis];

        // check whether the maximal element on the left is equal to loc
        // (with respect to dimension axis) to determine if the split is clean
        auto max_it = std::max_element (begin_it, median_it, [this, &axis] (size_t i1, size_t i2) {
          return this->vector_set[i1][axis] < this->vector_set[i2][axis];
        });
        size_t max_idx = *max_it;
        bool clean = (this->vector_set[max_idx][axis] < loc);

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
        size_t next_axis = (axis + 1) % this->dim;
        // we can now prepare the information of the root node and then
        // recursively prepare the left and right children
        this->tree[result].value_idx = std::nullopt;
        this->tree[result].location = loc;
        this->tree[result].axis = axis;
        this->tree[result].clean_split = clean;
        // now the recursive calls
        recursive_build ((result * 2) + 1, begin_it, median_it, length / 2, next_axis);
        recursive_build ((result * 2) + 2, median_it, end_it, length - (length / 2), next_axis);
        return;
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
        assert (static_cast<size_t> (
                    4 << (int) (std::floor (std::log2 (this->vector_set.size ())))) > node_idx);
        assert (dims_to_dom > 0);

        // from index to node pointer
        kdtree_node_ptr node = this->tree + node_idx;

        // if we are at a leaf, just check if it dominates
        if (node->value_idx) {
          auto po = v.partial_order (this->vector_set[*(node->value_idx)]);
          if (strict)
            return po.leq () and not po.geq ();
          else
            return po.leq ();
        }

        // so we're at an inner node!
        // let's check if the right subtree
        // is guaranteed to have a dominating vector
        const int old_bound = lbounds[node->axis];
        size_t still_to_dom = dims_to_dom;
        assert (node->location >= old_bound);
        if (node->location > v[node->axis] && old_bound <= v[node->axis])
          still_to_dom--;
        else if (!strict && node->location >= v[node->axis] && old_bound < v[node->axis])
          still_to_dom--;
        if (still_to_dom == 0)
          return true;
        lbounds[node->axis] = node->location;

        // if we got here, we need to check on the right recursively
        bool r_succ = recursive_dominates (v, strict, (2 * node_idx) + 2, lbounds, still_to_dom);
        if (r_succ)
          return true;

public:
  std::vector<V> vector_set;

  // NOTE: this works for any collection of vectors, not even set assumed
  template <std::ranges::input_range R, class Proj = std::identity>
  void relabel_tree(R &&elements, Proj proj = {}) {
    this->dim = (proj(*elements.begin()).size());

    // sanity checks
    assert(elements.size() > 0);
    assert(this->dim > 0);

    // Let n be the size of vector_set, the no. of leaves in the tree is
    // 2^{floor(lg(n)) + 1}, so this times 2 is the size of the full
    // binary tree we will be labelling
    size_t oldsize =
        4 << (size_t)(std::floor(std::log2(this->vector_set.size())));
    size_t tsize = 4 << (size_t)(std::floor(std::log2(elements.size())));

    // moving the given elements to the internal data structure
    std::vector<V> newset;
    newset.reserve(elements.size());
    for (auto &&e : elements | std::views::reverse)
      newset.push_back(proj(std::move(e)));
    this->vector_set = std::move(newset);

    // WARNING: moved elements, so we can't really use it below! instead,
    // use this->vector_set
    assert(this->vector_set.size() > 0);

    // We now prepare the list of indices to include in the tree
    std::vector<size_t> points(this->vector_set.size());
    std::iota(points.begin(), points.end(), 0);
    if (this->tree == nullptr)
      this->tree = new kdtree_node[tsize];
    if (this->tree != nullptr && oldsize < tsize) {
      delete[] this->tree;
      this->tree = new kdtree_node[tsize];
    }
    recursive_build(0, points.begin(), points.end(), points.size(), 0);
  }

    public:
      std::vector<V> vector_set;

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
        size_t oldsize = 4 << (size_t) (std::floor (std::log2 (this->vector_set.size ())));
        size_t tsize = 4 << (size_t) (std::floor (std::log2 (elements.size ())));

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
        std::iota (points.begin (), points.end (), 0);
        if (this->tree == nullptr)
          this->tree = new kdtree_node[tsize];
        if (this->tree != nullptr && oldsize < tsize) {
          delete[] this->tree;
          this->tree = new kdtree_node[tsize];
        }
        recursive_build (0, points.begin (), points.end (), points.size (), 0);
      }

      kdtree ()
        : tree (nullptr) {}  // FIXME: shall we delete this? it makes a kdtree
                             // without knowing the size of anything!
      kdtree (size_t dim) : dim (dim), tree (nullptr) {}
      kdtree (size_t dim, size_t initsize) : dim (dim) {
        size_t tsize = 4 << (size_t) (std::floor (std::log2 (initsize)));
        this->tree = new kdtree_node[tsize];
      }
      template <std::ranges::input_range R, class Proj = std::identity>
      kdtree (R&& elements, Proj proj = {})
        : dim (proj (*elements.begin ()).size ()),
          tree (nullptr) {
        relabel_tree (std::move (elements), proj);
      }

      kdtree (const kdtree& other) = delete;
      kdtree (kdtree&& other)
        : dim (other.dim),
          tree (other.tree),
          vector_set (std::move (other.vector_set)) {
        other.tree = nullptr;
      }

      ~kdtree () {
        if (this->tree != nullptr)
          delete[] this->tree;
      }

      kdtree& operator= (kdtree&& other) {
        this->dim = other.dim;
        this->vector_set = std::move (other.vector_set);
        // WARNING: 3 variable follows to make the whole thing safe for
        // self-assignment
        kdtree_node_ptr temp_tree = other.tree;
        other.tree = nullptr;
        // NOTE: this must be here, after having a local copy of the other
        // tree and before moving it to here, because of self-assignment
        // safety!
        if (this->tree != nullptr)
          delete[] this->tree;
        // we now copy things here and return
        this->tree = temp_tree;
        return *this;
      }


  bool dominates(const V &v, bool strict = false) const {
    int lbounds[this->dim];
    std::fill_n(lbounds, this->dim, std::numeric_limits<int>::min());
    return this->recursive_dominates(v, strict, 0, lbounds, this->dim);
  }


      bool is_antichain () const {
        for (auto it = this->begin (); it != this->end (); ++it) {
          for (auto it2 = it + 1; it2 != this->end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }

        }
      }

      bool operator== (const kdtree& other) const { return this->vector_set == other->vector_set; }
      auto size () const { return this->vector_set.size (); }
      bool empty () { return this->vector_set.empty (); }
      auto begin () noexcept { return this->vector_set.begin (); }
      const auto begin () const noexcept { return this->vector_set.begin (); }
      auto end () noexcept { return this->vector_set.end (); }
      const auto end () const noexcept { return this->vector_set.end (); }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const kdtree<V>& f) {
    for (auto&& el : f.vector_set)
      os << el << std::endl;

    return os;

  }
  auto size() const { return this->vector_set.size(); }
  bool empty() { return this->vector_set.empty(); }
  auto begin() noexcept { return this->vector_set.begin(); }
  const auto begin() const noexcept { return this->vector_set.begin(); }
  auto end() noexcept { return this->vector_set.end(); }
  const auto end() const noexcept { return this->vector_set.end(); }
};

template <Vector V>
inline std::ostream &operator<<(std::ostream &os, const kdtree<V> &f) {
  for (auto &&el : f.vector_set)
    os << el << std::endl;

  return os;
}
} // namespace posets::utils
