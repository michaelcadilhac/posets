#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/config.hh>

namespace posets::utils {
  template <Vector V>
  [[nodiscard, gnu::always_inline]] inline bool bound_leq (const V& lhs, const V& rhs) {
    if constexpr (requires { lhs.partial_order (rhs).leq_coordinates (); })
      return lhs.partial_order (rhs).leq_coordinates ();
    else {
      for (size_t i = 0; i < lhs.size (); ++i)
        if (not(lhs[i] <= rhs[i]))
          return false;
      return true;
    }
  }

  // Non-owning frozen geometry. IDs, unlike dense generator slots, remain stable
  // until rebuild. Rebuild requires unique IDs and common positive dimension.
  // Access/live predicates must agree and keep live payloads valid; callbacks may
  // not mutate/rebuild geometry during traversal. Dominates finds stored >= query
  // (strict means unequal); dominated_by reports stored <= query inclusively.
  // The accessor is called only after the live predicate succeeds.
  template <Vector V, bool Cascade = true>
  class bucket_kdtree {
    public:
      using id_type = std::uint32_t;
      static constexpr id_type none = std::numeric_limits<id_type>::max ();
      struct query_stats {
          uint64_t queries {0}, node_visits {0}, leaf_visits {0}, comparisons {0};
          uint64_t binary_searches {0}, bridge_steps {0}, corrections {0}, pending_comparisons {0};
          uint64_t node_prunes {0}, pending_blocks {0}, pending_prunes {0};
          uint64_t tail_comparisons {0}, rebuilds {0};
      };
      // Plain diagnostic record.
      // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
      struct statistics {
          size_t object_bytes, node_size, nodes, node_capacity;
          size_t keys, key_bytes, bridge_bytes, point_id_bytes, leaf_map_bytes, bound_object_bytes;
          [[nodiscard]] size_t bytes () const {
            return object_bytes + (node_size * node_capacity) + key_bytes + bridge_bytes +
                   point_id_bytes + leaf_map_bytes + bound_object_bytes;
          }
      };

      // NOLINTEND(misc-non-private-member-variables-in-classes)

    private:
      using key_type = V::value_type;
      struct node {
          id_type first, second;  // children, or leaf ID span (start, length)
          id_type parent, live, axis;
          id_type catalog, length;
          key_type split;
      };
      struct bridge {
          id_type left, right;
      };
      std::vector<node> nodes;
      // Reverse node order is bottom-up: both children precede their parent.
      // Deletions only shrink a subtree, so these bounds may remain stale.
      std::vector<std::pair<V, V>> bounds;
      std::vector<id_type> ids;
      std::vector<id_type> leaf_for_id;
      std::vector<key_type> keys;
      // One pair per INTERNAL catalog entry; leaf entries have no bridge storage.
      std::vector<bridge> links;
      std::vector<id_type> link_start;
      size_t dimension {0};
      size_t bucket_size {16};
#if POSETS_LAZY_KDTREE_STATS
      mutable query_stats query_counts;
      // Diagnostic totals survive index replacement and destruction. Like the
      // per-index counters, these opt-in counters require single-threaded queries.
      inline static query_stats cumulative_counts;
#endif

