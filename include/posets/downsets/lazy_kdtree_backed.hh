#pragma once

#include <type_traits>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/bucket_kdtree.hh>

namespace posets::downsets {
  struct lazy_kdtree_policy {
      size_t leaf_size {16};
      size_t min_index_size {32};
      size_t min_buffer {32};
      double buffer_fraction {0.25};
      double dead_fraction {0.25};
      size_t max_index_bytes {0};     // Maintenance trigger, not a hard allocation limit.
      size_t bulk_scan_limit {4096};  // Larger inputs use indexed insertion; 0 forces it.
  };

  // Experimental dense antichain plus a frozen, lazily deleted bucket index.
  // Filter coordinate is fixed at 0. IDs are append-only within an epoch;
  // dense slots may be reused, but IDs are renumbered only with a full rebuild.
  // Mutable generator access has the same index-invalidation restriction as
  // existing indexed backends; use clear()/reassignment instead of editing it.
  template <Vector V, bool Cascade = true>
  class lazy_kdtree_backed {
      using index_type = utils::bucket_kdtree<V, Cascade>;
      using id_type = index_type::id_type;
      static constexpr id_type none = index_type::none;
      static constexpr bool safe_moves =
          std::is_nothrow_move_constructible_v<V> and std::is_nothrow_move_assignable_v<V>;
      struct empty_tag {};
      lazy_kdtree_policy policy;
      std::vector<V> maxima;
      std::vector<id_type> id_at_slot, slot_of_id, pending;
      index_type index;
      size_t rebuild_count {0}, compact_count {0};
#if POSETS_LAZY_KDTREE_STATS
      mutable uint64_t pending_comparisons {0};
      mutable uint64_t bulk_comparisons {0};
#endif

      lazy_kdtree_backed (lazy_kdtree_policy settings, empty_tag) : policy (settings) {}
      template <bool Buffer, class Float>
      [[nodiscard]] static bool valid_fraction (Float value) {
        // Integer ordering validates the complete binary64 interval even when
        // fast-math assumes finite inputs or treats subnormals as signed zero.
        if constexpr (std::numeric_limits<Float>::is_iec559 and
                      std::numeric_limits<Float>::digits == 53 and
                      std::numeric_limits<Float>::radix == 2 and
                      std::numeric_limits<Float>::max_exponent == 1024 and
                      sizeof (Float) == sizeof (uint64_t)) {
          constexpr uint64_t one = UINT64_C (0x3ff0000000000000);
          constexpr uint64_t negative_zero = UINT64_C (0x8000000000000000);
          const auto bits = std::bit_cast<uint64_t> (value);
          if constexpr (Buffer)
            return bits > 0 and bits <= one;
          else
            return bits < one or bits == negative_zero;
        }
        else {
          // Other representations retain the compiler's floating-point rules.
          return std::isfinite (value) and
                 (Buffer ? value > 0 and value <= 1 : value >= 0 and value < 1);
        }
      }
      static void validate (const lazy_kdtree_policy& settings) {
        if (settings.leaf_size == 0 or settings.min_index_size == 0 or settings.min_buffer == 0 or
            not valid_fraction<true> (settings.buffer_fraction) or
            not valid_fraction<false> (settings.dead_fraction))
          throw std::invalid_argument ("invalid lazy_kdtree rebuild policy");
      }
      template <class T>
      static void reserve_growing (std::vector<T>& values, size_t needed) {
        if (needed > values.max_size ())
          throw std::length_error ("lazy_kdtree vector capacity exceeded");
        if (needed > values.capacity ())
          values.reserve (std::min (values.max_size (), needed + (needed / 2)));
      }
      template <class Get>
      void rebuild_from (size_t count, size_t dimension, const Get& get) {
        if (count > static_cast<size_t> (none) / 2)
          throw std::length_error ("lazy_kdtree ID capacity exceeded");
        std::vector<id_type> dense (count);
        std::iota (dense.begin (), dense.end (), 0);
        auto positions = dense;
        std::vector<id_type> buffer;
        index_type replacement;
        if (count >= policy.min_index_size)
          replacement.rebuild (dense, dimension, policy.leaf_size, get);
        else
          buffer = dense;
        index.swap (replacement);
        id_at_slot.swap (dense);
        slot_of_id.swap (positions);
        pending.swap (buffer);
        ++compact_count;
        rebuild_count += index.frozen_size () != 0;
      }
      [[nodiscard]] bool live (id_type id) const {
        return id < slot_of_id.size () and slot_of_id[id] != none;
      }
      [[nodiscard]] const V& point (id_type id) const {
        assert (live (id));
        return maxima[slot_of_id[id]];
      }
      [[nodiscard]] bool should_rebuild (size_t size, size_t created, size_t buffer) const {
        const size_t dead = created - size;
        const bool dirty =
            created >= policy.min_buffer and dead >= policy.min_buffer and
            static_cast<double> (dead) >= static_cast<double> (created) * policy.dead_fraction;
        const bool buffered =
            buffer >= policy.min_buffer and
            static_cast<double> (buffer) >= static_cast<double> (size) * policy.buffer_fraction;
        const bool bytes =
            policy.max_index_bytes != 0 and dead > 0 and
            index.storage ().bytes () +
                    ((id_at_slot.capacity () + slot_of_id.capacity () + pending.capacity ()) *
                     sizeof (id_type)) >
                policy.max_index_bytes;
        return dirty or buffered or bytes;
      }
      void erase_id (id_type id) noexcept {
        const size_t slot = slot_of_id[id];
        const auto last_id = id_at_slot.back ();
        index.retire (id);
        if (slot != maxima.size () - 1) {
          maxima[slot] = std::move (maxima.back ());
          id_at_slot[slot] = last_id;
          slot_of_id[last_id] = static_cast<id_type> (slot);
        }
        maxima.pop_back ();
        id_at_slot.pop_back ();
        slot_of_id[id] = none;
      }

