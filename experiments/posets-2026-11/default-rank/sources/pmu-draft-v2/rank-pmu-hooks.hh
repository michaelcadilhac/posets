#pragma once
// Profiling companion only. No Vector/set wrappers, allocation hooks or stats.
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

#ifdef POSETS_RANK_STATS
#error "PMU requires POSETS_RANK_STATS undefined, including no =0 definition"
#endif
#if defined(POSETS_TRACK_ALLOCATIONS) && POSETS_TRACK_ALLOCATIONS
#error "PMU requires allocation hooks off"
#endif
#if defined(POSETS_LAZY_KDTREE_STATS) && POSETS_LAZY_KDTREE_STATS
#error "PMU requires lazy KD statistics off"
#endif

namespace rank_pmu {
// Verbatim event/control/serialization implementation from verified phase one.
struct event_state {
  const char* name;
  uint32_t type;
  uint64_t config;
  int fd = -1, open_errno = 0, id_errno = 0;
  uint64_t id = 0, value = 0;
  bool available = false;
};

struct pmu_group {
  std::array<event_state, 4> events {{
    {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
    {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
    {"l1d_read_miss", PERF_TYPE_HW_CACHE,
      PERF_COUNT_HW_CACHE_L1D | (uint64_t(PERF_COUNT_HW_CACHE_OP_READ) << 8) |
      (uint64_t(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)},
    {"llc_read_miss", PERF_TYPE_HW_CACHE,
      PERF_COUNT_HW_CACHE_LL | (uint64_t(PERF_COUNT_HW_CACHE_OP_READ) << 8) |
      (uint64_t(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)}
  }};
  struct read_result {
    uint64_t nr, enabled, running;
    struct { uint64_t value, id; } values[4];
  } result {};
  int reset_errno = 0, enable_errno = 0, disable_errno = 0, read_errno = 0;
  long read_bytes = -1;
  bool enabled = false, read_valid = false;

  static int control(int fd, unsigned long command, unsigned long argument) {
    int code;
    do { code = ioctl(fd, command, argument); } while (code < 0 and errno == EINTR);
    return code;
  }
  void open() {
    for (size_t i = 0; i < events.size(); ++i) {
      auto& event = events[i];
      if (i > 0 and events[0].fd < 0) continue;
      perf_event_attr attr {};
      attr.size = sizeof(attr);
      attr.type = event.type;
      attr.config = event.config;
      attr.disabled = i == 0;
      attr.pinned = i == 0;
      attr.exclude_kernel = 1;
      attr.exclude_hv = 1;
      attr.inherit = 0;
      attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID |
                         PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
      event.fd = int(syscall(SYS_perf_event_open, &attr, 0, -1,
                             i == 0 ? -1 : events[0].fd, PERF_FLAG_FD_CLOEXEC));
      if (event.fd < 0) { event.open_errno = errno; continue; }
      if (ioctl(event.fd, PERF_EVENT_IOC_ID, &event.id) < 0) event.id_errno = errno;
    }
  }
  void start() {
    if (events[0].fd < 0) return;
    if (control(events[0].fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0) {
      reset_errno = errno; return;
    }
    if (control(events[0].fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0) {
      enable_errno = errno; return;
    }
    enabled = true;
  }
  void stop() {
    if (not enabled) return;
    if (control(events[0].fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0) {
      disable_errno = errno; close_all(); enabled = false; return;
    }
    enabled = false;
    do { read_bytes = ::read(events[0].fd, &result, sizeof(result)); }
    while (read_bytes < 0 and errno == EINTR);
    if (read_bytes < 0) { read_errno = errno; return; }
    read_valid = result.nr <= 4 and read_bytes == long(24 + result.nr * 16);
    if (not read_valid) return;
    for (auto& event : events)
      for (size_t i = 0; i < result.nr; ++i)
        if (event.id_errno == 0 and event.id != 0 and event.id == result.values[i].id) {
          event.available = true;
          event.value = result.values[i].value;
        }
  }
  const char* status() const {
    if (reset_errno or enable_errno or disable_errno) return "ioctl_error";
    if (events[0].fd < 0) return "unavailable";
    if (read_bytes == 0) return "unscheduled_or_error_state";
    if (not read_valid) return "read_error";
    if (result.running == 0) return "unscheduled";
    if (result.running < result.enabled) return "multiplexed";
    if (result.running > result.enabled) return "invalid_time";
    return "ok";
  }
  static const char* event_status(const event_state& event, bool leader_available) {
    if (event.fd >= 0 and event.id_errno == 0) return event.available ? "read" : "not_counted";
    if (event.id_errno) return "id_error";
    if (not leader_available and event.open_errno == 0) return "leader_unavailable";
    if (event.open_errno == EACCES or event.open_errno == EPERM) return "denied";
    if (event.open_errno == EINVAL or event.open_errno == EOPNOTSUPP or event.open_errno == ENOENT)
      return "unsupported_or_invalid";
    return "open_error";
  }
  void print() const {
    std::cout << "pmu_group_status=" << status() << " attr_size=" << sizeof(perf_event_attr)
              << " per_thread=1 cpu_argument=-1 pinned=1 inherit=0 exclude_kernel=1 exclude_hv=1"
              << " read_format=" << (PERF_FORMAT_GROUP | PERF_FORMAT_ID |
                    PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING)
              << " reset_errno=" << reset_errno << " enable_errno=" << enable_errno
              << " disable_errno=" << disable_errno << " read_errno=" << read_errno
              << " read_bytes=" << read_bytes << " time_available=" << read_valid;
    if (read_valid)
      std::cout << " time_enabled=" << result.enabled << " time_running=" << result.running;
    std::cout << '\n';
    if (read_valid)
      for (size_t i = 0; i < result.nr; ++i)
        std::cout << "group_raw_id=" << result.values[i].id
                  << " group_raw_count=" << result.values[i].value << '\n';
    for (const auto& event : events) {
      std::cout << "event=" << event.name << " type=" << event.type << " config=" << event.config
                << " config_hex=0x" << std::hex << event.config << std::dec
                << " id=" << event.id << " status=" << event_status(event, events[0].fd >= 0)
                << " open_errno=" << event.open_errno << " id_errno=" << event.id_errno
                << " raw_available=" << event.available;
      if (event.available) std::cout << " raw_count=" << event.value;
      std::cout << '\n';
    }
  }
  void close_all() {
    for (auto& event : events)
      if (event.fd >= 0) { ::close(event.fd); event.fd = -1; }
  }
  ~pmu_group() { close_all(); }
};

static pmu_group counters;
static size_t selected_call = 0, next_call = 0, completed_regions = 0;
static bool selected = false, inside_region = false, initialized = false;
static bool check_only = false;
static const char* phase_name = nullptr;

static void require(bool condition, const char* message) {
  if (not condition) throw std::runtime_error(message);
}
static void initialize() {
  const char* mode = std::getenv("POSETS_RANK_PMU_MODE");
  phase_name = std::getenv("POSETS_RANK_PMU_PHASE");
  require(mode and (std::strcmp(mode, "check") == 0 or std::strcmp(mode, "measure") == 0),
          "set POSETS_RANK_PMU_MODE=check|measure");
  require(phase_name and (std::strcmp(phase_name, "build") == 0 or std::strcmp(phase_name, "cpre") == 0),
          "set POSETS_RANK_PMU_PHASE=build|cpre");
  check_only = std::strcmp(mode, "check") == 0;
  selected_call = std::strcmp(phase_name, "build") == 0 ? 0 : 1;
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  require(sched_getaffinity(0, sizeof(affinity), &affinity) == 0,
          "could not read CPU affinity");
  require(CPU_COUNT(&affinity) == 1 and CPU_ISSET(2, &affinity),
          "PMU companion requires exactly CPU2; no fallback");
  initialized = true;
  std::cout << "rank_pmu_diagnostic=1 clocks_discarded=1 cpu_affinity=2"
            << " process_id=" << getpid() << " thread_id=" << syscall(SYS_gettid)
            << " mode=" << mode << " selected_phase=" << phase_name
            << " selected_region=" << selected_call << '\n';
  if (not check_only) counters.open();
}
static void profile_start() {
  require(initialized and not check_only and not inside_region and next_call < 4,
          "unexpected PMU region start");
  inside_region = true;
  selected = next_call++ == selected_call;
  if (selected) counters.start();
}
static void profile_stop() {
  require(inside_region, "PMU stop without start");
  if (selected) { counters.stop(); ++completed_regions; }
  selected = false;
  inside_region = false;
}
static int finish(int kernel_exit) {
  counters.stop();
  const bool roi_valid = not inside_region and
      (check_only ? next_call == 0 and completed_regions == 0
                  : next_call == 4 and completed_regions == 1);
  const bool core_usable = not check_only and
      std::strcmp(counters.status(), "ok") == 0 and
      counters.result.enabled > 0 and counters.result.running == counters.result.enabled and
      counters.events[0].available and counters.events[1].available;
  bool all_events = core_usable;
  for (const auto& event : counters.events) all_events = all_events and event.available;
  std::cout << "rank_pmu_kernel_exit=" << kernel_exit << " rank_pmu_roi_valid=" << roi_valid
            << " total_region_calls=" << next_call << " captured_regions=" << completed_regions
            << " selected_region=" << selected_call << " check_only=" << check_only
            << " pmu_core_usable=" << core_usable
            << " pmu_all_events_available=" << all_events << '\n';
  if (not check_only) counters.print();
  // Successful semantics/ROI and usable PMU events are separate statuses.
  // Controller acceptance must check both, never treat missing events as zeros.
  return kernel_exit == 0 and roi_valid ? 0 : (kernel_exit != 0 ? kernel_exit : 1);
}
static void failed() {
  counters.stop();
  if (initialized and not check_only) counters.print();
}
} // namespace rank_pmu

#define CALLGRIND_START_INSTRUMENTATION rank_pmu::profile_start()
#define CALLGRIND_STOP_INSTRUMENTATION rank_pmu::profile_stop()
