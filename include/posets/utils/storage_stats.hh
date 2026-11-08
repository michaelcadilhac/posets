#pragma once

#include <cstddef>
#include <ostream>

namespace posets::utils {
  // Known retained capacity bytes, excluding allocator overhead and opaque
  // Vector-owned payloads. Inline labels are reported separately but not added twice.
  // Plain diagnostic record.
  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  struct storage_stats {
      size_t object_bytes;
      size_t node_bytes;
      size_t reachable_nodes;
      size_t node_capacity;
      size_t node_storage_bytes;
      size_t separate_label_storage_bytes;
      size_t logical_label_bytes;
      size_t vector_object_bytes;
      size_t scratch_bytes;
      size_t cache_bytes;

      [[nodiscard]] size_t known_retained_bytes () const {
        return object_bytes + node_storage_bytes + separate_label_storage_bytes +
               vector_object_bytes + scratch_bytes + cache_bytes;
      }
  };
  // NOLINTEND(misc-non-private-member-variables-in-classes)
  inline std::ostream& operator<< (std::ostream& os, const storage_stats& s) {
    return os << "object=" << s.object_bytes << " node_size=" << s.node_bytes
              << " reachable=" << s.reachable_nodes << " capacity=" << s.node_capacity
              << " node_storage=" << s.node_storage_bytes
              << " separate_labels=" << s.separate_label_storage_bytes
              << " logical_labels=" << s.logical_label_bytes
              << " vector_objects=" << s.vector_object_bytes << " scratch=" << s.scratch_bytes
              << " cache=" << s.cache_bytes << " known_retained=" << s.known_retained_bytes ();
  }
}
