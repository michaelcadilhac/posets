#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include <posets/concepts.hh>

// Tunable knobs (override at build time with -DSKIPLIST_MAX_LEVEL=N
// and -DSKIPLIST_BRANCHING_INV=K, where the per-level promotion
// probability is 1/K).  Defaults of 10 and 4 (p=0.25) were picked from
// a sweep on the syntcomp24 0s-20s suite; see the acacia-bonsai
// best_decomp_skiplist_mona benchmarks.
#ifndef SKIPLIST_MAX_LEVEL
# define SKIPLIST_MAX_LEVEL 10
#endif
#ifndef SKIPLIST_BRANCHING_INV
# define SKIPLIST_BRANCHING_INV 4
#endif

/*
 * A probabilistic skip list for variable-dimension vectors, sorted by
 * (L1 norm, then lexicographic).  The sort key enables pruning in dominance
 * queries: only elements with sum >= sum(v) can dominate v.
 *
 * Storage layout: a single std::vector<node> holds every node contiguously
 * (header at index 0); forward links are int32_t indices into that vector,
 * not pointers.  This keeps allocations amortized O(1) and makes traversal
 * cache-friendlier than the previous one-node-per-`new` layout, mirroring
 * the buffer pattern in utils::kdtree and utils::sharingtrie.  Removed
 * slots are reused via a free list threaded through forward[0].
 *
 * Coded to mirror the interface of posets::utils::kdtree.
 */
namespace posets::utils {
  template <Vector V>
  class skiplist {
    private:
      static constexpr int max_level = SKIPLIST_MAX_LEVEL;
      static constexpr int branching_inv = SKIPLIST_BRANCHING_INV;
      static_assert (max_level >= 1, "SKIPLIST_MAX_LEVEL must be >= 1");
      static_assert (branching_inv >= 2, "SKIPLIST_BRANCHING_INV must be >= 2");

      // Index sentinel for "null".  Indices fit comfortably in int32_t for
      // the antichain sizes we care about (millions of elements).
      static constexpr int32_t nil = -1;
      static constexpr int32_t header_idx = 0;

      struct node {
          V value;    // NOLINT(misc-non-private-member-variables-in-classes)
          int level;  // NOLINT(misc-non-private-member-variables-in-classes)
          std::array<int32_t, max_level>
              forward;  // NOLINT(misc-non-private-member-variables-in-classes)

          explicit node (V&& v, int lvl) : value (std::move (v)), level (lvl) {
            forward.fill (nil);
          }

          // Header sentinel: dummy 1-d V (never compared), all levels enabled.
          node () : value (1U), level (max_level - 1) { forward.fill (nil); }
      };

      // nodes[0] is the header.  Subsequent slots are either live (reachable
      // from header via level-0 forward links) or on the free list.
      std::vector<node> nodes;
      int32_t free_head {nil};
      int current_level {0};
      size_t list_size {0};
      size_t dim {0};
      mutable std::mt19937 rng {42};

      int get_sum (const V& v) const {
        int s = 0;
        for (size_t i = 0; i < dim; ++i)
          s += v[i];
        return s;
      }

      // Strict total order: (sum, lex).
      bool comes_before (const V& a, const V& b) const {
        const int sa = get_sum (a);
        const int sb = get_sum (b);
        if (sa != sb)
          return sa < sb;
        for (size_t i = 0; i < dim; ++i) {
          if (a[i] < b[i])
            return true;
          if (a[i] > b[i])
            return false;
        }
        return false;
      }

      int random_level () {
        int lvl = 0;
        while (lvl < max_level - 1 and
               std::uniform_int_distribution<> (0, branching_inv - 1) (rng) == 0)
          ++lvl;
        return lvl;
      }

      // Allocate a node slot for `v` at level `lvl`.  Reuses a free-list slot
      // if available, else appends to the nodes vector.  Returns the index;
      // pointer-stable across vector growth (callers should hold indices).
      int32_t alloc_node (V&& v, int lvl) {
        if (free_head != nil) {
          const int32_t idx = free_head;
          // The dead slot was on the free list with forward[0] threaded to
          // the next free node.  Read that before overwriting.
          free_head = nodes[idx].forward[0];
          nodes[idx].value = std::move (v);
          nodes[idx].level = lvl;
          nodes[idx].forward.fill (nil);
          return idx;
        }
        nodes.emplace_back (std::move (v), lvl);
        return static_cast<int32_t> (nodes.size () - 1);
      }

