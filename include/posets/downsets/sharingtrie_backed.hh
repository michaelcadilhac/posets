#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/sharingtrie.hh>
#include <posets/utils/trie_algebra.hh>

namespace posets::downsets {
  // Forward definition for the operator<<s.
  template <Vector>
  class sharingtrie_backed;

  template <Vector V>
  std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V>& f);

  template <Vector V>
  class sharingtrie_backed {
    private:
      utils::sharingtrie<V> trie;

      template <Vector V2>
      friend std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V2>& f);

    public:
      using value_type = V;
      [[nodiscard]] auto storage () const { return trie.storage (); }

      sharingtrie_backed () = delete;

      sharingtrie_backed (std::vector<V>&& elements) {
        utils::reset_trie_maxima (trie, std::move (elements));
      }

      sharingtrie_backed (V&& e) : trie (std::array<V, 1> {std::move (e)}) {}

      template <typename F>
      [[nodiscard]] auto apply (const F& lambda) const {
        return utils::apply_trie_maxima<sharingtrie_backed> (trie, lambda);
      }

      sharingtrie_backed (const sharingtrie_backed&) = delete;
      sharingtrie_backed (sharingtrie_backed&&) = default;
      sharingtrie_backed& operator= (const sharingtrie_backed&) = delete;
      sharingtrie_backed& operator= (sharingtrie_backed&&) = default;

      [[nodiscard]] bool contains (const V& v) const { return this->trie.dominates (v); }

      // N8 transaction and strict duplicate retention are shared by both wrappers.
      void union_with (sharingtrie_backed&& other) { utils::union_trie_maxima (trie, other.trie); }

      void intersect_with (const sharingtrie_backed& other) {
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

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const sharingtrie_backed<V>& f) {
    os << f.trie << std::endl;
    return os;
  }
}
