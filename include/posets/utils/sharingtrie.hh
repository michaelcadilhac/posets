#pragma once

#include <type_traits>
#include <unordered_map>

#include <algorithm>
#include <boost/functional/hash.hpp>
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/config.hh>
#include <posets/utils/antichain.hh>
#include <posets/utils/storage_stats.hh>

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
      size_t dim {0};
      int root {-1};
      size_t allocated_nodes {0};
      size_t reachable_nodes {0};
      struct st_node {
          typename V::value_type label;
          int color;
          int son;
          int bro;
      };
      // Runtime-sized storage, initialized by build_nodes rather than zero-filled.
      using node_array = st_node[];  // NOLINT(modernize-avoid-c-arrays)
      std::unique_ptr<node_array> bin_tree;
      std::vector<V> vector_set;

      // Colors are globally unique across layers, so (color, strictness)
      // already identifies a query state without a redundant depth dimension.
      // Each entry holds the last generation that visited it. Unsigned wrap
      // clears the table before restarting at generation 1.
      mutable std::vector<unsigned> dominates_stamp;
      mutable unsigned dominates_gen {0};
      mutable std::vector<std::tuple<int, short, bool>> dominates_stack;

      // We need to compare subtrees (assuming the trie construction
      // has been applied)
      using signature_type = std::vector<std::common_type_t<typename V::value_type, int>>;
      struct signature_hash {
          std::size_t operator() (const signature_type& v) const {
            return boost::hash_range (v.begin (), v.end ());
          }
      };

      // We change the sibling pointers/indices of children of nodes[start..end)
      // so that they form a single set of siblings
      void string_children (const std::vector<int>& nodes, size_t start, size_t end) {
        // we fetch the first sibling, if there is one
        const st_node* cur = this->bin_tree.get () + nodes[start];
        if (cur->son == -1)
          return;
        st_node* last = this->bin_tree.get () + cur->son;
        // now, for all remaining nodes we
        // (1) get to the last sibling
        // (2) link to the next first sibling and
        for (size_t i = start + 1; i < end; i++) {
          while (last->bro > -1)
            last = this->bin_tree.get () + last->bro;
          cur = this->bin_tree.get () + nodes[i];
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
            const st_node* cur = this->bin_tree.get () + idx;
            if (cur->son > -1) {
              to_visit.emplace_back (idx, 2);
              to_visit.emplace_back (cur->son, 0);
            }
          }
          else if (mode == 2) {
            // we are going right, so look for a sibling and push it into the
            // stack in mode=down
            const st_node* cur = this->bin_tree.get () + idx;
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
              prev = this->bin_tree.get () + sibs[i];
              i = j;
            }
            prev->bro = -1;
            // now, we either repair the root, or the top-of-stack node
            if (not to_visit.empty ()) {
              const auto [idx_parent, mode_parent] = to_visit.back ();
              assert (mode_parent == 2);  // went down already, next time right
              st_node* parent = this->bin_tree.get () + idx_parent;
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

#if POSETS_SHARINGTRIE_COMPACT_NODES
      void compact_nodes () {
        std::vector<int> pending;
        pending.reserve (dim);
        pending.push_back (root);
        size_t count = 0;
        while (not pending.empty ()) {
          const auto& current = bin_tree[pending.back ()];
          pending.pop_back ();
          ++count;
          if (current.bro >= 0)
            pending.push_back (current.bro);
          if (current.son >= 0)
            pending.push_back (current.son);
        }
        if (count == allocated_nodes)
          return;
        auto compact = std::make_unique_for_overwrite<node_array> (count);
        struct link {
            int old;
            int parent;
            bool sibling;
        };
        std::vector<link> links;
        links.reserve (dim);
        links.push_back ({root, -1, false});
        int next = 0;
        while (not links.empty ()) {
          const auto [old, parent, sibling] = links.back ();
          links.pop_back ();
          const auto& current = bin_tree[old];
          const int index = next++;
          compact[index] = {current.label, 0, -1, -1};
          if (parent >= 0) {
            if (sibling)
              compact[parent].bro = index;
            else
              compact[parent].son = index;
          }
          if (current.bro >= 0)
            links.push_back ({current.bro, index, true});
          if (current.son >= 0)
            links.push_back ({current.son, index, false});
        }
        bin_tree.swap (compact);
        root = 0;
        allocated_nodes = count;
      }
#endif

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
          const st_node* cur = this->bin_tree.get () + idx;

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

        for (const auto& entries : layer)
          reachable_nodes += entries.size ();

        // Color equal suffix languages bottom-up. Assign each color at its
        // first signature, avoiding a separate vector of indices per class.
        // Reuse signature scratch and hash buckets between layers.
        int nxt_color = 0;
        signature_type k;
        std::unordered_map<signature_type, int, signature_hash> colors;
        for (int i = this->dim - 1; i >= 0; i--) {
          colors.clear ();
          for (const int idx : layer[i]) {
            const st_node* cur = this->bin_tree.get () + idx;
            k.clear ();
            k.push_back (cur->label);
            int son_idx = cur->son;
            while (son_idx > -1) {
              cur = this->bin_tree.get () + son_idx;
              k.push_back (cur->color);
              son_idx = cur->bro;
            }
            const auto [entry, inserted] = colors.try_emplace (k, nxt_color);
            this->bin_tree[idx].color = entry->second;
            nxt_color += static_cast<int> (inserted);
          }
        }
        // Set up the generation-stamp cache and DFS stack for dominates()
        this->dominates_stamp.assign (static_cast<size_t> (nxt_color) * 2, 0U);
        this->dominates_gen = 0U;
        this->dominates_stack.reserve (this->dim);
      }

      // Build only unpublished state. Values stay untouched until all node,
      // coloring and query-cache allocations have succeeded.
      template <class R, class Proj>
      void build_nodes (const R& elements, Proj proj) {
        if (elements.empty ())
          return;
        this->dim = proj (*elements.begin ()).size ();
        assert (this->dim > 0);
        // Node links use int indices. Check before multiplying or allocating.
        if (this->dim > static_cast<size_t> (std::numeric_limits<int>::max ()) / elements.size ())
          throw std::length_error ("sharingtrie node index capacity exceeded");
        this->bin_tree = std::make_unique_for_overwrite<node_array> (this->dim * elements.size ());
        this->allocated_nodes = this->dim * elements.size ();
        this->root = 0;

        // creating linear trees with their roots being siblings
        int idx = 0;
        st_node* prev_root = nullptr;
        for (const auto& element : elements) {
          const auto& e = proj (element);
          assert (e.size () == this->dim);
          bool first_comp = true;
          st_node* prev_node;
          st_node* cur_node = this->bin_tree.get () + idx;
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
            else  // Link consecutive components as parent and child.
              prev_node->son = idx;
            prev_node = cur_node;
            idx += 1;
            cur_node += 1;
          }
        }

        // now, we make a trie/suffix tree (making sure the children are in
        // decreasing label order)
        this->to_trie ();
#if POSETS_SHARINGTRIE_COMPACT_NODES
        compact_nodes ();
#endif

        // Color equivalent suffix languages and prepare the query caches.
        this->color_as_dfa ();
      }

    public:
      // Strong guarantee for this trie, including aliased projected values.
      // Projection must only select values, without modifying them. External
      // inputs are moved only after every potentially throwing build step; for
      // throwing vector moves (or value-returning projections) we use copy().
      template <std::ranges::input_range R, class Proj = std::identity>
      void relabel_trie (R&& elements, Proj proj = {}) {
        sharingtrie replacement;
        replacement.vector_set.reserve (elements.size ());
        using projected_type =
            std::invoke_result_t<Proj&, std::ranges::range_rvalue_reference_t<R>>;
        if constexpr (std::is_same_v<projected_type, V&&> and
                      std::is_nothrow_move_constructible_v<V>) {
          // Resolve all projections before moving any values. This also makes
          // late projection/traversal failures safe when sources alias *this.
          std::vector<V*> sources;
          sources.reserve (elements.size ());
          for (auto&& e : elements | std::views::reverse) {
            auto&& value = proj (std::move (e));
            sources.push_back (std::addressof (value));
          }
          replacement.build_nodes (sources, [] (const V* value) -> const V& { return *value; });
          for (auto* value : sources)
            replacement.vector_set.push_back (std::move (*value));
        }
        else {
          for (auto&& e : elements | std::views::reverse)
            replacement.vector_set.push_back (proj (std::move (e)).copy ());
          replacement.build_nodes (replacement.vector_set, std::identity {});
        }
        swap (replacement);
      }

      template <std::ranges::input_range R, class Proj = std::identity>
      sharingtrie (R&& elements, Proj proj = {}) {
        relabel_trie (std::forward<R> (elements), proj);
      }

      sharingtrie () = default;
      sharingtrie (size_t dim) : dim (dim) {}
      sharingtrie (size_t dim, size_t initsize)
        : dim (dim),
          allocated_nodes (initsize),
          bin_tree (std::make_unique_for_overwrite<node_array> (initsize)) {}
      sharingtrie (const sharingtrie&) = delete;
      sharingtrie (sharingtrie&& other) noexcept { swap (other); }
      sharingtrie& operator= (sharingtrie&& other) noexcept {
        if (this != &other) {
          sharingtrie replacement (std::move (other));
          swap (replacement);
        }
        return *this;
      }

      void swap (sharingtrie& other) noexcept {
        using std::swap;
        swap (dim, other.dim);
        swap (root, other.root);
        swap (allocated_nodes, other.allocated_nodes);
        swap (reachable_nodes, other.reachable_nodes);
        swap (bin_tree, other.bin_tree);
        swap (vector_set, other.vector_set);
        swap (dominates_stamp, other.dominates_stamp);
        swap (dominates_gen, other.dominates_gen);
        swap (dominates_stack, other.dominates_stack);
      }

      // Check, for a given vector, whether some vector in this sharingtrie
      // dominates it. We explicitly avoid making this recursive as
      // experiments show large-dimensional vectors may make this overflow
      // otherwise.
      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        if (this->vector_set.empty ())
          return false;

        // This is essentially going to be a DFS where we check for domination
        // at each level/dimension and stopping when it does not hold (recall
        // we have ordered labels in decreasing fashion, so no need to look at
        // the right subtrees afterwards). To speed things up, we keep track
        // of visited colors and strictness per level.

        // Bump the generation counter; on unsigned wrap-around to 0 (roughly
        // every 4 billion calls) clear the stamp array and restart at 1.
        if (++this->dominates_gen == 0U) {
          std::ranges::fill (this->dominates_stamp.begin (), this->dominates_stamp.end (), 0U);
          ++this->dominates_gen;
        }

        // DFS: reuse the member stack (capacity already reserved to dim).
        auto& to_visit = this->dominates_stack;
        to_visit.clear ();
        to_visit.emplace_back (this->root, 0, strict);

        bool ret = false;
        while (not to_visit.empty ()) {
          assert (to_visit.size () <= this->dim);
          const auto [idx, direction, loc_strict] = to_visit.back ();
          to_visit.pop_back ();
          const st_node* cur = this->bin_tree.get () + idx;

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
              const size_t strict_color =
                  (static_cast<size_t> (cur->color) * 2) + static_cast<size_t> (new_strict);
              unsigned& stamp = this->dominates_stamp[strict_color];
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
        if (this->vector_set.empty ())
          return {};

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
          const st_node* cur = this->bin_tree.get () + idx;

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

      [[nodiscard]] storage_stats storage () const {
        return {
            sizeof (*this),
            sizeof (st_node),
            reachable_nodes,
            allocated_nodes,
            allocated_nodes * sizeof (st_node),
            0,
            reachable_nodes * sizeof (typename V::value_type),
            vector_set.capacity () * sizeof (V),
            dominates_stack.capacity () * sizeof (typename decltype (dominates_stack)::value_type),
            dominates_stamp.capacity () * sizeof (unsigned)};
      }
      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
      [[nodiscard]] bool is_antichain () const { return utils::is_antichain (vector_set); }
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