      void free_node (int32_t idx) {
        // Park the slot on the free list.  forward[0] becomes the link to
        // the next free node; level/value are left as is and overwritten on
        // the next alloc_node.
        nodes[idx].forward[0] = free_head;
        free_head = idx;
      }

      // Fill update[] with the index of the last node at each level that
      // comes before v.
      void find_update (const V& v, std::array<int32_t, max_level>& update) const {
        int32_t cur = header_idx;
        for (int i = current_level; i >= 0; --i) {
          int32_t next;
          while ((next = nodes[cur].forward[i]) != nil and comes_before (nodes[next].value, v))
            cur = next;
          update[i] = cur;
        }
        for (int i = current_level + 1; i < max_level; ++i)
          update[i] = header_idx;
      }

      // Splice node `nd` out, given update[] populated with its predecessors.
      void unlink_node (int32_t nd, const std::array<int32_t, max_level>& update) {
        const int lvl = nodes[nd].level;
        for (int i = 0; i <= lvl; ++i) {
          if (nodes[update[i]].forward[i] != nd)
            break;
          nodes[update[i]].forward[i] = nodes[nd].forward[i];
        }
        while (current_level > 0 and nodes[header_idx].forward[current_level] == nil)
          --current_level;
        --list_size;
      }

    public:
      skiplist () {  // NOLINT(modernize-use-equals-default)
        // Reserve a small batch up-front so the very first inserts don't
        // realloc; arbitrary modest size, grows geometrically afterwards.
        nodes.reserve (16);
        nodes.emplace_back ();  // header at index 0
      }

      ~skiplist () = default;

      skiplist (skiplist&& other) noexcept
        : nodes (std::move (other.nodes)),
          free_head (other.free_head),
          current_level (other.current_level),
          list_size (other.list_size),
          dim (other.dim),
          rng (other.rng) {
        // Keep the moved-from object destructible and assignable without
        // allocating inside this noexcept move.  Mutating it recreates the
        // header lazily in push().
        other.nodes.clear ();
        other.free_head = nil;
        other.current_level = 0;
        other.list_size = 0;
        other.dim = 0;
      }

      skiplist& operator= (skiplist&& other) noexcept {
        if (this == &other)
          return *this;
        nodes = std::move (other.nodes);
        free_head = other.free_head;
        current_level = other.current_level;
        list_size = other.list_size;
        dim = other.dim;
        rng = other.rng;
        other.nodes.clear ();
        other.free_head = nil;
        other.current_level = 0;
        other.list_size = 0;
        other.dim = 0;
        return *this;
      }

      skiplist (const skiplist&) = delete;
      skiplist& operator= (const skiplist&) = delete;

      // Insert v without checking dominance (caller is responsible).
      void push (V&& v) {
        if (nodes.empty ())
          nodes.emplace_back ();
        if (dim == 0)
          dim = v.size ();
        std::array<int32_t, max_level> update {};
        find_update (v, update);

        const int lvl = random_level ();
        if (lvl > current_level) {
          for (int i = current_level + 1; i <= lvl; ++i)
            update[i] = header_idx;
          current_level = lvl;
        }

        // alloc_node may realloc nodes; do it before threading update[] in.
        // update[] holds indices, so it survives the realloc unscathed.
        const int32_t new_idx = alloc_node (std::move (v), lvl);
        for (int i = 0; i <= lvl; ++i) {
          nodes[new_idx].forward[i] = nodes[update[i]].forward[i];
          nodes[update[i]].forward[i] = new_idx;
        }
        ++list_size;
      }