    public:
      using value_type = V;
      static constexpr bool uses_cascade = Cascade;
      struct statistics {
          index_type::statistics frozen;
          size_t live, created_ids, pending_slots, pending_live, retired_ids;
          size_t generator_object_bytes, map_bytes, known_retained_bytes, rebuilds, compactions;
          size_t bulk_scan_limit;
          uint64_t bulk_comparisons;
          friend std::ostream& operator<< (std::ostream& os, const statistics& s) {
            return os << "live=" << s.live << " created_ids=" << s.created_ids
                      << " pending_slots=" << s.pending_slots << " pending_live=" << s.pending_live
                      << " retired_ids=" << s.retired_ids << " node_size=" << s.frozen.node_size
                      << " reachable=" << s.frozen.nodes << " capacity=" << s.frozen.node_capacity
                      << " catalog_keys=" << s.frozen.keys << " key_bytes=" << s.frozen.key_bytes
                      << " bridge_bytes=" << s.frozen.bridge_bytes
                      << " point_id_bytes=" << s.frozen.point_id_bytes
                      << " leaf_map_bytes=" << s.frozen.leaf_map_bytes
                      << " generator_objects=" << s.generator_object_bytes
                      << " map_bytes=" << s.map_bytes
                      << " known_retained=" << s.known_retained_bytes << " rebuilds=" << s.rebuilds
                      << " compactions=" << s.compactions
                      << " bulk_scan_limit=" << s.bulk_scan_limit
                      << " query_stats_enabled=" << POSETS_LAZY_KDTREE_STATS
                      << " bulk_comparisons=" << s.bulk_comparisons;
          }
      };

