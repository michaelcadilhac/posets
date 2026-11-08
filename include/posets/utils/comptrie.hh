#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <tuple>
#include <vector>

#include <posets/concepts.hh>
#include <posets/config.hh>
#include <posets/utils/block_compare.hh>
#include <posets/utils/storage_stats.hh>

namespace posets::utils {
  // A path-compressed left-child/right-sibling trie.  Stored vectors stay in
  // their original coordinate order; only trie labels use the pinned
  // per-dimension permutation.
  template <Vector V>
  class comptrie {
      struct ct_node {
          uint32_t label_off;
          uint32_t label_len;
          int32_t son;
          int32_t bro;
      };

      std::vector<V> vector_set;
      std::vector<ct_node> nodes;
      std::vector<typename V::value_type> label_pool;
      std::shared_ptr<const std::vector<uint32_t>> perm;
      int32_t root {-1};
#if POSETS_COMPTRIE_SIMD_PATHS
      mutable std::vector<typename V::value_type> packed_query;
#endif
      mutable std::vector<std::tuple<int32_t, uint32_t, bool>> dominates_stack;
      static std::map<size_t, std::weak_ptr<const std::vector<uint32_t>>> perm_map;

      [[nodiscard]] static std::shared_ptr<const std::vector<uint32_t>> identity_perm (
          size_t dim) {
        auto result = std::make_shared<std::vector<uint32_t>> (dim);
        std::iota (result->begin (), result->end (), 0);
        return result;
      }

      void select_permutation () {
        const size_t dim = vector_set.front ().size ();
        if (auto existing = perm_map[dim].lock ()) {
          perm = std::move (existing);
          return;
        }
        if (vector_set.size () < 16) {
          perm = identity_perm (dim);
          return;
        }

        auto selected = std::make_shared<std::vector<uint32_t>> (dim);
        std::iota (selected->begin (), selected->end (), 0);
        std::vector<uint16_t> distinct (dim);
        for (size_t d = 0; d < dim; ++d) {
          std::array<bool, 256> seen {};
          for (const auto& value : vector_set)
            seen[static_cast<unsigned char> (value[d])] = true;
          distinct[d] = std::count (seen.begin (), seen.end (), true);  // NOLINT(boost-use-ranges)
        }
        std::stable_sort (
            selected->begin (), selected->end (),
            [&distinct] (uint32_t lhs, uint32_t rhs) { return distinct[lhs] > distinct[rhs]; });
        perm = selected;
        perm_map[dim] = selected;
      }

      // The recursion follows trie depth and is bounded by the vector dimension.
      // NOLINTBEGIN(misc-no-recursion)
      [[nodiscard]] int32_t build_level (const std::vector<uint32_t>& order, size_t begin,
                                         size_t end, uint32_t depth) {
        int32_t first = -1;
        int32_t previous = -1;
        size_t group_begin = begin;
        while (group_begin < end) {
          size_t group_end = group_begin + 1;
          const auto first_label = vector_set[order[group_begin]][(*perm)[depth]];
          while (group_end < end and vector_set[order[group_end]][(*perm)[depth]] == first_label)
            ++group_end;

          uint32_t span = 1;
          const uint32_t dim = perm->size ();
          while (depth + span < dim) {
            const auto label = vector_set[order[group_begin]][(*perm)[depth + span]];
            bool common = true;
            for (size_t i = group_begin + 1; i < group_end; ++i)
              common and_eq vector_set[order[i]][(*perm)[depth + span]] == label;
            if (not common)
              break;
            ++span;
          }

          const int32_t current = nodes.size ();
          const uint32_t label_off = label_pool.size ();
          for (uint32_t offset = 0; offset < span; ++offset)
            label_pool.push_back (vector_set[order[group_begin]][(*perm)[depth + offset]]);
          nodes.push_back ({label_off, span, -1, -1});
          if (first == -1)
            first = current;
          if (previous != -1)
            nodes[previous].bro = current;
          previous = current;

          if (depth + span < dim)
            nodes[current].son = build_level (order, group_begin, group_end, depth + span);
          group_begin = group_end;
        }
        return first;
      }
      // NOLINTEND(misc-no-recursion)

    public:
      comptrie () = default;
      comptrie (const comptrie&) = delete;
      comptrie (comptrie&&) = default;
      comptrie& operator= (const comptrie&) = delete;
      comptrie& operator= (comptrie&&) = default;

      explicit comptrie (std::vector<V>&& elements) { assign_and_build (std::move (elements)); }

