#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

#include <posets/concepts.hh>

/*
 * A probabilistic skip list for variable-dimension vectors, sorted by
 * (L1 norm, then lexicographic).  The sort key enables pruning in dominance
 * queries: only elements with sum >= sum(v) can dominate v.
 *
 * Coded to mirror the interface of posets::utils::kdtree.
 */
namespace posets::utils {
  template <Vector V>
  class skiplist {
    private:
      static constexpr int MAX_LEVEL = 16;

      struct Node {
          V value;
          int level;
          Node* forward[MAX_LEVEL];

          explicit Node (V&& v, int lvl) : value (std::move (v)), level (lvl) {
            std::fill (forward, forward + MAX_LEVEL, nullptr);
          }

          // Header sentinel: no value, all levels enabled.
          Node () : value (1u), level (MAX_LEVEL - 1) {
            std::fill (forward, forward + MAX_LEVEL, nullptr);
          }
      };

      Node* header;
      int current_level;
      size_t list_size;
      size_t dim;
      mutable std::mt19937 rng{42};

      int get_sum (const V& v) const {
        int s = 0;
        for (size_t i = 0; i < dim; ++i)
          s += v[i];
        return s;
      }

      // Strict total order: (sum, lex).
      bool comes_before (const V& a, const V& b) const {
        int sa = get_sum (a);
        int sb = get_sum (b);
        if (sa != sb)
          return sa < sb;
        for (size_t i = 0; i < dim; ++i) {
          if (a[i] < b[i])
            return true;
          if (a[i] > b[i])
            return false;
        }
        return false;  // equal
      }

      int random_level () {
        int lvl = 0;
        while (lvl < MAX_LEVEL - 1 && std::uniform_int_distribution<> (0, 1)(rng) == 1)
          ++lvl;
        return lvl;
      }

      // Fill update[] with the last node at each level that comes before v.
      void find_update (const V& v, Node* update[MAX_LEVEL]) const {
        Node* cur = header;
        for (int i = current_level; i >= 0; --i) {
          while (cur->forward[i] && comes_before (cur->forward[i]->value, v))
            cur = cur->forward[i];
          update[i] = cur;
        }
        // Zero out levels above current_level (they stay at header).
        for (int i = current_level + 1; i < MAX_LEVEL; ++i)
          update[i] = header;
      }

      // Remove a node given the update array pointing to its predecessors.
      void unlink_node (Node* node, Node* update[MAX_LEVEL]) {
        for (int i = 0; i <= node->level; ++i) {
          if (update[i]->forward[i] != node)
            break;
          update[i]->forward[i] = node->forward[i];
        }
        while (current_level > 0 && header->forward[current_level] == nullptr)
          --current_level;
        --list_size;
      }

    public:
      skiplist () : header (new Node ()), current_level (0), list_size (0), dim (0) {}

      ~skiplist () {
        Node* cur = header;
        while (cur) {
          Node* next = cur->forward[0];
          delete cur;
          cur = next;
        }
      }

      skiplist (skiplist&& other) noexcept
        : header (other.header),
          current_level (other.current_level),
          list_size (other.list_size),
          dim (other.dim),
          rng (std::move (other.rng)) {
        other.header = new Node ();
        other.current_level = 0;
        other.list_size = 0;
      }

      skiplist& operator= (skiplist&& other) noexcept {
        if (this == &other)
          return *this;
        // Free current contents.
        Node* cur = header->forward[0];
        while (cur) {
          Node* next = cur->forward[0];
          delete cur;
          cur = next;
        }
        delete header;
        header = other.header;
        current_level = other.current_level;
        list_size = other.list_size;
        dim = other.dim;
        rng = std::move (other.rng);
        other.header = new Node ();
        other.current_level = 0;
        other.list_size = 0;
        return *this;
      }

      skiplist (const skiplist&) = delete;
      skiplist& operator= (const skiplist&) = delete;

