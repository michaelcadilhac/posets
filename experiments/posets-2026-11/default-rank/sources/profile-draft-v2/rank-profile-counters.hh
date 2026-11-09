#pragma once

#include <cstddef>
#include <cstdint>

// Profiling-only, single-threaded, allocation-free hooks. Never ship this overlay.
namespace rank_probe {
  struct vector_counts {
      std::uint64_t copy_attempts = 0, move_constructions_completed = 0;
      std::uint64_t move_assignments_completed = 0, base_adoptions_completed = 0;
      std::uint64_t partial_order_attempts = 0, meet_attempts = 0;
  };
  struct counters {
      std::uint64_t rank_started = 0, rank_completed = 0;
      std::uint64_t rank_active_coordinates_planned = 0, rank_active_coordinates_completed = 0;
      std::uint64_t intersections_started = 0, intersections_completed = 0;
      std::uint64_t intersections_noop = 0, intersections_changed = 0;
      std::size_t last_left_size = 0, last_queries = 0, last_first_failed_query = 0;
      bool last_has_failed_query = false;
      const char* last_reason = "not_called";
      vector_counts vectors;
  };
  inline counters work;
  inline bool active = false;

  inline void rank_enter (std::size_t dimension) noexcept {
    if (active) {
      ++work.rank_started;
      work.rank_active_coordinates_planned += dimension;
    }
  }
  inline void rank_leave (std::size_t dimension) noexcept {
    if (active) {
      ++work.rank_completed;
      work.rank_active_coordinates_completed += dimension;
    }
  }
  inline void intersection_enter (std::size_t left_size) noexcept {
    if (active) {
      ++work.intersections_started;
      work.last_left_size = left_size;
      work.last_queries = 0;
      work.last_has_failed_query = false;
      work.last_first_failed_query = 0;
      work.last_reason = "incomplete";
    }
  }
  inline void intersection_query (std::size_t index, bool covered) noexcept {
    if (active) {
      ++work.last_queries;
      if (not covered and not work.last_has_failed_query) {
        work.last_has_failed_query = true;
        work.last_first_failed_query = index;
      }
    }
  }
  inline void intersection_leave (bool noop, const char* reason = "no_query") noexcept {
    if (active) {
      ++work.intersections_completed;
      if (noop)
        ++work.intersections_noop;
      else
        ++work.intersections_changed;
      work.last_reason = work.last_queries == 0 ? reason : "queried";
    }
  }
}
