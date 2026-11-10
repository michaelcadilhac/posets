#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/antichain.hh>
#include <posets/utils/block_sharingtrie.hh>
#include <posets/utils/reduce.hh>

namespace posets::downsets {
  // Forward definition for the operator<<s.
  template <Vector, size_t B = 8, bool Simd = true, bool Cache = true>
  class block_sharingtrie_backed;

  template <Vector V, size_t B, bool Simd, bool Cache>
  std::ostream& operator<< (std::ostream& os,
                            const block_sharingtrie_backed<V, B, Simd, Cache>& f);

  template <Vector V, size_t B, bool Simd, bool Cache>
  class block_sharingtrie_backed {
    private:
      utils::block_sharingtrie<V, B, Simd, Cache> trie;

      template <Vector V2, size_t B2, bool Simd2, bool Cache2>
      friend std::ostream& operator<< (std::ostream& os,
                                       const block_sharingtrie_backed<V2, B2, Simd2, Cache2>& f);

      struct proj {
          const V& operator() (const V* pv) { return *pv; }
          V&& operator() (V*&& pv) { return std::move (*pv); }
      };

      void reset_trie (std::vector<V>&& elements) {
        auto antichain = utils::reduce_to_maxima (std::move (elements));
        assert (utils::is_antichain (antichain));
        this->trie.relabel_trie (std::move (antichain));
      }

    public:
      using value_type = V;
      static constexpr size_t block_size = B;
      static constexpr bool uses_simd = Simd;
      static constexpr bool uses_cache = Cache;
      [[nodiscard]] auto storage () const { return trie.storage (); }

      block_sharingtrie_backed () = delete;

      block_sharingtrie_backed (std::vector<V>&& elements) { reset_trie (std::move (elements)); }

      block_sharingtrie_backed (V&& e) : trie (std::array<V, 1> {std::move (e)}) {}

      template <typename F>
      auto apply (const F& lambda) const {
        const auto& backing_vector = trie.get_backing_vector ();
        std::vector<V> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return block_sharingtrie_backed (std::move (ss));
      }

      block_sharingtrie_backed (const block_sharingtrie_backed&) = delete;
      block_sharingtrie_backed (block_sharingtrie_backed&&) = default;
      block_sharingtrie_backed& operator= (const block_sharingtrie_backed&) = delete;
      block_sharingtrie_backed& operator= (block_sharingtrie_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const { return this->trie.dominates (v); }

      // Union/intersection provide the strong guarantee for both operands:
      // build and allocate before publishing or moving any aliased generators.
      // Union in place
      void union_with (block_sharingtrie_backed&& other) {
        if (this == &other or other.size () == 0)
          return;
        if (this->size () == 0) {
          this->trie = std::move (other.trie);
          return;
        }

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

        if (result.size () == this->size ()) {
          bool unchanged = true;
          for (size_t i = 0; i < result.size (); ++i)
            unchanged and_eq result[i] == &this->trie.get_backing_vector ()[i];
          if (unchanged)
            return;
        }

        // ready to rebuild the tree now
        assert (not result.empty ());
        // Even debug validation can throw for a user-defined Vector. Do it
        // while projected sources still belong to the unchanged operands.
        assert (utils::is_antichain (result, proj ()));
        this->trie.relabel_trie (std::move (result), proj ());
      }

      // Intersection in place
      void intersect_with (const block_sharingtrie_backed& other) {
        if (this == &other or this->size () == 0)
          return;
        if (other.size () == 0) {
          reset_trie (std::vector<V> {});
          return;
        }

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
          // the set of maxima is different than what is in this->trie
          smaller_set or_eq not dominated;
        }

        // We can skip building trees and all if this->trie is the antichain
        // of maximal elements
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

  template <Vector V, size_t B, bool Simd, bool Cache>
  inline std::ostream& operator<< (std::ostream& os,
                                   const block_sharingtrie_backed<V, B, Simd, Cache>& f) {
    os << f.trie << std::endl;
    return os;
  }
}
