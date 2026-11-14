#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/antichain.hh>
#include <posets/utils/reduce.hh>

namespace posets::utils {
  // A static bounding-box hierarchy over an antichain.  join_c is an upper
  // corner and meet_c a lower corner of every subtree.  The tree is rebuilt
  // in bulk because Acacia's apply operation changes every stored vector.
  template <Vector V, size_t B = 16>
  class bboxtree {
      static_assert (B >= 2);

      class node {
          friend class bboxtree;

          V join_c;
          V meet_c;
          uint32_t begin;
          uint32_t end;
          uint32_t first_child;
          uint32_t nchildren;

        public:
          node (V&& join_corner, V&& meet_corner, uint32_t first, uint32_t last, uint32_t child,
                uint32_t children)
            : join_c {std::move (join_corner)},
              meet_c {std::move (meet_corner)},
              begin {first},
              end {last},
              first_child {child},
              nchildren {children} {}
      };

      std::vector<V> vector_set;
      std::vector<vector_sum_t<V>> keys;
      mutable std::vector<node> nodes;
      mutable std::vector<uint32_t> stack_cache;
      mutable bool corners_dirty {false};

#ifdef POSETS_BBOX_STATS
    public:
      struct stats {
          uint64_t node_visits {0};
          uint64_t corner_prunes {0};
          uint64_t accept_alls {0};
          uint64_t cutoff_prunes {0};
          uint64_t leaf_comparisons {0};
      };

    private:
      mutable stats query_stats;
#endif

      void append_node (uint32_t begin, uint32_t end, uint32_t first_child,
                        uint32_t nchildren) const {
        V join_corner =
            nchildren == 0 ? vector_set[begin].copy () : nodes[first_child].join_c.copy ();
        V meet_corner =
            nchildren == 0 ? vector_set[begin].copy () : nodes[first_child].meet_c.copy ();
        const uint32_t count = nchildren == 0 ? end - begin : nchildren;
        for (uint32_t i = 1; i < count; ++i) {
          const V& next_join =
              nchildren == 0 ? vector_set[begin + i] : nodes[first_child + i].join_c;
          const V& next_meet =
              nchildren == 0 ? vector_set[begin + i] : nodes[first_child + i].meet_c;
          join_corner.join_with (next_join);
          meet_corner.meet_with (next_meet);
        }
        nodes.emplace_back (std::move (join_corner), std::move (meet_corner), begin, end,
                            first_child, nchildren);
      }

      void build_corners () const {
        if (not corners_dirty)
          return;
        assert (vector_set.size () > B);
        nodes.clear ();
        nodes.reserve (((vector_set.size () * B) / (B - 1)) + 1);

        uint32_t level_begin = 0;
        for (uint32_t begin = 0; begin < vector_set.size (); begin += B) {
          const uint32_t end = std::min<uint32_t> (begin + B, vector_set.size ());
          append_node (begin, end, 0, 0);
        }
        uint32_t level_end = nodes.size ();

        while (level_end - level_begin > 1) {
          const uint32_t next_begin = nodes.size ();
          for (uint32_t first = level_begin; first < level_end; first += B) {
            const uint32_t nchildren = std::min<uint32_t> (B, level_end - first);
            append_node (nodes[first].begin, nodes[first + nchildren - 1].end, first, nchildren);
          }
          level_begin = next_begin;
          level_end = nodes.size ();
        }
        stack_cache.reserve (nodes.size ());
        corners_dirty = false;
      }

    public:
      bboxtree () = default;
      bboxtree (const bboxtree&) = delete;
      bboxtree (bboxtree&&) = default;
      bboxtree& operator= (const bboxtree&) = delete;
      bboxtree& operator= (bboxtree&&) = default;

      template <std::ranges::input_range R>
      explicit bboxtree (R&& elements) {
        std::vector<V> values;
        values.reserve (elements.size ());
        for (auto&& value : elements)
          values.push_back (std::move (value));
        assign_and_build (std::move (values));
      }

      void assign_and_build (std::vector<V>&& sorted_antichain) {
        std::vector<vector_sum_t<V>> sorted_keys;
        sorted_keys.reserve (sorted_antichain.size ());
        for (const auto& value : sorted_antichain)
          sorted_keys.push_back (sum_key (value));
        assign_and_build (std::move (sorted_antichain), std::move (sorted_keys));
      }

      void assign_and_build (std::vector<V>&& sorted_antichain,
                             std::vector<vector_sum_t<V>>&& sorted_keys) {
        assert (sorted_antichain.size () <= UINT32_MAX);
        assert (sorted_antichain.size () == sorted_keys.size ());
        assert (std::ranges::is_sorted (sorted_keys, std::greater<> {}));
        for (const auto& value : sorted_antichain)
          check_sum_dimension<typename V::value_type> (value.size ());
        vector_set = std::move (sorted_antichain);
        keys = std::move (sorted_keys);
        nodes.clear ();
        corners_dirty = vector_set.size () > B;
      }