      // Recursion is bounded by balanced tree height, independent of dimension.
      // NOLINTBEGIN(misc-no-recursion)
      template <class Get>
      id_type build_node (size_t begin, size_t end, id_type axis, id_type parent, const Get& get) {
        const auto index = static_cast<id_type> (nodes.size ());
        nodes.push_back ({0, 0, parent, static_cast<id_type> (end - begin), none, 0, 0, {}});
        if constexpr (Cascade)
          link_start.push_back (0);
        if (end - begin <= bucket_size) {
          std::ranges::sort (std::span (ids).subspan (begin, end - begin),
                             [&] (id_type a, id_type b) { return get (a)[0] < get (b)[0]; });
          auto& current = nodes[index];
          current.first = static_cast<id_type> (begin);
          current.second = static_cast<id_type> (end - begin);
          current.catalog = static_cast<id_type> (keys.size ());
          current.length = current.second;
          for (size_t i = begin; i < end; ++i) {
            keys.push_back (get (ids[i])[0]);
            leaf_for_id[ids[i]] = index;
          }
          return index;
        }
        const size_t middle = begin + ((end - begin) / 2);
        auto points = std::span (ids).subspan (begin, end - begin);
        std::ranges::nth_element (
            points, points.begin () + static_cast<std::ptrdiff_t> (middle - begin),
            [&] (id_type a, id_type b) { return get (a)[axis] < get (b)[axis]; });
        const key_type split = get (ids[middle])[axis];
        const auto next_axis = static_cast<id_type> ((static_cast<size_t> (axis) + 1) % dimension);
        const id_type left = build_node (begin, middle, next_axis, index, get);
        const id_type right = build_node (middle, end, next_axis, index, get);
        nodes[index].first = left;
        nodes[index].second = right;
        nodes[index].axis = axis;
        nodes[index].split = split;
        if constexpr (Cascade) {
          const auto a = nodes[left];
          const auto b = nodes[right];
          const auto start = static_cast<id_type> (keys.size ());
          link_start[index] = static_cast<id_type> (links.size ());
          size_t i = 1;
          size_t j = 1;
          size_t lower_a = 0;
          size_t lower_b = 0;
          while (i < a.length or j < b.length) {
            key_type key;
            if (j >= b.length or (i < a.length and keys[a.catalog + i] <= keys[b.catalog + j])) {
              key = keys[a.catalog + i];
              i += 2;
            }
            else {
              key = keys[b.catalog + j];
              j += 2;
            }
            while (lower_a < a.length and keys[a.catalog + lower_a] < key)
              ++lower_a;
            while (lower_b < b.length and keys[b.catalog + lower_b] < key)
              ++lower_b;
            keys.push_back (key);
            links.push_back ({static_cast<id_type> (lower_a), static_cast<id_type> (lower_b)});
          }
          nodes[index].catalog = start;
          nodes[index].length = static_cast<id_type> (keys.size () - start);
        }
        return index;
      }

      // NOLINTEND(misc-no-recursion)

      template <class Get>
      void build_bounds (const Get& get) {
        bounds.reserve (nodes.size ());
        for (size_t i = nodes.size (); i-- > 0;) {
          const auto& current = nodes[i];
          if (current.axis == none) {
            const auto& first = get (ids[current.first]);
            auto lower = first.copy ();
            auto upper = first.copy ();
            for (size_t j = 1; j < current.second; ++j) {
              const auto& point = get (ids[current.first + j]);
              lower.meet_with (point);
              upper.join_with (point);
            }
            bounds.emplace_back (std::move (lower), std::move (upper));
          }
          else {
            const auto& left = bounds[nodes.size () - 1 - current.first];
            const auto& right = bounds[nodes.size () - 1 - current.second];
            bounds.emplace_back (left.first.meet (right.first), left.second.join (right.second));
          }
        }
      }

      [[nodiscard]] size_t bound (id_type index, key_type key, bool upper) const {
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::binary_searches);
#endif
        const auto& current = nodes[index];
        const auto catalog = std::span (keys).subspan (current.catalog, current.length);
        if (upper)
          return std::upper_bound (catalog.begin (), catalog.end (), key) - catalog.begin ();
        return std::lower_bound (catalog.begin (), catalog.end (), key) - catalog.begin ();
      }

