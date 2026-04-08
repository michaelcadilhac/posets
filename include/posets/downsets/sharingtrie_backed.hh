#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/sharingtrie.hh>

namespace posets::downsets {
  // Forward definition for the operator<<s.
  template <Vector>
  class sharingtrie_backed;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V>& f);

  // Finally the actual class definition
  template <Vector V>
  class sharingtrie_backed {
    private:
      utils::sharingtrie<V> trie;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V2>& f);

      struct proj {
          const V& operator() (const V* pv) { return *pv; }
          V&& operator() (V*&& pv) { return std::move (*pv); }
      };

      void reset_trie (std::vector<V>&& elements) noexcept {
        std::vector<V*> pelements;
        pelements.reserve (elements.size ());
        for (auto& e : elements)
          pelements.push_back (&e);

        // Sort and remove duplicates before building the trie so we can
        // iterate the trie's backing vector directly, avoiding get_all()'s
        // O(n*dim) allocation+copy purely for deduplication.
        std::sort (pelements.begin (), pelements.end (), [] (const V* v1, const V* v2) {
          for (size_t i = 0; i < v1->size (); ++i) {
            if ((*v1)[i] > (*v2)[i])
              return false;
            if ((*v1)[i] < (*v2)[i])
              return true;
          }
          return false;
        });
        size_t dups_pos = pelements.size ();
        for (size_t i = pelements.size () - 1; i > 0; --i)
          if (*pelements[i] == *pelements[i - 1])
            std::swap (pelements[i], pelements[--dups_pos]);
        if (dups_pos != pelements.size ())
          pelements.erase (pelements.begin () + dups_pos, pelements.end ());

        // Build the trie; backing vector now holds the deduplicated elements.
        auto antichain = std::vector<V*> ();
        antichain.reserve (pelements.size ());
        this->trie.relabel_trie (std::move (pelements), proj ());

        for (V& e : this->trie)
          if (not this->trie.dominates (e, true))
            antichain.push_back (&e);

        this->trie.relabel_trie (std::move (antichain), proj ());
        assert (this->trie.is_antichain ());
      }

    public:
      using value_type = V;

      sharingtrie_backed () = delete;

      sharingtrie_backed (std::vector<V>&& elements) noexcept {
        reset_trie (std::move (elements));
      }

      sharingtrie_backed (V&& e) : trie (std::array<V, 1> {std::move (e)}) {}

      template <typename F>
      auto apply (const F& lambda) const {
        const auto& backing_vector = trie.get_backing_vector ();
        std::vector<V> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return sharingtrie_backed (std::move (ss));
      }

      sharingtrie_backed (const sharingtrie_backed&) = delete;
      sharingtrie_backed (sharingtrie_backed&&) = default;
      sharingtrie_backed& operator= (const sharingtrie_backed&) = delete;
      sharingtrie_backed& operator= (sharingtrie_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const { return this->trie.dominates (v); }

      // Union in place
      void union_with (sharingtrie_backed&& other) {
        assert (other.size () > 0);
        std::vector<V*> result;
        result.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto& e : trie)
          if (not other.trie.dominates (e, true))
            result.push_back (&e);

        // for all elements in the other tree, if they are not dominated
        // (not necessarily strict) by this tree, we keep them
        for (auto& e : other.trie)
          if (not this->trie.dominates (e))
            result.push_back (&e);

        // ready to rebuild the tree now
        assert (not result.empty ());
        this->trie.relabel_trie (std::move (result), proj ());
        assert (this->trie.is_antichain ());
      }

      // Intersection in place
      void intersect_with (const sharingtrie_backed& other) {
        std::vector<V> intersection;
        bool smaller_set = false;

        for (auto& x : trie) {
          assert (x.size () > 0);

          // If x is part of the set of all meets, then x will dominate the
          // whole list! So we use this to short-circuit the computation: we
          // first check whether x will be there (which happens only if it is
          // itself dominated by some element in other)
          const bool dominated = other.trie.dominates (x);
          if (dominated)
            intersection.push_back (x.copy ());
          else
            for (auto& y : other)
              intersection.push_back (x.meet (y));

          // If x wasn't in the set of meets, dominated is false and
          // the set of minima is different than what is in this->trie
          smaller_set or_eq not dominated;
        }

        // We can skip building trees and all if this->trie is the antichain
        // of minimal elements
        if (not smaller_set)
          return;

        // Worst-case scenario: we do need to build trees
        reset_trie (std::move (intersection));
      }

      [[nodiscard]] auto size () const { return this->trie.size (); }

      [[nodiscard]] auto& get_backing_vector () { return trie.get_backing_vector (); }

      [[nodiscard]] const auto& get_backing_vector () const { return trie.get_backing_vector (); }

      [[nodiscard]] auto begin () { return this->trie.begin (); }
      [[nodiscard]] auto begin () const { return this->trie.begin (); }
      [[nodiscard]] auto end () { return this->trie.end (); }
      [[nodiscard]] auto end () const { return this->trie.end (); }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V>& f) {
    os << f.trie << std::endl;
    return os;
  }
}