      lazy_kdtree_backed () = delete;
      explicit lazy_kdtree_backed (std::vector<V>&& values, lazy_kdtree_policy settings = {})
        : lazy_kdtree_backed (settings, empty_tag {}) {
        validate (policy);
        if (values.empty ())
          return;
        const size_t dimension = values.front ().size ();
        if (dimension == 0)
          throw std::invalid_argument ("lazy_kdtree requires nonzero dimension");
        for (const auto& value : values)
          if (value.size () != dimension)
            throw std::invalid_argument ("lazy_kdtree dimension mismatch");
        if (values.size () > policy.bulk_scan_limit) {
          for (auto& value : values)
            insert (std::move (value));
          return;
        }

        // Descending lexicographic order puts every possible strict dominator
        // before its candidate, without sums or assumptions about V::operator<.
        // Ties retain the first original duplicate deterministically.
        std::vector<size_t> order (values.size ());
        // Keep this C++20 utility independent of an additional Boost header.
        std::iota (order.begin (), order.end (), 0);  // NOLINT(boost-use-ranges)
        std::ranges::sort (order, [&] (size_t lhs, size_t rhs) {
          const V& a = values[lhs];
          const V& b = values[rhs];
          for (size_t d = 0; d < dimension; ++d) {
            const auto left = a[d];
            const auto right = b[d];
            if (left != right)
              return left > right;
          }
          return lhs < rhs;
        });
        size_t kept = 0;
        for (const auto candidate : order) {
          const V& value = values[candidate];
          bool covered = false;
          for (size_t i = 0; i < kept; ++i) {
#if POSETS_LAZY_KDTREE_STATS
            ++bulk_comparisons;
#endif
            if (value.partial_order (values[order[i]]).leq ()) {
              covered = true;
              break;
            }
          }
          if (not covered)
            order[kept++] = candidate;  // Only overwrite already visited slots.
        }
        order.resize (kept);
        rebuild_from (kept, dimension, [&] (id_type id) -> const V& { return values[order[id]]; });
        maxima.reserve (kept);
        // The nothrow path has no allocations/comparisons after consumption starts.
        // Throwing moves use the same input-preserving copy path as insert().
        for (const auto original : order)
          if constexpr (safe_moves)
            maxima.push_back (std::move (values[original]));
          else
            maxima.push_back (std::as_const (values)[original].copy ());
      }
      explicit lazy_kdtree_backed (V&& value, lazy_kdtree_policy settings = {})
        : lazy_kdtree_backed (settings, empty_tag {}) {
        validate (policy);
        insert (std::move (value));
      }
      lazy_kdtree_backed (const lazy_kdtree_backed&) = delete;
      lazy_kdtree_backed (lazy_kdtree_backed&& other) noexcept : policy (other.policy) {
        swap (other);
      }
      lazy_kdtree_backed& operator= (lazy_kdtree_backed&& other) noexcept {
        if (this != &other) {
          lazy_kdtree_backed replacement (std::move (other));
          swap (replacement);
        }
        return *this;
      }
      void swap (lazy_kdtree_backed& other) noexcept {
        using std::swap;
        swap (policy, other.policy);
        swap (maxima, other.maxima);
        swap (id_at_slot, other.id_at_slot);
        swap (slot_of_id, other.slot_of_id);
        swap (pending, other.pending);
        index.swap (other.index);
        swap (rebuild_count, other.rebuild_count);
        swap (compact_count, other.compact_count);
#if POSETS_LAZY_KDTREE_STATS
        swap (pending_comparisons, other.pending_comparisons);
        swap (bulk_comparisons, other.bulk_comparisons);
#endif
      }
      void clear () noexcept {
        lazy_kdtree_backed replacement (policy, empty_tag {});
        swap (replacement);
      }
      [[nodiscard]] bool contains (const V& value) const { return dominates (value); }
      [[nodiscard]] bool dominates (const V& value, bool strict = false) const {
        if (maxima.empty ())
          return false;
        if (value.size () != maxima.front ().size ())
          throw std::invalid_argument ("lazy_kdtree query dimension mismatch");
        for (const auto id : pending)
          if (live (id)) {
#if POSETS_LAZY_KDTREE_STATS
            ++pending_comparisons;
#endif
            auto order = value.partial_order (point (id));
            if (order.leq () and (not strict or not order.geq ()))
              return true;
          }
        return index.dominates (
            value, [this] (id_type id) -> const V& { return point (id); },
            [this] (id_type id) { return live (id); }, strict);
      }