      void assign_and_build (std::vector<V>&& sorted_antichain) {
        assert (not sorted_antichain.empty ());
        vector_set = std::move (sorted_antichain);
        select_permutation ();
        nodes.clear ();
        label_pool.clear ();

        std::vector<uint32_t> order (vector_set.size ());
        std::iota (order.begin (), order.end (), 0);  // NOLINT(boost-use-ranges)
        std::stable_sort (order.begin (), order.end (), [this] (uint32_t lhs, uint32_t rhs) {
          for (const uint32_t d : *perm)
            if (vector_set[lhs][d] != vector_set[rhs][d])
              return vector_set[lhs][d] > vector_set[rhs][d];
          return false;
        });
        root = build_level (order, 0, order.size (), 0);
        // One deferred sibling per coordinate bounds the query stack.
        dominates_stack.reserve (std::min (nodes.size (), perm->size ()));
#if POSETS_COMPTRIE_SIMD_PATHS
        packed_query.resize (perm->size ());
#endif
      }

      [[nodiscard]] bool dominates (const V& value, bool strict = false) const {
        if (root < 0)
          return false;
#if POSETS_COMPTRIE_SIMD_PATHS
        // Pay permutation and packing once per query, including on early failures.
        for (size_t d = 0; d < perm->size (); ++d)
          packed_query[d] = value[(*perm)[d]];
#endif
        dominates_stack.clear ();
        dominates_stack.emplace_back (root, 0, strict);
        while (not dominates_stack.empty ()) {
          auto [index, depth, owe_strict] = dominates_stack.back ();
          dominates_stack.pop_back ();
          if (index < 0)
            continue;
          const auto& current = nodes[index];

          const auto first_label = label_pool[current.label_off];
          const auto first_query = value[(*perm)[depth]];
          if (first_label < first_query)
            continue;  // siblings are descending too
          if (current.bro >= 0)
            dominates_stack.emplace_back (current.bro, depth, owe_strict);

#if POSETS_COMPTRIE_SIMD_PATHS
          using T = typename V::value_type;
          const bool matches = dominates_block<T, simd_traits<T>::simd_size> (
              label_pool.data () + current.label_off, packed_query.data () + depth,
              current.label_len, owe_strict);
#else
          bool matches = true;
          for (uint32_t offset = 0; offset < current.label_len; ++offset) {
            const auto label = label_pool[current.label_off + offset];
            const auto query = value[(*perm)[depth + offset]];
            if (label < query) {
              matches = false;
              break;
            }
            if (label > query)
              owe_strict = false;
          }
#endif
          if (not matches)
            continue;
          if (current.son < 0) {
            if (not owe_strict)
              return true;
          }
          else
            dominates_stack.emplace_back (current.son, depth + current.label_len, owe_strict);
        }
        return false;
      }

      [[nodiscard]] storage_stats storage () const {
        return {sizeof (*this),
                sizeof (ct_node),
                nodes.size (),
                nodes.capacity (),
                nodes.capacity () * sizeof (ct_node),
                label_pool.capacity () * sizeof (typename V::value_type),
                label_pool.size () * sizeof (typename V::value_type),
                vector_set.capacity () * sizeof (V),
                (dominates_stack.capacity () *
                 sizeof (typename decltype (dominates_stack)::value_type)) +
                    (perm ? perm->capacity () * sizeof (uint32_t) : 0)
#if POSETS_COMPTRIE_SIMD_PATHS
                    + packed_query.capacity () * sizeof (typename V::value_type)
#endif
                    ,
                0};
      }
      [[nodiscard]] size_t size () const { return vector_set.size (); }
      [[nodiscard]] size_t node_count () const { return nodes.size (); }
      [[nodiscard]] size_t label_count () const { return label_pool.size (); }
      [[nodiscard]] std::vector<V> get_all () const {
        std::vector<V> result;
        result.reserve (vector_set.size ());
        for (const auto& value : vector_set)
          result.push_back (value.copy ());
        return result;
      }
      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }
      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
      [[nodiscard]] const auto& permutation () const { return *perm; }

      [[nodiscard]] bool has_single_child_chain () const {
        for (const auto& current : nodes)
          if (current.son >= 0 and nodes[current.son].bro < 0 and current.bro < 0)
            return true;
        return false;
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it = vector_set.begin (); it != vector_set.end (); ++it)
          for (auto jt = it + 1; jt != vector_set.end (); ++jt) {
            auto po = it->partial_order (*jt);
            if (po.leq () or po.geq ())
              return false;
          }
        return true;
      }
  };

  template <Vector V>
  std::map<size_t, std::weak_ptr<const std::vector<uint32_t>>> comptrie<V>::perm_map;
}
