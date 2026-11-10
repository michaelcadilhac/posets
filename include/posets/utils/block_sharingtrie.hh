#pragma once

#include <type_traits>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/antichain.hh>
#include <posets/utils/block_compare.hh>
#include <posets/utils/storage_stats.hh>

namespace posets::utils {
  // Experimental fixed consecutive blocks. SIMD width and block size are independent.
  // Queries seek a stored vector >= the query componentwise; strict additionally
  // requires an unequal coordinate. Signed -1 is an ordinary component value.
  // Packing, DFS scratch and generation stamps are mutable: queries on one
  // instance require external synchronization; they must not race with rebuilds.
  template <Vector V, size_t B = 8, bool Simd = true, bool Cache = true>
  class block_sharingtrie {
      static_assert (B > 0);
      using value_type = V::value_type;
      using index_type = uint32_t;
      struct node {
          index_type first_child;
          index_type child_count;
          index_type color;
      };
      struct query_frame {
          index_type next;
          index_type end;
          size_t depth;
          bool owe_strict;
      };
      struct build_frame {
          size_t begin;
          size_t end;
          size_t depth;
          index_type parent;
      };
      static constexpr index_type no_parent = std::numeric_limits<index_type>::max ();
      size_t dim {0};
      size_t logical_labels {0};
      index_type root_count {0};
      std::vector<node> nodes;
      // Every label has B contiguous lanes; final-block padding is never compared.
      std::vector<value_type> labels;
      std::vector<V> vector_set;
      mutable std::vector<value_type> query;
      mutable std::vector<query_frame> stack;
      mutable std::vector<unsigned> stamps;
      mutable unsigned generation {0};

      template <class R, class Proj>
      void build_nodes (const R& elements, Proj proj) {
        if (elements.empty ())
          return;
        dim = proj (*elements.begin ()).size ();
        if (dim == 0)
          throw std::invalid_argument ("block_sharingtrie requires nonzero dimension");
        for (const auto& element : elements)
          if (proj (element).size () != dim)
            throw std::invalid_argument ("block_sharingtrie dimension mismatch");
        const size_t blocks = (dim / B) + (dim % B != 0);
        if (elements.size () > static_cast<size_t> (no_parent) / blocks)
          throw std::length_error ("block_sharingtrie node index capacity exceeded");

        std::vector<size_t> order (elements.size ());
        std::iota (order.begin (), order.end (), 0);  // NOLINT(boost-use-ranges)
        // Keep input Vector storage opaque, including packed Boolean wrappers.
        std::vector<const V*> values;
        values.reserve (elements.size ());
        for (const auto& element : elements)
          values.push_back (std::addressof (proj (element)));
        std::sort (order.begin (), order.end (), [&] (size_t lhs, size_t rhs) {
          for (size_t d = 0; d < dim; ++d)
            if ((*values[lhs])[d] != (*values[rhs])[d])
              return (*values[lhs])[d] > (*values[rhs])[d];
          return false;
        });
        std::vector<build_frame> pending {{0, order.size (), 0, no_parent}};
        std::vector<size_t> depths;
        while (not pending.empty ()) {
          const auto [begin, end, depth, parent] = pending.back ();
          pending.pop_back ();
          const index_type first = nodes.size ();
          const size_t count = std::min (B, dim - depth);
          // Allocate a complete sibling group before any of its descendants.
          for (size_t i = begin; i < end;) {
            size_t j = i + 1;
            while (j < end) {
              bool equal = true;
              for (size_t d = 0; d < count; ++d)
                equal and_eq (*values[order[i]])[depth + d] == (*values[order[j]])[depth + d];
              if (not equal)
                break;
              ++j;
            }
            const index_type index = nodes.size ();
            nodes.push_back ({0, 0, 0});
            if constexpr (Cache)
              depths.push_back (depth);
            logical_labels += count;
            for (size_t d = 0; d < B; ++d)
              labels.push_back (d < count ? (*values[order[i]])[depth + d] : value_type {0});
            if (depth + count < dim)
              pending.push_back ({i, j, depth + count, index});
            i = j;
          }
          const index_type siblings = nodes.size () - first;
          if (parent == no_parent) {
            root_count = siblings;
          }
          else {
            nodes[parent].first_child = first;
            nodes[parent].child_count = siblings;
          }
        }

        if constexpr (Cache) {
          // Reuse input-order storage to group nodes by depth, children before parents.
          order.resize (nodes.size ());
          std::iota (order.begin (), order.end (), 0);  // NOLINT(boost-use-ranges)
          std::ranges::sort (
              order, [&depths] (size_t lhs, size_t rhs) { return depths[lhs] > depths[rhs]; });
          const auto signature_less = [this] (size_t lhs, size_t rhs) {
            for (size_t d = 0; d < B; ++d) {
              const auto left = labels[(lhs * B) + d];
              const auto right = labels[(rhs * B) + d];
              if (left != right)
                return left < right;
            }
            const auto& left = nodes[lhs];
            const auto& right = nodes[rhs];
            for (index_type c = 0; c < std::min (left.child_count, right.child_count); ++c) {
              const auto lc = nodes[left.first_child + c].color;
              const auto rc = nodes[right.first_child + c].color;
              if (lc != rc)
                return lc < rc;
            }
            return left.child_count < right.child_count;
          };
          index_type next_color = 0;
          for (size_t begin = 0; begin < order.size ();) {
            size_t end = begin + 1;
            while (end < order.size () and depths[order[end]] == depths[order[begin]])
              ++end;
            // All child colors are complete; own colors do not affect comparisons.
            std::sort (order.begin () + static_cast<std::ptrdiff_t> (begin),
                       order.begin () + static_cast<std::ptrdiff_t> (end), signature_less);
            index_type color = next_color++;
            nodes[order[begin]].color = color;
            for (size_t i = begin + 1; i < end; ++i) {
              if (signature_less (order[i - 1], order[i]))
                color = next_color++;
              nodes[order[i]].color = color;
            }
            begin = end;
          }
          stamps.assign (size_t {2} * next_color, 0U);
        }
        query.resize (dim);
        stack.reserve (blocks);
      }

