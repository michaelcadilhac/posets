#include "test_maker.hh"
#include "timer.hh"
#include <string_view>
#include <type_traits>

#include <cassert>
#include <cstdint>
#include <cxxabi.h>
#include <experimental/random>
#include <memory>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <vector>

#include <posets/downsets.hh>
#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

// This benchmark uses 64 coordinates independently of downset-bm.
#undef DIMENSION
#define DIMENSION 64UL
#define ROUNDS 3
#ifndef POSETS_SIMD_BM_ITEMS
# define POSETS_SIMD_BM_ITEMS (1 << 14)
#endif

size_t posets::vectors::bool_threshold = 2 * DIMENSION;
size_t posets::vectors::bitset_threshold = 2 * DIMENSION;

namespace utils = posets::utils;
using test_value_type = std::int8_t;

namespace {
  template <typename T>
  auto random_vector () {
    utils::vector_mm<T> v (DIMENSION);
    std::generate (v.begin (), v.end (), [] () { return std::experimental::randint (-1, 3); });
    return v;
  }
}

template <typename SetType>
struct test_t : public generic_test<void> {
    using v_type = SetType::value_type;
    using value_type = v_type::value_type;

    void operator() () override {
      std::experimental::reseed (0);
      double time = 0.;
      long checksum = 0;
      size_t sizes = 0;
      spot::stopwatch sw;

      for (size_t rounds = 0; rounds < ROUNDS; ++rounds) {
        std::cout << "Round " << rounds << '\n';
        sw.start ();
        SetType set (v_type (random_vector<value_type> ()));

        for (size_t i = 0; i < POSETS_SIMD_BM_ITEMS; ++i)
          set.insert (v_type (random_vector<value_type> ()));
        sizes += set.size ();
        // stop clock
        time += sw.stop ();
        for (auto&& x : set)
          checksum += x[0];
      }
      std::cout << "\nRuntime: " << time / ROUNDS << " sec, average size: " << sizes / ROUNDS
                << ", checksum: " << checksum;
    }
};

namespace posets::vectors {
  template <typename T>
  using array_backed_fixed = posets::vectors::array_backed<T, DIMENSION>;

  template <typename T>
  using array_ptr_backed_fixed = posets::vectors::array_ptr_backed<T, DIMENSION>;

  template <typename T>
  using simd_array_backed_fixed = posets::vectors::simd_array_backed<T, DIMENSION>;

  template <typename T>
  using simd_array_backed_sum_fixed = posets::vectors::simd_array_backed_sum<T, DIMENSION>;

  template <typename T>
  using simd_array_ptr_backed_fixed = posets::vectors::simd_array_ptr_backed<T, DIMENSION>;
}

VECTOR_TYPES (posets::vectors::array_backed_fixed<test_value_type>,
              posets::vectors::array_ptr_backed_fixed<test_value_type>,
              posets::vectors::simd_array_backed_fixed<test_value_type>,
              posets::vectors::simd_array_ptr_backed_fixed<test_value_type>,
              posets::vectors::simd_array_backed_sum_fixed<test_value_type>,
              posets::vectors::vector_backed<test_value_type>,
              posets::vectors::simd_vector_backed<test_value_type>);

using set_types =
    template_type_list<posets::downsets::vector_backed, posets::downsets::vector_backed_bin>;

namespace {
  void usage (const char* progname) {
    std::cout << "usage: " << progname << " SETTYPE VECTYPE" << '\n';

    std::cout << "  SETTYPE:\n  - all" << '\n';
    for (auto&& s : set_names)
      std::cout << "  - " << s.substr (s.find_last_of (':') + 1) << '\n';

    std::cout << "  VECTYPE:\n  - all" << '\n';
    for (auto&& s : vector_names) {
      auto start = s.find_last_of (':') + 1;
      std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << '\n';
    }
  }
}

int main (int argc, char* argv[]) {
  register_maker (static_cast<vector_types*> (nullptr), static_cast<set_types*> (nullptr));

  if (argc == 2 and std::string_view (argv[1]) == "--help") {
    usage (argv[0]);
    return 0;
  }
  if (argc != 3) {
    usage (argv[0]);
    return 1;
  }

  auto implem =
      std::string ("posets::downsets::") + argv[1] + "<posets::vectors::" + argv[2] + ">";

  posets::vectors::bool_threshold = 128;
  posets::vectors::bitset_threshold = 128;
  const auto& tests = test_list<void>::list;
  const auto implementation = tests.find (implem);
  if (implementation == tests.end ()) {
    std::cerr << "error: no such implem: " << implem << '\n';
    return 1;
  }
  implementation->second ();

  return 0;
}
