#pragma once

#include <algorithm>
#include <cassert>
#include <iterator>
#include <ostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/antichain.hh>
#include <posets/utils/bboxtree.hh>
#include <posets/utils/reduce.hh>

namespace posets::downsets {
  template <Vector V>
  class bboxtree_backed {
      utils::bboxtree<V> tree;

      void reset_tree (std::vector<V>&& elements) {
        std::vector<utils::vector_sum_t<V>> keys;
        auto antichain = utils::reduce_to_maxima (std::move (elements), &keys);
        tree.assign_and_build (std::move (antichain), std::move (keys));
        assert (tree.is_antichain ());
      }

    public:
      using value_type = V;

      bboxtree_backed () = delete;
      explicit bboxtree_backed (std::vector<V>&& elements) { reset_tree (std::move (elements)); }
      explicit bboxtree_backed (V&& element) {
        std::vector<V> singleton;
        std::vector<utils::vector_sum_t<V>> keys;
        keys.push_back (utils::sum_key (element));
        singleton.push_back (std::move (element));
        tree.assign_and_build (std::move (singleton), std::move (keys));
      }
      bboxtree_backed (const bboxtree_backed&) = delete;
      bboxtree_backed (bboxtree_backed&&) = default;
      bboxtree_backed& operator= (const bboxtree_backed&) = delete;
      bboxtree_backed& operator= (bboxtree_backed&&) = default;

      [[nodiscard]] bool contains (const V& value) const { return tree.dominates (value); }
      [[nodiscard]] size_t size () const { return tree.size (); }

      void union_with (bboxtree_backed&& other) {
        const size_t this_size = size ();
        const size_t other_size = other.size ();
        if (other_size == 0)
          return;
        if (this_size == 0) {
          tree = std::move (other.tree);
          return;
        }
        std::vector<size_t> other_survivors;
        other_survivors.reserve (other_size);
        for (size_t i = 0; i < other_size; ++i) {
          const utils::vector_sum_t<V> key = other.tree.get_keys ()[i];
          if (key > tree.max_key () or
              not tree.dominates (other.tree.get_backing_vector ()[i], false, key))
            other_survivors.push_back (i);
        }
        if (other_survivors.empty ())
          return;

        std::vector<size_t> this_survivors;
        this_survivors.reserve (this_size);
        for (size_t i = 0; i < this_size; ++i) {
          const utils::vector_sum_t<V> key = tree.get_keys ()[i];
          if (key > other.tree.max_key () or
              not other.tree.dominates (tree.get_backing_vector ()[i], true, key))
            this_survivors.push_back (i);
        }

        if (this_survivors.empty () and other_survivors.size () == other_size) {
          tree = std::move (other.tree);
          return;
        }

        std::vector<V> merged;
        std::vector<utils::vector_sum_t<V>> merged_keys;
        merged.reserve (this_survivors.size () + other_survivors.size ());
        merged_keys.reserve (this_survivors.size () + other_survivors.size ());
        size_t lhs = 0;
        size_t rhs = 0;
        while (lhs < this_survivors.size () and rhs < other_survivors.size ()) {
          const size_t lhs_index = this_survivors[lhs];
          const size_t rhs_index = other_survivors[rhs];
          if (tree.get_keys ()[lhs_index] >= other.tree.get_keys ()[rhs_index]) {
            merged_keys.push_back (tree.get_keys ()[lhs_index]);
            merged.push_back (std::move (tree.get_backing_vector ()[lhs_index]));
            ++lhs;
          }
          else {
            merged_keys.push_back (other.tree.get_keys ()[rhs_index]);
            merged.push_back (std::move (other.tree.get_backing_vector ()[rhs_index]));
            ++rhs;
          }
        }
        while (lhs < this_survivors.size ()) {
          const size_t index = this_survivors[lhs++];
          merged_keys.push_back (tree.get_keys ()[index]);
          merged.push_back (std::move (tree.get_backing_vector ()[index]));
        }
        while (rhs < other_survivors.size ()) {
          const size_t index = other_survivors[rhs++];
          merged_keys.push_back (other.tree.get_keys ()[index]);
          merged.push_back (std::move (other.tree.get_backing_vector ()[index]));
        }
        tree.assign_and_build (std::move (merged), std::move (merged_keys));
      }

      void intersect_with (const bboxtree_backed& other) {
        if (size () == 0)
          return;
        if (other.size () == 0) {
          tree = utils::bboxtree<V> {};
          return;
        }
        constexpr size_t incremental_limit = 400000;
        const bool incremental = size () <= incremental_limit / other.size ();
        std::vector<V> intersection;
        bool smaller_set = false;
        for (size_t i = 0; i < tree.size (); ++i) {
          const auto& x = tree.get_backing_vector ()[i];
          const bool dominated = other.tree.dominates (x, false, tree.get_keys ()[i]);
          if (dominated) {
            if (incremental)
              (void) utils::insert_maximum (intersection, x.copy ());
            else
              intersection.push_back (x.copy ());
          }
          else
            for (const auto& y : other) {
              auto meet = x.meet (y);
              if (incremental)
                (void) utils::insert_maximum (intersection, std::move (meet));
              else
                intersection.push_back (std::move (meet));
            }
          smaller_set or_eq not dominated;
        }
        if (smaller_set)
          reset_tree (std::move (intersection));
      }

      template <typename F>
      bboxtree_backed apply (const F& lambda) const {
        std::vector<V> result;
        result.reserve (size ());
        for (const auto& value : tree)
          result.push_back (lambda (value));
        return bboxtree_backed (std::move (result));
      }

      [[nodiscard]] auto begin () { return tree.begin (); }
      [[nodiscard]] auto begin () const { return tree.begin (); }
      [[nodiscard]] auto end () { return tree.end (); }
      [[nodiscard]] auto end () const { return tree.end (); }
      [[nodiscard]] auto& get_backing_vector () { return tree.get_backing_vector (); }
      [[nodiscard]] const auto& get_backing_vector () const { return tree.get_backing_vector (); }
      [[nodiscard]] bool corners_built () const { return tree.corners_built (); }
      [[nodiscard]] size_t bbox_node_count () const { return tree.node_count (); }

#ifdef POSETS_BBOX_STATS
      void print_bbox_stats (std::ostream& os) const { tree.print_stats (os); }
#endif
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const bboxtree_backed<V>& downset) {
    for (const auto& value : downset)
      os << value << '\n';
    return os;
  }
}
