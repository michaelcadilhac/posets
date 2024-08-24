#include <cassert>
#include <span>
#include <memory>
#include <ostream>
#include <set>
#include <vector>
#include <string>
#include <type_traits>
#include <cxxabi.h>
#include <experimental/random>

#include "timer.hh"

#include <posets/utils/vector_mm.hh>
#include <posets/downsets.hh>
#include <posets/vectors.hh>

#include "test_maker.hh"

#define DIMENSION 64
#define ROUNDS 3
#define NITEMS (1 << 14)
#define MAXVAL 3

size_t posets::vectors::bool_threshold = 2 * DIMENSION;
size_t posets::vectors::bitset_threshold = 2 * DIMENSION;

namespace utils = posets::utils;
using test_value_type = char;

template <typename T>
auto random_vector () {
  utils::vector_mm<T> v(DIMENSION);
  std::generate (v.begin(), v.end(), [] () {return std::experimental::randint (-1, 3); });
  return v;
}

template<typename SetType>
struct test_t : public generic_test<void> {
    using VType = typename SetType::value_type;
    using value_type = typename VType::value_type;

    void operator() () {
      std::experimental::reseed(0);
      double time = 0.;
      long checksum = 0;
      size_t sizes = 0;
      spot::stopwatch sw;

      for (size_t rounds = 0; rounds < ROUNDS; ++rounds) {
        std::cout << "Round " << rounds << std::endl;
        sw.start ();
        SetType set (VType (random_vector<value_type> ()));

        for (size_t i = 0; i < NITEMS; ++i)
          set.insert (VType (random_vector<value_type> ()));
        sizes += set.size ();
        // stop clock
        time += sw.stop ();
        for (auto&& x : set)
          checksum += x[0];
      }
      std::cout << "\nRuntime: " << time / ROUNDS << " sec, average size: " << sizes / ROUNDS << ", checksum: " << checksum;
    }
};

namespace posets::vectors {
  template <typename T>
  using simd_array_backed_sum_fixed = posets::vectors::simd_array_backed_sum<T, DIMENSION>;

  template <typename T>
  using simd_array_backed_fixed = posets::vectors::simd_array_backed<T, DIMENSION>;

  template <typename T>
  using array_backed_fixed = posets::vectors::array_backed<T, DIMENSION>;

  template <typename T>
  using array_backed_sum_fixed = posets::vectors::array_backed_sum<T, DIMENSION>;
}

using vector_types = type_list<posets::vectors::vector_backed<test_value_type>,
                               posets::vectors::array_backed_fixed<test_value_type>,
                               posets::vectors::array_backed_sum_fixed<test_value_type>,
                               posets::vectors::simd_vector_backed<test_value_type>,
                               posets::vectors::simd_array_backed_fixed<test_value_type>,
                               posets::vectors::simd_array_backed_sum_fixed<test_value_type>>;

using set_types = template_type_list<posets::downsets::vector_backed,
                                     posets::downsets::vector_backed_bin>;

void usage (const char* progname) {
  std::cout << "usage: " << progname << " SETTYPE VECTYPE" << std::endl;

  std::cout << "  SETTYPE:\n  - all" << std::endl;
  for (auto&& s : set_names) {
    std::cout << "  - " << s.substr (s.find_last_of (':') + 1) << std::endl;
  }

  std::cout << "  VECTYPE:\n  - all" << std::endl;
  for (auto&& s : vector_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << std::endl;
  }

  exit (0);
}

int main (int argc, char* argv[]) {
  register_maker ((vector_types*) 0, (set_types*) 0);

  if (argc != 3)
    usage (argv[0]);

  auto implem = std::string ("posets::downsets::") + argv[1]
    + "<posets::vectors::" + argv[2] + (argv[2][strlen (argv[2]) - 1] == '>' ? " " : "") + ">";

  try {
    posets::vectors::bool_threshold = 128;
    posets::vectors::bitset_threshold = 128;
    auto& tests = test_list<void>::list;
    tests[implem] ();
  } catch (std::bad_function_call& e) {
    std::cout << "error: no such implem: " << implem << std::endl;
  }

  return 0;
}
