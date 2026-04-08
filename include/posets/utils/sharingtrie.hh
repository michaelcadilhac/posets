#pragma once

#include <unordered_map>

#include <algorithm>
#include <boost/functional/hash.hpp>
#include <cassert>
#include <iostream>
#include <ranges>
#include <tuple>
#include <vector>

#include <posets/concepts.hh>

namespace posets::utils {

  // Forward definition for the operator<<
  template <Vector>
  class sharingtrie;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const utils::sharingtrie<V>& f);

  template <Vector V>
  class sharingtrie {
    private:
      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const utils::sharingtrie<V2>& f);

      // This is a left-child right-sibling implementation of the sharing
      // tree, so we need nodes (with indices of a son and a brother)
      // and a single array of nodes to store them all. We also keep a color,
      // this will be used to define equivalence classes (as we DO NOT reduce
      // to a DFA/DAG, instead storing the intermediate trie).
      size_t dim;
      int root;
      struct st_node {
          typename V::value_type label;
          int color;
          int son;
          int bro;
      };
      st_node* bin_tree;
      size_t bt_size;
      std::vector<V> vector_set;

      // Generation-stamp cache for dominates(): avoids allocating hash sets on
      // every call.  After color_as_dfa() runs, dominates_stamp has size
      // dim * (nxt_color * 2); entry [depth * nxt_color*2 + strict_color] holds
      // the generation when that (depth, strict_color) pair was last seen.
      // dominates() bumps dominates_gen on entry; on the rare unsigned wrap to 0
      // the stamp array is cleared and the generation restarted at 1.
      int nxt_color {0};
      mutable std::vector<unsigned> dominates_stamp;
      mutable unsigned dominates_gen {0};
      mutable std::vector<std::tuple<int, short, bool>> dominates_stack;

      // We need to compare subtrees (assuming the trie construction
      // has been applied)
      struct intvec_hash {
          std::size_t operator() (const std::vector<int>& v) const {
            return boost::hash_range (v.begin (), v.end ());
          }
      };

      // We change the sibling pointers/indices of children of nodes[start..end)
      // so that they form a single set of siblings
      void string_children (const std::vector<int>& nodes, size_t start, size_t end) {
        // we fetch the first sibling, if there is one
        st_node* cur = this->bin_tree + nodes[start];
        if (cur->son == -1)
          return;
        st_node* last = this->bin_tree + cur->son;
        // now, for all remaining nodes we
        // (1) get to the last sibling
        // (2) link to the next first sibling and
        for (size_t i = start + 1; i < end; i++) {
          while (last->bro > -1)
            last = this->bin_tree + last->bro;
          cur = this->bin_tree + nodes[i];
          assert (cur->son > -1);
          last->bro = cur->son;
        }
      }

      void to_trie () {
        // A vector used as a stack of node indices and
        // modes (0 reorder siblings, 1 down, 2 right); bounded by dim so we
        // can reserve upfront and avoid std::deque chunked allocation
        std::vector<std::tuple<int, short>> to_visit;
        to_visit.reserve (this->dim);
        to_visit.emplace_back (this->root, 0);
        // reused across mode-0 iterations to avoid repeated heap allocation
        std::vector<int> sibs;

        while (not to_visit.empty ()) {
          assert (to_visit.size () <= this->dim);
          const auto [idx, mode] = to_visit.back ();
          to_visit.pop_back ();

          if (mode == 1) {
            // we are going down, so look for a son and push it into the
            // stack; before that, we also push back the current node but in
            // mode=right so that when we come back we move to its sibling
            st_node* cur = this->bin_tree + idx;
            if (cur->son > -1) {
              to_visit.emplace_back (idx, 2);
              to_visit.emplace_back (cur->son, 0);
            }
          }
          else if (mode == 2) {
            // we are going right, so look for a sibling and push it into the
            // stack in mode=down
            st_node* cur = this->bin_tree + idx;
            if (cur->bro > -1)
              to_visit.emplace_back (cur->bro, 1);
          }
          else if (mode == 0) {
            // collect siblings into a vector and sort by label descending
            // (single allocation reused across iterations, avoids map overhead)
            sibs.clear ();
            int sib_idx = idx;
            while (sib_idx > -1) {
              sibs.push_back (sib_idx);
              sib_idx = this->bin_tree[sib_idx].bro;
            }
            std::sort (sibs.begin (), sibs.end (), [this] (int a, int b) {
              return this->bin_tree[a].label > this->bin_tree[b].label;
            });
            // process groups of same-label siblings and relink bro pointers
            int head = sibs[0];
            st_node* prev = nullptr;
            size_t i = 0;
            while (i < sibs.size ()) {
              const auto label = this->bin_tree[sibs[i]].label;
              size_t j = i + 1;
              while (j < sibs.size () and this->bin_tree[sibs[j]].label == label)
                j++;
              string_children (sibs, i, j);
              if (prev == nullptr)
                head = sibs[i];
              else
                prev->bro = sibs[i];
              prev = this->bin_tree + sibs[i];
              i = j;
            }
            prev->bro = -1;
            // now, we either repair the root, or the top-of-stack node
            if (not to_visit.empty ()) {
              const auto [idx_parent, mode_parent] = to_visit.back ();
              assert (mode_parent == 2);  // went down already, next time right
              st_node* parent = this->bin_tree + idx_parent;
              parent->son = head;
            }
            else
              this->root = head;
            // finally we put the head back in the stack in mode=down
            to_visit.emplace_back (head, 1);
          }
          else
            assert (false);
        }
      }

      void color_as_dfa () {
        std::vector<std::vector<int>> layer (dim);

        // We collect the indices of nodes per layer via a DFS. For this, we
        // use a vector as a stack of node indices and directions (0 down, 1 right);
        // bounded by dim so we reserve upfront
        std::vector<std::tuple<int, short>> to_visit;
        to_visit.reserve (this->dim);
        to_visit.emplace_back (this->root, 0);

        while (not to_visit.empty ()) {
          assert (to_visit.size () <= this->dim);
          const auto [idx, direction] = to_visit.back ();
          to_visit.pop_back ();
          st_node* cur = this->bin_tree + idx;

          // base case: reached the bottom layer
          if (cur->son == -1) {
            assert (to_visit.size () == this->dim - 1);
            assert (direction == 0);  // leaves only reached going down
            layer[to_visit.size ()].push_back (idx);
            // is there a sibling?
            if (cur->bro > -1)
              to_visit.emplace_back (cur->bro, 0);
          }
          // recursive case: we need to push something into the stack
          else {
            assert (to_visit.size () < this->dim - 1);
            // either this is the first time we've seen the node, and we need
            // to go down pushing a reminder of the next time not being the
            // first, and its child...
            if (direction == 0) {
              assert (cur->son > -1);
              layer[to_visit.size ()].push_back (idx);
              to_visit.emplace_back (idx, 1);
              to_visit.emplace_back (cur->son, 0);
            }
            // or we're already going right and we need to push its
            // sibling (and start by going down from there)
            else if (direction == 1) {
              if (cur->bro > -1)
                to_visit.emplace_back (cur->bro, 0);
            }
            else
              assert (false);
          }
        }

        // Now, per layer (in bottom-up fashion) we use a hash table to assign
        // "colors" to the nodes based on their label and the colors of their
        // children. Both the key vector and the map are hoisted out of the
        // inner loop to reuse their heap allocations across iterations.
        int nxt_color = 0;
        std::vector<int> k;
        std::unordered_map<std::vector<int>, std::vector<int>, intvec_hash> colors2indices;
        for (int i = this->dim - 1; i >= 0; i--) {
          colors2indices.clear ();
          for (const int idx : layer[i]) {
            st_node* cur = this->bin_tree + idx;
            k.clear ();
            k.push_back (cur->label);
            int son_idx = cur->son;
            while (son_idx > -1) {
              cur = this->bin_tree + son_idx;
              k.push_back (cur->color);
              son_idx = cur->bro;
            }
            colors2indices[k].push_back (idx);
          }
          // Every node in this layer is ready to get its equivalence-class
          // color
          for (const auto& [key, idces] : colors2indices) {
            for (const int idx : idces) {
              st_node* cur = this->bin_tree + idx;
              cur->color = nxt_color;
            }
            nxt_color += 1;
          }
        }
        // Set up the generation-stamp cache and DFS stack for dominates()
        this->nxt_color = nxt_color;
        this->dominates_stamp.assign (this->dim * (nxt_color * 2), 0U);
        this->dominates_gen = 0U;
        this->dominates_stack.reserve (this->dim);
      }

    public:
      template <std::ranges::input_range R, class Proj = std::identity>
      void relabel_trie (R&& elements, Proj proj = {}) {
        this->dim = (proj (*elements.begin ()).size ());

        // sanity checks
        assert (elements.size () > 0);
        assert (this->dim > 0);

        // allocating memory
        if (this->bin_tree == nullptr) {
          this->bt_size = this->dim * elements.size ();
          this->bin_tree = new st_node[bt_size];
        }
        else if (this->bt_size < this->dim * elements.size ()) {
          delete[] this->bin_tree;
          this->bt_size = this->dim * elements.size ();
          this->bin_tree = new st_node[bt_size];
        }

        this->root = 0;

        // moving the given elements to the internal data structure
        std::vector<V> newset;
        newset.reserve (elements.size ());
        for (auto&& e : elements | std::views::reverse)
          newset.push_back (proj (std::move (e)));
        this->vector_set = std::move (newset);
        // WARNING: avoid using elements from here onward

        // creating linear trees with their roots being siblings
        int idx = 0;
        st_node* prev_root = nullptr;
        for (const auto& e : this->vector_set) {
          bool first_comp = true;
          st_node* prev_node;
          st_node* cur_node = this->bin_tree + idx;
          for (size_t c = 0; c < e.size (); c++) {
            cur_node->label = e[c];
            cur_node->bro = -1;
            cur_node->son = -1;
            // if it's the first component of the vector and there is a
            // previous root, then we can update that root so that this new
            // one is its sibling; otherwise we still store the current node
            // as previous root
            if (first_comp) {
              if (prev_root != nullptr)
                prev_root->bro = idx;
              prev_root = cur_node;
              first_comp = false;
            }
            else  // there is a previous component, so we make this one its sibling
              prev_node->son = idx;
            prev_node = cur_node;
            idx += 1;
            cur_node += 1;
          }
        }

        // now, we make a trie/suffix tree (making sure the children are in
        // decreasing label order)
        this->to_trie ();

        // finally, we proceed bottom-up to merge language equivalent nodes
        this->color_as_dfa ();
      }

      template <std::ranges::input_range R, class Proj = std::identity>
      sharingtrie (R&& elements, Proj proj = {})
        : dim (proj (*elements.begin ()).size ()),
          bin_tree (nullptr) {
        relabel_trie (std::forward<R> (elements), proj);
      }

      sharingtrie () : bin_tree (nullptr) {}
      sharingtrie (size_t dim) : dim (dim), bin_tree (nullptr) {}
      sharingtrie (size_t dim, size_t initsize) : dim (dim) {
        this->bin_tree = new st_node[initsize];
      }
      sharingtrie (const sharingtrie& other) = delete;
      sharingtrie (sharingtrie&& other) noexcept
        : dim (other.dim),
          root (other.root),
          bin_tree (other.bin_tree),
          bt_size (other.bt_size),
          vector_set (std::move (other.vector_set)),
          nxt_color (other.nxt_color),
          dominates_stamp (std::move (other.dominates_stamp)),
          dominates_gen (other.dominates_gen),
          dominates_stack (std::move (other.dominates_stack)) {
        other.bin_tree = nullptr;
      }
      ~sharingtrie () { delete[] this->bin_tree; }
      sharingtrie& operator= (sharingtrie&& other) noexcept {
        this->dim = other.dim;
        this->root = other.root;
        this->bt_size = other.bt_size;
        this->vector_set = std::move (other.vector_set);
        this->nxt_color = other.nxt_color;
        this->dominates_stamp = std::move (other.dominates_stamp);
        this->dominates_gen = other.dominates_gen;
        this->dominates_stack = std::move (other.dominates_stack);
        // WARNING: 3 variable follows to make the whole thing safe for
        // self-assignment
        st_node* temp_tree = other.bin_tree;
        other.bin_tree = nullptr;
        // NOTE: this must be here, after having a local copy of the other
        // tree and before moving it to here, because of self-assignment
        // safety!
        delete[] this->bin_tree;
        // we now copy things here and return
        this->bin_tree = temp_tree;
        return *this;
      }

      // Check, for a given vector, whether some vector in this sharingtrie
      // dominates it. We explicitly avoid making this recursive as
      // experiments show large-dimensional vectors may make this overflow
      // otherwise.
      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        // This is essentially going to be a DFS where we check for domination
        // at each level/dimension and stopping when it does not hold (recall
        // we have ordered things in increasing fashion, so no need to look at
        // the right subtrees afterwards). To speed things up, we keep track
        // of visited colors and strictness per level.

        // Bump the generation counter; on unsigned wrap-around to 0 (roughly
        // every 4 billion calls) clear the stamp array and restart at 1.
        if (++this->dominates_gen == 0U) {
          std::fill (this->dominates_stamp.begin (), this->dominates_stamp.end (), 0U);
          ++this->dominates_gen;
        }
        const int sc_stride = this->nxt_color * 2;

        // DFS: reuse the member stack (capacity already reserved to dim).
        auto& to_visit = this->dominates_stack;
        to_visit.clear ();
        to_visit.emplace_back (this->root, 0, strict);

        bool ret = false;
        while (not to_visit.empty ()) {
          assert (to_visit.size () <= this->dim);
          const auto [idx, direction, loc_strict] = to_visit.back ();
          to_visit.pop_back ();
          st_node* cur = this->bin_tree + idx;

          // if we're already going right, we need to push its
          // sibling and start by going down from there with the same local
          // strictness
          if (direction == 1) {
            // leaves only reached going down
            assert (to_visit.size () < this->dim - 1);
            if (cur->bro > -1)
              to_visit.emplace_back (cur->bro, 0, loc_strict);
          }
          else if (direction == 0) {
            // This is a general check, if this does not hold, we can ignore
            // the subtree and the siblings (since children have been sorted in
            // decreasing order).
            const typename V::value_type v_comp = v[to_visit.size ()];
            if (cur->label < v_comp)
              continue;
            const bool new_strict = loc_strict and (cur->label == v_comp);

            // base case: reached the bottom layer
            if (cur->son == -1) {
              assert (to_visit.size () == this->dim - 1);
              // we can stop and declare domination if we don't still owe
              // a strict domination
              if (not new_strict) {
                ret = true;
                break;
              }
              // otherwise, we just backtrack and avoid the siblings
            }
            // recursive case: we may need to push something into the stack
            else {
              assert (to_visit.size () < this->dim - 1);
              // before actually checking this subtree, we check if we've
              // visited an equivalent one (using the generation-stamp cache)
              // and otherwise mark it for the future; skipping = go to sibling
              const int strict_color = (cur->color << 1) + (new_strict ? 1 : 0);
              const size_t depth = to_visit.size ();
              unsigned& stamp = this->dominates_stamp[depth * sc_stride + strict_color];
              if (stamp == this->dominates_gen) {
                if (cur->bro > -1)
                  to_visit.emplace_back (cur->bro, 0, loc_strict);
              }
              else {
                stamp = this->dominates_gen;
                to_visit.emplace_back (idx, 1, loc_strict);
                to_visit.emplace_back (cur->son, 0, new_strict);
              }
            }
          }
          else
            assert (false);
        }
        return ret;
      }

      [[nodiscard]] std::vector<V> get_all () const {
        // A vector used as a stack of node indices and directions (0 down, 1 right)
        std::vector<std::tuple<int, short>> to_visit;
        to_visit.reserve (this->dim);
        to_visit.emplace_back (this->root, 0);
        std::vector<V> res;
        std::vector<typename V::value_type> temp;

        while (not to_visit.empty ()) {
          assert (to_visit.size () <= this->dim);
          const auto [idx, direction] = to_visit.back ();
          to_visit.pop_back ();
          st_node* cur = this->bin_tree + idx;

          // base case: reached the bottom layer
          if (cur->son == -1) {
            assert (to_visit.size () == this->dim - 1);
            temp.push_back (cur->label);
            std::vector<typename V::value_type> cpy {temp};
            res.push_back (V (std::move (cpy)));
            temp.pop_back ();
            // is there a sibling with another label?
            if (cur->bro > -1)
              to_visit.emplace_back (cur->bro, 0);
          }
          // recursive case: we need to push something into the stack
          else {
            assert (to_visit.size () < this->dim - 1);
            // either this is the first time we've seen the node, and we need
            // to go down pushing a reminder of the next time not being the
            // first, and its child...
            if (direction == 0) {
              assert (cur->son > -1);
              to_visit.emplace_back (idx, 1);
              temp.push_back (cur->label);
              to_visit.emplace_back (cur->son, 0);
            }
            // or we're already going right and we need to push its
            // sibling (and start by going down from there)
            else if (direction == 1) {
              temp.pop_back ();
              if (cur->bro > -1)
                to_visit.emplace_back (cur->bro, 0);
            }
            else
              assert (false);
          }
        }

        return res;
      }

      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
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
      [[nodiscard]] bool operator== (const sharingtrie& other) const {
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
  inline std::ostream& operator<< (std::ostream& os, const sharingtrie<V>& f) {
    for (auto&& el : f.get_all ())
      os << el << '\n';
    return os;
  }

}  // namespace posets::utils