    public:
      static constexpr size_t block_size = B;
      static constexpr bool uses_simd = Simd;
      static constexpr bool uses_cache = Cache;
      [[nodiscard]] size_t root_branches () const { return root_count; }

      // Strong guarantee for this trie, including aliased projected values.
      // Projection must only select values, without modifying them. External
      // inputs are moved only after every potentially throwing build step; for
      // throwing vector moves (or value-returning projections) we use copy().
      template <std::ranges::input_range R, class Proj = std::identity>
      void relabel_trie (R&& elements, Proj proj = {}) {
        block_sharingtrie replacement;
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
      block_sharingtrie (R&& elements, Proj proj = {}) {
        relabel_trie (std::forward<R> (elements), proj);
      }
      block_sharingtrie () = default;
      block_sharingtrie (const block_sharingtrie&) = delete;
      block_sharingtrie (block_sharingtrie&& other) noexcept { swap (other); }
      block_sharingtrie& operator= (block_sharingtrie&& other) noexcept {
        if (this != &other) {
          block_sharingtrie replacement (std::move (other));
          swap (replacement);
        }
        return *this;
      }
      void swap (block_sharingtrie& other) noexcept {
        using std::swap;
        swap (dim, other.dim);
        swap (logical_labels, other.logical_labels);
        swap (root_count, other.root_count);
        swap (nodes, other.nodes);
        swap (labels, other.labels);
        swap (vector_set, other.vector_set);
        swap (query, other.query);
        swap (stack, other.stack);
        swap (stamps, other.stamps);
        swap (generation, other.generation);
      }

      [[nodiscard]] bool dominates (const V& value, bool strict = false) const {
        if (vector_set.empty ())
          return false;
        if (value.size () != dim)
          throw std::invalid_argument ("block_sharingtrie query dimension mismatch");
        // Packing cost belongs to every query; no assumptions about Vector::data().
        for (size_t d = 0; d < dim; ++d)
          query[d] = value[d];
        if constexpr (Cache) {
          if (++generation == 0U) {
            std::ranges::fill (stamps, 0U);
            ++generation;
          }
        }
        stack.clear ();
        stack.push_back ({0, root_count, 0, strict});
        while (not stack.empty ()) {
          auto& frame = stack.back ();
          if (frame.next == frame.end) {
            stack.pop_back ();
            continue;
          }
          const size_t index = frame.next++;
          const size_t depth = frame.depth;
          bool owe_strict = frame.owe_strict;
          const auto* label = labels.data () + (index * B);
          // Only the first coordinate orders the entire remaining sibling group.
          if (label[0] < query[depth]) {
            stack.pop_back ();
            continue;
          }
          // A later failed coordinate cannot discard incomparable siblings.
          if (not dominates_block<
                  value_type,
                  std::min (B, static_cast<size_t> (simd_traits<value_type>::simd_size)), Simd> (
                  label, query.data () + depth, std::min (B, dim - depth), owe_strict))
            continue;
          const auto& current = nodes[index];
          if (current.child_count == 0) {
            if (not owe_strict)
              return true;
            continue;
          }
          if constexpr (Cache) {
            auto& stamp = stamps[(size_t {2} * current.color) + static_cast<size_t> (owe_strict)];
            if (stamp == generation)
              continue;
            stamp = generation;
          }
          stack.push_back ({current.first_child, current.first_child + current.child_count,
                            depth + B, owe_strict});
        }
        return false;
      }

      [[nodiscard]] storage_stats storage () const {
        return {
            sizeof (*this),
            sizeof (node),
            nodes.size (),
            nodes.capacity (),
            nodes.capacity () * sizeof (node),
            labels.capacity () * sizeof (value_type),
            logical_labels * sizeof (value_type),
            vector_set.capacity () * sizeof (V),
            (query.capacity () * sizeof (value_type)) + (stack.capacity () * sizeof (query_frame)),
            stamps.capacity () * sizeof (unsigned)};
      }
      [[nodiscard]] std::vector<V> get_all () const {
        std::vector<V> result;
        for (const auto& value : vector_set)
          if (std::ranges::none_of (result, [&] (const V& v) { return v == value; }))
            result.push_back (value.copy ());
        return result;
      }
      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
      [[nodiscard]] bool is_antichain () const { return utils::is_antichain (vector_set); }
      [[nodiscard]] bool operator== (const block_sharingtrie& other) const {
        return vector_set == other.vector_set;
      }
      [[nodiscard]] auto size () const { return vector_set.size (); }
      [[nodiscard]] bool empty () const { return vector_set.empty (); }
      [[nodiscard]] auto begin () noexcept { return vector_set.begin (); }
      [[nodiscard]] auto begin () const noexcept { return vector_set.begin (); }
      [[nodiscard]] auto end () noexcept { return vector_set.end (); }
      [[nodiscard]] auto end () const noexcept { return vector_set.end (); }
  };

  template <Vector V, size_t B, bool Simd, bool Cache>
  std::ostream& operator<< (std::ostream& os, const block_sharingtrie<V, B, Simd, Cache>& trie) {
    for (const auto& value : trie)
      os << value << '\n';
    return os;
  }
}