      [[nodiscard]] bool dominates (const V& v, bool strict = false) const {
        if constexpr (not requires { v.cached_sum (); })
          return dominates (
              v, strict,
              std::numeric_limits<
                  vector_sum_t<V>>::min ());  // NOLINT(boost-use-ranges,modernize-use-ranges)
        return dominates (v, strict, sum_key (v));
      }

      [[nodiscard]] bool dominates (const V& v, bool strict, vector_sum_t<V> query_key) const {
        check_sum_dimension<typename V::value_type> (v.size ());
        const auto cutoff_it = std::ranges::upper_bound (keys, query_key, std::greater<> {});
        const auto cutoff = static_cast<uint32_t> (std::distance (keys.begin (), cutoff_it));
#ifdef POSETS_BBOX_STATS
        if (cutoff < vector_set.size ())
          ++query_stats.cutoff_prunes;
#endif
        if (cutoff == 0)
          return false;

        if (vector_set.size () <= B) {
          for (uint32_t i = 0; i < cutoff; ++i) {
#ifdef POSETS_BBOX_STATS
            ++query_stats.leaf_comparisons;
#endif
            auto po = v.partial_order (vector_set[i]);
            if (po.leq () and (not strict or not po.geq ()))
              return true;
          }
          return false;
        }

        // Most successful queries are answered by the highest-sum leaf.  Scan
        // it directly so that those queries do not pay hierarchy/corner costs.
        const uint32_t direct_end = std::min<uint32_t> (B, cutoff);
        for (uint32_t i = 0; i < direct_end; ++i) {
#ifdef POSETS_BBOX_STATS
          ++query_stats.leaf_comparisons;
#endif
          auto po = v.partial_order (vector_set[i]);
          if (po.leq () and (not strict or not po.geq ()))
            return true;
        }
        if (cutoff <= B)
          return false;

        build_corners ();
        stack_cache.clear ();
        stack_cache.push_back (nodes.size () - 1);

        while (not stack_cache.empty ()) {
          const node& current = nodes[stack_cache.back ()];
          stack_cache.pop_back ();
#ifdef POSETS_BBOX_STATS
          ++query_stats.node_visits;
#endif
          if (current.begin >= cutoff) {
#ifdef POSETS_BBOX_STATS
            ++query_stats.cutoff_prunes;
#endif
            continue;
          }
          if (current.end <= direct_end)
            continue;

          auto upper = v.partial_order (current.join_c);
          if (not upper.leq ()) {
#ifdef POSETS_BBOX_STATS
            ++query_stats.corner_prunes;
#endif
            continue;
          }

          auto lower = v.partial_order (current.meet_c);
          if (lower.leq ()) {
            const uint32_t allowed =
                std::min (current.end, cutoff) - std::max (current.begin, direct_end);
            if (not strict or not lower.geq () or allowed >= 2) {
#ifdef POSETS_BBOX_STATS
              ++query_stats.accept_alls;
#endif
              return true;
            }
          }

          if (current.nchildren == 0) {
            const uint32_t leaf_end = std::min (current.end, cutoff);
            for (uint32_t i = std::max (current.begin, direct_end); i < leaf_end; ++i) {
#ifdef POSETS_BBOX_STATS
              ++query_stats.leaf_comparisons;
#endif
              auto po = v.partial_order (vector_set[i]);
              if (po.leq () and (not strict or not po.geq ()))
                return true;
            }
          }
          else {
            for (uint32_t i = current.nchildren; i > 0; --i)
              stack_cache.push_back (current.first_child + i - 1);
          }
        }
        return false;
      }

      [[nodiscard]] size_t size () const { return vector_set.size (); }
      [[nodiscard]] bool empty () const { return vector_set.empty (); }
      [[nodiscard]] auto begin () { return vector_set.begin (); }
      [[nodiscard]] auto begin () const { return vector_set.begin (); }
      [[nodiscard]] auto end () { return vector_set.end (); }
      [[nodiscard]] auto end () const { return vector_set.end (); }
      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }
      [[nodiscard]] auto& get_keys () { return keys; }
      [[nodiscard]] const auto& get_keys () const { return keys; }
      [[nodiscard]] vector_sum_t<V> max_key () const { return keys.front (); }
      [[nodiscard]] bool corners_built () const { return not corners_dirty; }
      [[nodiscard]] size_t node_count () const { return nodes.size (); }

#ifdef POSETS_BBOX_STATS
      [[nodiscard]] const stats& get_stats () const { return query_stats; }
      void reset_stats () const { query_stats = {}; }
      void print_stats (std::ostream& os) const {
        os << "bbox_node_visits=" << query_stats.node_visits
           << " bbox_corner_prunes=" << query_stats.corner_prunes
           << " bbox_accept_alls=" << query_stats.accept_alls
           << " bbox_cutoff_prunes=" << query_stats.cutoff_prunes
           << " bbox_leaf_comparisons=" << query_stats.leaf_comparisons;
      }
#endif

      [[nodiscard]] bool is_antichain () const { return utils::is_antichain (vector_set); }
  };
}