      // A failed insertion preserves logical generators and membership. Successful
      // earlier reserves may change capacities and invalidate iterator addresses.
      // Each insertion stages allocations and throwing copies before publishing.
      // Frozen geometry/keys never change until a replacement index is ready.
      bool insert (V&& value) {
        if (value.size () == 0)
          throw std::invalid_argument ("lazy_kdtree requires nonzero dimension");
        if (maxima.empty () and not slot_of_id.empty ())
          clear ();  // Also support destructive extraction followed by reuse.
        if (not maxima.empty () and value.size () != maxima.front ().size ())
          throw std::invalid_argument ("lazy_kdtree dimension mismatch");
        if (slot_of_id.size () >= static_cast<size_t> (none) / 2)
          rebuild ();
        if (slot_of_id.size () >= static_cast<size_t> (none) / 2)
          throw std::length_error ("lazy_kdtree ID capacity exceeded");
        if (contains (value))
          return false;
        std::vector<id_type> retired;
        index.dominated_by (
            value, [this] (id_type id) -> const V& { return point (id); },
            [this] (id_type id) { return live (id); },
            [&] (id_type id) { retired.push_back (id); });
        for (const auto id : pending)
          if (live (id)) {
#if POSETS_LAZY_KDTREE_STATS
            ++pending_comparisons;
#endif
            if (value.partial_order (point (id)).geq ())
              retired.push_back (id);
          }
        const auto new_id = static_cast<id_type> (slot_of_id.size ());
        const size_t final_size = maxima.size () + 1 - retired.size ();
        size_t retired_pending = 0;
        for (const auto id : retired)
          retired_pending += id >= index.frozen_size ();
        const size_t buffer = maxima.size () - index.live_size () + 1 - retired_pending;
        const bool reset = should_rebuild (final_size, slot_of_id.size () + 1, buffer);
        std::vector<id_type> order;
        std::vector<id_type> positions;
        if (reset or not safe_moves) {
          order = id_at_slot;
          positions = slot_of_id;
          order.push_back (new_id);
          positions.push_back (static_cast<id_type> (maxima.size ()));
          for (const auto id : retired) {
            const auto slot = positions[id];
            const auto last_id = order.back ();
            order[slot] = last_id;
            positions[last_id] = slot;
            order.pop_back ();
            positions[id] = none;
          }
        }
        index_type replacement_index;
        std::vector<id_type> dense_ids;
        std::vector<id_type> dense_slots;
        std::vector<id_type> replacement_pending;
        if (reset) {
          dense_ids.resize (final_size);
          std::iota (dense_ids.begin (), dense_ids.end (), 0);
          dense_slots = dense_ids;
          if (final_size >= policy.min_index_size) {
            replacement_index.rebuild (dense_ids, value.size (), policy.leaf_size,
                                       [&] (id_type id) -> const V& {
                                         return order[id] == new_id ? value : point (order[id]);
                                       });
          }
          else {
            replacement_pending = dense_ids;
          }
        }
        std::vector<V> replacement_values;
        if (reset or not safe_moves)
          replacement_values.reserve (final_size);
        if constexpr (not safe_moves)
          for (const auto id : order)
            replacement_values.push_back ((id == new_id ? value : point (id)).copy ());
        if (not reset) {
          reserve_growing (pending, pending.size () + 1);
          if constexpr (safe_moves) {
            reserve_growing (maxima, maxima.size () + 1);
            reserve_growing (id_at_slot, id_at_slot.size () + 1);
            reserve_growing (slot_of_id, slot_of_id.size () + 1);
          }
        }
        // No allocating or potentially throwing Vector operation follows here.
        if (reset) {
          if constexpr (safe_moves)
            for (const auto id : order)
              replacement_values.push_back (
                  std::move (id == new_id ? value : maxima[slot_of_id[id]]));
          maxima.swap (replacement_values);
          id_at_slot.swap (dense_ids);
          slot_of_id.swap (dense_slots);
          pending.swap (replacement_pending);
          index.swap (replacement_index);
          ++compact_count;
          rebuild_count += index.frozen_size () != 0;
        }
        else {
          if constexpr (safe_moves) {
            maxima.push_back (std::move (value));
            id_at_slot.push_back (new_id);
            slot_of_id.push_back (static_cast<id_type> (maxima.size () - 1));
            for (const auto id : retired)
              erase_id (id);
          }
          else {
            for (const auto id : retired)
              index.retire (id);
            maxima.swap (replacement_values);
            id_at_slot.swap (order);
            slot_of_id.swap (positions);
          }
          pending.push_back (new_id);
        }
        return true;
      }

