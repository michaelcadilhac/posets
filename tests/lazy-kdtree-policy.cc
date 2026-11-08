#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/vectors.hh>

namespace {
  struct policy_case {
      uint64_t bits;
      bool dead, buffer;
  };

  template <class Float, bool Cascade>
  bool check () {
    if constexpr (std::numeric_limits<Float>::is_iec559 and
                  std::numeric_limits<Float>::digits == 53 and
                  std::numeric_limits<Float>::radix == 2 and
                  std::numeric_limits<Float>::max_exponent == 1024 and
                  sizeof (Float) == sizeof (uint64_t)) {
      using vector_type = posets::vectors::vector_backed<int8_t>;
      using downset_type = posets::downsets::lazy_kdtree_backed<vector_type, Cascade>;
      // Expected outcomes are explicit; no floating-point comparison is an oracle.
      constexpr std::array cases {
          policy_case {
              .bits = UINT64_C (0x0000000000000000), .dead = true, .buffer = false},  // +zero
          policy_case {
              .bits = UINT64_C (0x8000000000000000), .dead = true, .buffer = false},  // -zero
          policy_case {.bits = UINT64_C (0x0000000000000001),
                       .dead = true,
                       .buffer = true},  // +min subnormal
          policy_case {.bits = UINT64_C (0x000fffffffffffff),
                       .dead = true,
                       .buffer = true},  // +max subnormal
          policy_case {.bits = UINT64_C (0x8000000000000001),
                       .dead = false,
                       .buffer = false},  // -min subnormal
          policy_case {.bits = UINT64_C (0x800fffffffffffff),
                       .dead = false,
                       .buffer = false},  // -max subnormal
          policy_case {
              .bits = UINT64_C (0x0010000000000000), .dead = true, .buffer = true},  // +min normal
          policy_case {.bits = UINT64_C (0x8010000000000000),
                       .dead = false,
                       .buffer = false},  // -min normal
          policy_case {
              .bits = UINT64_C (0x3fd0000000000000), .dead = true, .buffer = true},  // 0.25
          policy_case {
              .bits = UINT64_C (0x3fefffffffffffff), .dead = true, .buffer = true},  // before 1
          policy_case {.bits = UINT64_C (0x3ff0000000000000), .dead = false, .buffer = true},  // 1
          policy_case {
              .bits = UINT64_C (0x3ff0000000000001), .dead = false, .buffer = false},  // after 1
          policy_case {
              .bits = UINT64_C (0xbff0000000000000), .dead = false, .buffer = false},  // -1
          policy_case {
              .bits = UINT64_C (0x7ff0000000000000), .dead = false, .buffer = false},  // +infinity
          policy_case {
              .bits = UINT64_C (0xfff0000000000000), .dead = false, .buffer = false},  // -infinity
          policy_case {.bits = UINT64_C (0x7ff8000000000000),
                       .dead = false,
                       .buffer = false},  // +quiet NaN
          policy_case {.bits = UINT64_C (0xfff8000000000000),
                       .dead = false,
                       .buffer = false},  // -quiet NaN
          policy_case {.bits = UINT64_C (0x7ff0000000000001),
                       .dead = false,
                       .buffer = false},  // +signaling NaN
          policy_case {.bits = UINT64_C (0xfff0000000000001),
                       .dead = false,
                       .buffer = false}  // -signaling NaN
      };
      for (const auto test : cases)
        for (const bool buffer : {false, true}) {
          // Force runtime classification, including the process's DAZ/FTZ mode.
          const volatile uint64_t runtime_bits = test.bits;
          auto policy = posets::downsets::lazy_kdtree_policy {};
          (buffer ? policy.buffer_fraction : policy.dead_fraction) =
              std::bit_cast<Float> (static_cast<uint64_t> (runtime_bits));
          bool accepted;
          try {
            const downset_type subject (std::vector<vector_type> {}, policy);
            accepted = true;
          } catch (const std::invalid_argument&) {
            accepted = false;
          }
          if (accepted != (buffer ? test.buffer : test.dead)) {
            std::cerr << "wrong policy acceptance for bits " << std::hex << test.bits
                      << " buffer=" << buffer << '\n';
            return false;
          }
        }
    }
    return true;
  }
}

int main () try {
  return check<double, true> () and check<double, false> () ? 0 : 1;
} catch (...) {
  return 1;
}