      // Both modes bridge to LOWER_BOUND(child, next_parent_key). Alternating
      // sampling leaves at most one unsampled predecessor, including at either
      // end. Upper-bound mode changes only root search and the correction test.
      [[nodiscard]] size_t cascade (id_type parent, size_t position, bool right, key_type key,
                                    bool upper) const {
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::bridge_steps);
#endif
        const auto& p = nodes[parent];
        const id_type child = right ? p.second : p.first;
        const auto& c = nodes[child];
        size_t result = c.length;
        if (position < p.length) {
          const auto& link = links[link_start[parent] + position];
          result = right ? link.right : link.left;
        }
        if (result > 0) {
          const auto previous = keys[c.catalog + result - 1];
          if (upper ? previous > key : previous >= key) {
            --result;
#if POSETS_LAZY_KDTREE_STATS
            record (&query_stats::corrections);
#endif
          }
        }
        return result;
      }

      // Reporting emits IDs; its boolean sentinel is intentionally ignored.
      // NOLINTBEGIN(misc-no-recursion,modernize-use-nodiscard)
      template <bool Report, class Get, class Live, class Emit>
      bool visit (id_type index, size_t position, const V& value, bool strict, const Get& get,
                  const Live& live, const Emit& emit) const {
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::node_visits);
#endif
        const auto& current = nodes[index];
        if (current.live == 0) {
#if POSETS_LAZY_KDTREE_STATS
          record (&query_stats::node_prunes);
#endif
          return false;
        }
        const auto& [lower, upper] = bounds[nodes.size () - 1 - index];
        // A passing bound is never a witness, including after lazy retirement.
        const bool possible = Report ? bound_leq (lower, value) : bound_leq (value, upper);
        if (not possible) {
#if POSETS_LAZY_KDTREE_STATS
          record (&query_stats::node_prunes);
#endif
          return false;
        }
        if (current.axis == none) {
#if POSETS_LAZY_KDTREE_STATS
          record (&query_stats::leaf_visits);
#endif
          if constexpr (not Cascade)
            position = bound (index, value[0], Report);
          const size_t begin = Report ? 0 : position;
          const size_t end = Report ? position : current.length;
          for (size_t i = begin; i < end; ++i) {
            const id_type id = ids[current.first + i];
            if (not live (id))
              continue;
#if POSETS_LAZY_KDTREE_STATS
            record (&query_stats::comparisons);
#endif
            auto order = value.partial_order (get (id));
            const bool matches = Report ? order.geq () : order.leq ();
            if (matches and (not strict or not(Report ? order.leq () : order.geq ()))) {
              if constexpr (Report)
                emit (id);
              else
                return true;
            }
          }
          return false;
        }
        // Internal catalogs are SAMPLES: even position==end cannot prune.
        const auto child_position = [&] (bool right) {
          if constexpr (Cascade)
            return cascade (index, position, right, value[0], Report);
          return size_t {0};
        };
        if constexpr (Report) {
          visit<Report> (current.first, child_position (false), value, strict, get, live, emit);
          if (value[current.axis] >= current.split)
            visit<Report> (current.second, child_position (true), value, strict, get, live, emit);
          return false;
        }
        else {
          if (visit<Report> (current.second, child_position (true), value, strict, get, live,
                             emit))
            return true;
          return value[current.axis] <= current.split and
                 visit<Report> (current.first, child_position (false), value, strict, get, live,
                                emit);
        }
      }

      // NOLINTEND(misc-no-recursion,modernize-use-nodiscard)

    public:
#if POSETS_LAZY_KDTREE_STATS
      void record (uint64_t query_stats::*field, uint64_t amount = 1) const {
        query_counts.*field += amount;
        cumulative_counts.*field += amount;
      }
      [[nodiscard]] static query_stats cumulative_statistics () { return cumulative_counts; }