      // Return true if any element dominates v (i.e. v is in the downset).
      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        if (list_size == 0)
          return false;
        const int sv = get_sum (v);
        // Fast-forward to the first element with sum >= sv via the upper
        // levels, then linear-scan at level 0.
        int32_t cur = header_idx;
        for (int i = current_level; i >= 0; --i) {
          int32_t next;
          while ((next = nodes[cur].forward[i]) != nil and get_sum (nodes[next].value) < sv)
            cur = next;
        }
        cur = nodes[cur].forward[0];
        while (cur != nil) {
          auto po = v.partial_order (nodes[cur].value);
          if (strict ? (po.leq () and not po.geq ()) : po.leq ())
            return true;
          cur = nodes[cur].forward[0];
        }
        return false;
      }

      // Remove all elements strictly dominated by v.
      void remove_dominated_by (const V& v) {
        if (list_size == 0)
          return;
        const int sv = get_sum (v);
        std::array<int32_t, max_level> update {};
        update.fill (header_idx);

        // Predecessors at each level as we walk the prefix.
        std::array<int32_t, max_level> prev {};
        prev.fill (header_idx);

        int32_t cur = nodes[header_idx].forward[0];
        while (cur != nil and get_sum (nodes[cur].value) <= sv) {
          auto po = v.partial_order (nodes[cur].value);
          const int32_t next = nodes[cur].forward[0];
          if (po.geq ()) {
            const int lvl = nodes[cur].level;
            for (int i = 0; i <= lvl; ++i)
              update[i] = prev[i];
            unlink_node (cur, update);
            free_node (cur);
          }
          else {
            const int lvl = nodes[cur].level;
            for (int i = 0; i <= lvl; ++i)
              prev[i] = cur;
          }
          cur = next;
        }
      }

      // Move all values out, leaving the list empty.
      [[nodiscard]] std::vector<V> drain () {
        std::vector<V> result;
        result.reserve (list_size);
        if (nodes.empty ())
          return result;
        int32_t cur = nodes[header_idx].forward[0];
        while (cur != nil) {
          result.push_back (std::move (nodes[cur].value));
          cur = nodes[cur].forward[0];
        }
        clear ();
        return result;
      }

      // Reset to an empty list (just the header).  Releases all node memory.
      void clear () {
        nodes.clear ();
        nodes.emplace_back ();  // fresh header
        free_head = nil;
        current_level = 0;
        list_size = 0;
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it1 = begin (); it1 != end (); ++it1) {
          auto it2 = it1;
          ++it2;
          for (; it2 != end (); ++it2) {
            auto po = it1->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        }
        return true;
      }

      [[nodiscard]] auto size () const { return list_size; }
      [[nodiscard]] bool empty () const { return list_size == 0; }

      // Forward iterator over the level-0 chain.  Holds an index + a back-
      // pointer to the owning skiplist so it can dereference; references to
      // elements remain stable as long as no insertion is performed during
      // iteration (insertions may realloc the underlying nodes vector).
      struct const_iterator {
          using iterator_category = std::forward_iterator_tag;
          using value_type = V;
          using difference_type = std::ptrdiff_t;
          using pointer = const V*;
          using reference = const V&;

          const_iterator () : sl (nullptr), cur (nil) {}

          const V& operator* () const { return sl->nodes[cur].value; }
          const V* operator->() const { return &sl->nodes[cur].value; }

          const_iterator& operator++ () {
            cur = sl->nodes[cur].forward[0];
            return *this;
          }

          const_iterator operator++ (int) {
            const_iterator tmp = *this;
            cur = sl->nodes[cur].forward[0];
            return tmp;
          }

          bool operator== (const const_iterator& o) const { return cur == o.cur; }
          bool operator!= (const const_iterator& o) const { return cur != o.cur; }

        private:
          const skiplist* sl;
          int32_t cur;
          friend class skiplist;
          const_iterator (const skiplist* s, int32_t c) : sl (s), cur (c) {}
      };

      [[nodiscard]] const_iterator begin () const {
        if (nodes.empty ())
          return end ();
        return const_iterator (this, nodes[header_idx].forward[0]);
      }
      [[nodiscard]] const_iterator end () const { return const_iterator (this, nil); }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const skiplist<V>& sl) {
    for (const auto& el : sl)
      os << el << '\n';
    return os;
  }
}