      void rebuild () {
        if (maxima.empty ()) {
          clear ();
          return;
        }
        rebuild_from (maxima.size (), maxima.front ().size (),
                      [this] (id_type id) -> const V& { return maxima[id]; });
      }
      // Whole union is incremental/basic: earlier accepted insertions remain if
      // a later one throws. RHS stays unchanged until success, then becomes empty.
      void union_with (lazy_kdtree_backed&& other) {
        if (this == &other)
          return;
        for (const auto& value : other)
          insert (value.copy ());
        other.clear ();
      }
      template <class F>
      [[nodiscard]] auto apply (const F& transform) const {
        lazy_kdtree_backed result (policy, empty_tag {});
        for (const auto& value : maxima)
          result.insert (transform (value));
        return result;
      }
      void intersect_with (const lazy_kdtree_backed& other) {
        if (this == &other or maxima.empty ())
          return;
        lazy_kdtree_backed result (policy, empty_tag {});
        for (const auto& value : maxima)
          if (other.contains (value))
            result.insert (value.copy ());
          else
            for (const auto& rhs : other)
              result.insert (value.meet (rhs));
        swap (result);
      }
      [[nodiscard]] auto query_statistics () const {
        auto result = index.query_statistics ();
#if POSETS_LAZY_KDTREE_STATS
        result.pending_comparisons = pending_comparisons;
#endif
        return result;
      }
      void reset_query_statistics () const {
        index.reset_query_statistics ();
#if POSETS_LAZY_KDTREE_STATS
        pending_comparisons = 0;
        bulk_comparisons = 0;
#endif
      }
      [[nodiscard]] statistics storage () const {
        const auto frozen = index.storage ();
        const size_t objects = maxima.capacity () * sizeof (V);
        const size_t maps =
            (id_at_slot.capacity () + slot_of_id.capacity () + pending.capacity ()) *
            sizeof (id_type);
#if POSETS_LAZY_KDTREE_STATS
        const auto bulk_count = bulk_comparisons;
#else
        constexpr uint64_t bulk_count = 0;
#endif
        return {frozen,
                maxima.size (),
                slot_of_id.size (),
                pending.size (),
                maxima.empty () ? 0 : maxima.size () - index.live_size (),
                slot_of_id.size () - maxima.size (),
                objects,
                maps,
                sizeof (*this) + frozen.bytes () - sizeof (index_type) + objects + maps,
                rebuild_count,
                compact_count,
                policy.bulk_scan_limit,
                bulk_count};
      }
      [[nodiscard]] const auto& rebuild_policy () const { return policy; }
      [[nodiscard]] auto size () const { return maxima.size (); }
      [[nodiscard]] bool empty () const { return maxima.empty (); }
      [[nodiscard]] auto begin () noexcept { return maxima.begin (); }
      [[nodiscard]] auto begin () const noexcept { return maxima.begin (); }
      [[nodiscard]] auto end () noexcept { return maxima.end (); }
      [[nodiscard]] auto end () const noexcept { return maxima.end (); }
      [[nodiscard]] auto& get_backing_vector () { return maxima; }
      [[nodiscard]] const auto& get_backing_vector () const { return maxima; }
  };
}