#endif
      bucket_kdtree () = default;
      bucket_kdtree (const bucket_kdtree& other)
        : nodes (other.nodes),
          ids (other.ids),
          leaf_for_id (other.leaf_for_id),
          keys (other.keys),
          links (other.links),
          link_start (other.link_start),
          dimension (other.dimension),
          bucket_size (other.bucket_size) {
        bounds.reserve (other.bounds.size ());
        for (const auto& [lower, upper] : other.bounds)
          bounds.emplace_back (lower.copy (), upper.copy ());
#if POSETS_LAZY_KDTREE_STATS
        query_counts = other.query_counts;
#endif
      }
      bucket_kdtree& operator= (const bucket_kdtree& other) {
        if (this != &other) {
          bucket_kdtree replacement (other);
          swap (replacement);
        }
        return *this;
      }
      bucket_kdtree (bucket_kdtree&& other) noexcept { swap (other); }
      bucket_kdtree& operator= (bucket_kdtree&& other) noexcept {
        if (this != &other) {
          bucket_kdtree replacement (std::move (other));
          swap (replacement);
        }
        return *this;
      }
      void swap (bucket_kdtree& other) noexcept {
        using std::swap;
        swap (nodes, other.nodes);
        swap (bounds, other.bounds);
        swap (ids, other.ids);
        swap (leaf_for_id, other.leaf_for_id);
        swap (keys, other.keys);
        swap (links, other.links);
        swap (link_start, other.link_start);
        swap (dimension, other.dimension);
        swap (bucket_size, other.bucket_size);
#if POSETS_LAZY_KDTREE_STATS
        swap (query_counts, other.query_counts);
#endif
      }
      template <class Get>
      void rebuild (std::vector<id_type> points, size_t dim, size_t leaf_size, const Get& get) {
        if (dim == 0 or dim >= none or leaf_size == 0)
          throw std::invalid_argument (
              "bucket_kdtree requires positive dimension and bucket size");
        if (points.size () > static_cast<size_t> (none) / 2)
          throw std::length_error ("bucket_kdtree node index capacity exceeded");
        bucket_kdtree replacement;
        replacement.dimension = dim;
        replacement.bucket_size = leaf_size;
        replacement.ids = std::move (points);
        if (not replacement.ids.empty ()) {
          const auto maximum = std::ranges::max (replacement.ids);
          if (maximum == none)
            throw std::length_error ("bucket_kdtree ID capacity exceeded");
          replacement.leaf_for_id.assign (static_cast<size_t> (maximum) + 1, none);
          for (auto id : replacement.ids) {
            if (replacement.leaf_for_id[id] != none)
              throw std::invalid_argument ("bucket_kdtree requires unique IDs");
            replacement.leaf_for_id[id] = 0;
            if (get (id).size () != dim)
              throw std::invalid_argument ("bucket_kdtree dimension mismatch");
          }
          replacement.build_node (0, replacement.ids.size (), 0, none, get);
          replacement.build_bounds (get);
        }
        swap (replacement);
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::rebuilds);
#endif
      }
      void retire (id_type id) noexcept {
        if (id >= leaf_for_id.size ())
          return;  // An ID in the pending buffer has no frozen leaf yet.
        id_type index = leaf_for_id[id];
        if (index == none)
          return;
        leaf_for_id[id] = none;
        while (index != none) {
          assert (nodes[index].live > 0);
          --nodes[index].live;
          index = nodes[index].parent;
        }
      }
      template <class Get, class Live>
      [[nodiscard]] bool dominates (const V& value, const Get& get, const Live& live,
                                    bool strict = false) const {
        if (nodes.empty () or nodes[0].live == 0)
          return false;
        if (value.size () != dimension)
          throw std::invalid_argument ("bucket_kdtree query dimension mismatch");
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::queries);
#endif
        const size_t position = Cascade ? bound (0, value[0], false) : 0;
        return visit<false> (0, position, value, strict, get, live, [] (id_type) {});
      }
      template <class Get, class Live, class Emit>
      void dominated_by (const V& value, const Get& get, const Live& live,
                         const Emit& emit) const {
        if (nodes.empty () or nodes[0].live == 0)
          return;
        if (value.size () != dimension)
          throw std::invalid_argument ("bucket_kdtree query dimension mismatch");
#if POSETS_LAZY_KDTREE_STATS
        record (&query_stats::queries);
#endif
        const size_t position = Cascade ? bound (0, value[0], true) : 0;
        visit<true> (0, position, value, false, get, live, emit);
      }
      [[nodiscard]] query_stats query_statistics () const {
#if POSETS_LAZY_KDTREE_STATS
        return query_counts;
#else
        return {};
#endif
      }
      void reset_query_statistics () const {
#if POSETS_LAZY_KDTREE_STATS
        query_counts = {};
#endif
      }
      [[nodiscard]] statistics storage () const {
        return {sizeof (*this),
                sizeof (node),
                nodes.size (),
                nodes.capacity (),
                keys.size (),
                keys.capacity () * sizeof (key_type),
                links.capacity () * sizeof (bridge),
                ids.capacity () * sizeof (id_type),
                (leaf_for_id.capacity () + link_start.capacity ()) * sizeof (id_type),
                bounds.capacity () * sizeof (bounds[0])};
      }
      [[nodiscard]] size_t frozen_size () const { return ids.size (); }
      [[nodiscard]] size_t live_size () const { return nodes.empty () ? 0 : nodes[0].live; }

      // Test the FC invariant independently of geometric pruning and liveness.
      [[nodiscard]] bool check_bridges (key_type key, bool upper) const {
        if constexpr (Cascade) {
          for (id_type i = 0; i < nodes.size (); ++i)
            if (nodes[i].axis != none) {
              const auto position = bound (i, key, upper);
              if (cascade (i, position, false, key, upper) != bound (nodes[i].first, key, upper) or
                  cascade (i, position, true, key, upper) != bound (nodes[i].second, key, upper))
                return false;
            }
        }
        return true;
      }
  };
}
