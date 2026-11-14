#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/block_sharingtrie.hh>
#include <posets/utils/trie_algebra.hh>

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

    public:
      using value_type = V;
      static constexpr size_t block_size = B;
      static constexpr bool uses_simd = Simd;
      static constexpr bool uses_cache = Cache;
      [[nodiscard]] auto storage () const { return trie.storage (); }

      block_sharingtrie_backed () = delete;

      block_sharingtrie_backed (std::vector<V>&& elements) {
        utils::reset_trie_maxima (trie, std::move (elements));
      }

      block_sharingtrie_backed (V&& e) : trie (std::array<V, 1> {std::move (e)}) {}

      template <typename F>
      [[nodiscard]] auto apply (const F& lambda) const {
        return utils::apply_trie_maxima<block_sharingtrie_backed> (trie, lambda);
      }

      block_sharingtrie_backed (const block_sharingtrie_backed&) = delete;
      block_sharingtrie_backed (block_sharingtrie_backed&&) = default;
      block_sharingtrie_backed& operator= (const block_sharingtrie_backed&) = delete;
      block_sharingtrie_backed& operator= (block_sharingtrie_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const { return this->trie.dominates (v); }

      // N8 transaction and strict duplicate retention are shared by both wrappers.
      void union_with (block_sharingtrie_backed&& other) {
        utils::union_trie_maxima (trie, other.trie);
      }

      void intersect_with (const block_sharingtrie_backed& other) {
        utils::intersect_trie_maxima (trie, other.trie);
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