      // Insert v without checking dominance (caller is responsible).
      void push (V&& v) {
        if (dim == 0)
          dim = v.size ();
        Node* update[MAX_LEVEL];
        find_update (v, update);

        int lvl = random_level ();
        if (lvl > current_level) {
          for (int i = current_level + 1; i <= lvl; ++i)
            update[i] = header;
          current_level = lvl;
        }

        Node* new_node = new Node (std::move (v), lvl);
        for (int i = 0; i <= lvl; ++i) {
          new_node->forward[i] = update[i]->forward[i];
          update[i]->forward[i] = new_node;
        }
        ++list_size;
      }

      // Return true if any element dominates v (i.e. v is in the downset).
      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        if (list_size == 0)
          return false;
        int sv = get_sum (v);
        // Fast-forward to first element with sum >= sv.
        Node* cur = header;
        for (int i = current_level; i >= 0; --i) {
          while (cur->forward[i] && get_sum (cur->forward[i]->value) < sv)
            cur = cur->forward[i];
        }
        cur = cur->forward[0];
        while (cur) {
          auto po = v.partial_order (cur->value);
          if (strict ? (po.leq () && !po.geq ()) : po.leq ())
            return true;
          cur = cur->forward[0];
        }
        return false;
      }

      // Remove all elements strictly dominated by v.
      void remove_dominated_by (const V& v) {
        if (list_size == 0)
          return;
        int sv = get_sum (v);
        // Only elements with sum <= sv can be dominated by v.
        Node* update[MAX_LEVEL];
        std::fill (update, update + MAX_LEVEL, header);

        // Track predecessors at each level as we scan the prefix.
        Node* prev[MAX_LEVEL];
        std::fill (prev, prev + MAX_LEVEL, header);

        Node* cur = header->forward[0];
        while (cur && get_sum (cur->value) <= sv) {
          auto po = v.partial_order (cur->value);
          Node* next = cur->forward[0];
          if (po.geq ()) {
            // Build update array for this node.
            for (int i = 0; i <= cur->level; ++i)
              update[i] = prev[i];
            unlink_node (cur, update);
            delete cur;
          } else {
            // Advance prev pointers.
            for (int i = 0; i <= cur->level; ++i)
              prev[i] = cur;
          }
          cur = next;
        }
        // Advance prev for all remaining levels if we didn't reach those nodes.
        // (Already handled above; unlink_node adjusts current_level.)
      }

      // Move all values out, leaving the list empty.
      [[nodiscard]] std::vector<V> drain () {
        std::vector<V> result;
        result.reserve (list_size);
        Node* cur = header->forward[0];
        while (cur) {
          result.push_back (std::move (cur->value));
          cur = cur->forward[0];
        }
        clear ();
        return result;
      }

      // Delete all data nodes.
      void clear () {
        Node* cur = header->forward[0];
        while (cur) {
          Node* next = cur->forward[0];
          delete cur;
          cur = next;
        }
        std::fill (header->forward, header->forward + MAX_LEVEL, nullptr);
        current_level = 0;
        list_size = 0;
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it1 = begin (); it1 != end (); ++it1) {
          auto it2 = it1;
          ++it2;
          for (; it2 != end (); ++it2) {
            auto po = it1->partial_order (*it2);
            if (po.leq () || po.geq ())
              return false;
          }
        }
        return true;
      }

      [[nodiscard]] auto size () const { return list_size; }
      [[nodiscard]] bool empty () const { return list_size == 0; }

      // Forward iterator over the level-0 chain.
      struct const_iterator {
          using iterator_category = std::forward_iterator_tag;
          using value_type = V;
          using difference_type = std::ptrdiff_t;
          using pointer = const V*;
          using reference = const V&;

          Node* cur;

          const V& operator* () const { return cur->value; }
          const V* operator-> () const { return &cur->value; }

          const_iterator& operator++ () {
            cur = cur->forward[0];
            return *this;
          }

          const_iterator operator++ (int) {
            const_iterator tmp = *this;
            cur = cur->forward[0];
            return tmp;
          }

          bool operator== (const const_iterator& o) const { return cur == o.cur; }
          bool operator!= (const const_iterator& o) const { return cur != o.cur; }
      };

      [[nodiscard]] const_iterator begin () const { return {header->forward[0]}; }
      [[nodiscard]] const_iterator end () const { return {nullptr}; }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const skiplist<V>& sl) {
    for (const auto& el : sl)
      os << el << '\n';
    return os;
  }
}
