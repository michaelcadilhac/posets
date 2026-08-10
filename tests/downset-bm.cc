// TODO : Sorting and Selection in Posets | SIAM Journal on Computing | Vol. 40, No. 3 | Society for Industrial and Applied Mathematics
// SORTING AND SELECTION IN POSETS
// Th. 4.5
// For vector_backed (insertion based) remove insert, and allow constructor on list
// insert only if non dominated, this would replace it
// dynamic data structure (vector vs kdtree) by gaperez
// bm on syntcomp

#include <cassert>
#include <span>
#include <memory>
#include <ostream>
#include <set>
#include <vector>
#include <string>
#include <type_traits>
#include <cxxabi.h>
#include <random>
#include <ranges>
#include <algorithm>
#include <cstdint>

#include <getopt.h>

#include "timer.hh"

#undef NO_VERBOSE
#include "verbose.hh"

#include <posets/utils/vector_mm.hh>
#include <posets/downsets.hh>
#include <posets/vectors.hh>

#include "test_maker.hh"

#if __has_include(<valgrind/callgrind.h>)
  #include <valgrind/callgrind.h>
#else
  #define CALLGRIND_START_INSTRUMENTATION (void)0
  #define CALLGRIND_STOP_INSTRUMENTATION (void)0  
#endif

using namespace std::literals;
namespace utils = posets::utils;

#ifndef DIMENSION
# define DIMENSION 10UL // (128UL * 1024UL)
#endif

size_t posets::vectors::bool_threshold = DIMENSION;
size_t posets::vectors::bitset_threshold = DIMENSION;

int               utils::verbose = 0;
utils::voutstream utils::vout;

using test_value_type = std::int8_t;

using result_t = std::map<std::string, double>;

namespace {
  std::default_random_engine::result_type starting_seed = 0;

  std::map<std::string, size_t> params = {
    {"maxval", 12},
    {"build", 10240 * 2},
    {"query", 10240 * 4},
    {"transfer", 1000000},
    {"intersection", 256},
    {"union", 512 * 20},
    {"cpre", 128},
    {"cpre_actions", 8},
    {"bitset_tail", 0},
    {"rounds", 1}
  };

  std::mt19937 rand_gen;
  std::uniform_int_distribution<uint32_t> distrib;

  template <typename T>
  auto random_vector () {
    utils::vector_mm<T> v (DIMENSION);
    std::generate (v.begin(), v.end(), [&] () { return distrib (rand_gen); });
    return v;
  }

  struct {
      size_t t1_sz, t1_in, t1_out;
      size_t t2_sz;
      size_t t3_sz;
      size_t t4_sz;
  } test_chk = {};
}


template <class T, class = void>
struct has_insert : std::false_type {};

template <class T>
struct has_insert<T, std::void_t<decltype(std::declval<T>().insert (std::declval<typename T::value_type> ()))>> : std::true_type {};

#define chk(Field, Value, Fail)                                                              \
  do {                                                                                       \
    if ((Field) == 0)                                                                        \
      (Field) = (Value);                                                                     \
    else if ((Field) != (Value)) {                                                       \
      verb_do (0, vout << ((Fail) ? "[FAIL] " : "[WARN] ") << #Field " != " #Value << '\n'); \
      if (Fail)                                                                              \
        abort ();                                                                            \
    }                                                                                        \
  } while (0)


template <typename SetType>
struct test_t : public generic_test<result_t> {
    using v_type = typename SetType::value_type;
    using value_type = typename v_type::value_type;

    SetType vec_to_set (std::vector<v_type>&& v) {
      if constexpr (has_insert<SetType>::value) {
        SetType set (std::move (v[0]));
        for (size_t i = 1; i < v.size (); ++i)
          set.insert (std::move (v[i]));
        return set;
      }
      else
        return SetType (std::move (v));
    }

    std::vector<v_type> test_vector (size_t nitems, ssize_t delta = 0) {
      rand_gen.seed (starting_seed);
      verb_do (3, vout << "Creating a vector of " << nitems << " elements... " << std::flush);
      std::vector<v_type> elts;
      elts.reserve (nitems);
      for (size_t i = 0; i < nitems; ++i) {
        auto r = random_vector<value_type> ();
        for (auto&& x : r)
          if (x > 0)
            x += delta;
        const size_t bitset_tail = std::min (params["bitset_tail"], r.size ());
        for (size_t j = r.size () - bitset_tail; j < r.size (); ++j)
          r[j] = static_cast<value_type> ((r[j] & 1) - 1);
        elts.push_back (v_type (std::move (r)));
      }
      verb_do (3, vout << "done.\n" << std::flush);
      return elts;
    }

    result_t operator() () override {
      result_t res;

      auto prounds = params["rounds"];
      if (params["build"] != 0) {
        verb_do (1, vout << "Test 1: Insertion and membership query\n");
        spot::stopwatch sw;
        double buildtime = 0;
        double querytime = 0;
        double transfertime = 0;
        for (size_t rounds = 0; rounds < prounds; ++rounds) {
          verb_do (2, vout << "Round " << rounds << '\n');
          verb_do (2, vout << "BUILD..." << std::flush);
          auto vec1 = test_vector (params["build"]);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          auto set = vec_to_set (std::move (vec1));
          CALLGRIND_STOP_INSTRUMENTATION;
          buildtime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << '\n');
          chk (test_chk.t1_sz, set.size (), false);
          if (params["query"] != 0) {
            verb_do (2, vout << "QUERY..." << std::flush);
            auto vec2 = test_vector (params["query"], -1);
            size_t in = 0;
            size_t out = 0;
            sw.start ();
            CALLGRIND_START_INSTRUMENTATION;
            for (auto&& x : vec2)
              if (set.contains (x))
                ++in;
              else
                ++out;
            CALLGRIND_STOP_INSTRUMENTATION;
            querytime += sw.stop ();
            verb_do (2, vout << "... IN: " << in << " OUT: " << out << '\n');
            chk (test_chk.t1_in, in, true);
            chk (test_chk.t1_out, out, true);
          }
#ifdef POSETS_BBOX_STATS
          if constexpr (requires (const SetType& stats_set) {
                          stats_set.print_bbox_stats (std::cerr);
                        }) {
            set.print_bbox_stats (std::cerr);
            std::cerr << '\n';
          }
#endif
          if (auto tr = params["transfer"]) {
            verb_do (2, vout << "TRANSFER..." << std::flush);
            sw.start ();
            CALLGRIND_START_INSTRUMENTATION;
            for (size_t i = 0; i < tr; ++i) {
              auto set2 (std::move (set));
              chk (test_chk.t1_sz, set2.size (), false);
              set = std::move (set2);
              chk (test_chk.t1_sz, set.size (), false);
            }
            CALLGRIND_STOP_INSTRUMENTATION;
            transfertime += sw.stop ();
            verb_do (2, vout << " done.\n");
            chk (test_chk.t1_sz, set.size (), false);
          }
        }

        res["build"] = buildtime;
        res["query"] = querytime;
        res["transfer"] = transfertime;

        verb_do (1, vout << "BUILD: " << buildtime / prounds << ", QUERY: " << querytime / prounds
                         << ", TRANSFER: " << transfertime / prounds << '\n');
      }

      if (params["intersection"] != 0) {
        verb_do (1, vout << "Test 2: Intersections\n");

        const size_t nitems = params["intersection"];
        spot::stopwatch sw;
        double intertime = 0;

        for (size_t rounds = 0; rounds < prounds; ++rounds) {
          verb_do (2, vout << "Round " << rounds << '\n');
          verb_do (2, vout << "BUILD\n");
          auto set = vec_to_set (test_vector (nitems));
          auto vec = test_vector (2 * nitems);
          decltype (vec) vec2;
          vec2.insert (vec2.end (), std::make_move_iterator (vec.begin () + (nitems / 2)),
                       std::make_move_iterator (vec.begin () + ((3 * nitems) / 2)));
          auto set2 = vec_to_set (std::move (vec2));
          verb_do (2, vout << "INTERSECT...";);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          set.intersect_with (std::move (set2));
          CALLGRIND_STOP_INSTRUMENTATION;
          intertime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << '\n');
          chk (test_chk.t2_sz, set.size (), false);
        }
        res["intersection"] = intertime;
        verb_do (1, vout << "INTER: " << intertime / prounds << '\n');
      }

      if (params["union"] != 0) {
        verb_do (1, vout << "Test 3: Unions\n");

        const size_t nitems = params["union"];
        spot::stopwatch sw;
        double uniontime = 0;

        for (size_t rounds = 0; rounds < prounds; ++rounds) {
          verb_do (2, vout << "Round " << rounds << '\n');

          verb_do (2, vout << "BUILD\n");
          auto set = vec_to_set (test_vector (nitems));
          auto vec = test_vector (2 * nitems);
          decltype (vec) vec2;
          vec2.insert (vec2.end (), std::make_move_iterator (vec.begin () + (nitems / 2)),
                       std::make_move_iterator (vec.begin () + ((3 * nitems) / 2)));
          auto set2 = vec_to_set (std::move (vec2));
          verb_do (2, vout << "UNION...";);
          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          set.union_with (std::move (set2));
          CALLGRIND_STOP_INSTRUMENTATION;
          uniontime += sw.stop ();
          verb_do (2, vout << " SIZE: " << set.size () << '\n');
          chk (test_chk.t3_sz, set.size (), false);
        }
        res["union"] = uniontime;
        verb_do (1, vout << "UNION: " << uniontime / prounds << '\n');
      }

      if (params["cpre"] != 0) {
        verb_do (1, vout << "Test 4: CPre-shaped apply/union/intersection\n");

        const size_t nitems = params["cpre"];
        const size_t action_count = std::max<size_t> (1, params["cpre_actions"]);
        spot::stopwatch sw;
        double cpretime = 0;

        for (size_t rounds = 0; rounds < prounds; ++rounds) {
          verb_do (2, vout << "Round " << rounds << '\n');
          auto frontier = vec_to_set (test_vector (nitems));

          auto apply_action = [&frontier, action_count] (size_t action) {
            return frontier.apply ([action, action_count] (const v_type& value) {
              utils::vector_mm<value_type> transformed (value.size ());
              for (size_t i = 0; i < value.size (); ++i) {
                transformed[i] = value[i];
                if (i % action_count == action and transformed[i] > -1)
                  --transformed[i];
              }
              return v_type (std::move (transformed));
            });
          };

          sw.start ();
          CALLGRIND_START_INSTRUMENTATION;
          auto predecessors = apply_action (0);
          for (size_t action = 1; action < action_count; ++action)
            predecessors.union_with (apply_action (action));
          frontier.intersect_with (std::move (predecessors));
          CALLGRIND_STOP_INSTRUMENTATION;
          cpretime += sw.stop ();
          verb_do (2, vout << " SIZE: " << frontier.size () << '\n');
          chk (test_chk.t4_sz, frontier.size (), false);
        }
        res["cpre"] = cpretime;
        verb_do (1, vout << "CPRE: " << cpretime / prounds << '\n');
      }

      return res;
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

  template <typename T>
  using simd_array_sum_and_bitset_fixed =
      posets::vectors::x_and_bitset<posets::vectors::simd_array_backed_sum<T, DIMENSION>,
                                   posets::vectors::nbools_to_nbitsets (DIMENSION)>;

  template <typename T>
  using simd_vector_and_bitset_fixed =
      posets::vectors::x_and_bitset<posets::vectors::simd_vector_backed<T>,
                                   posets::vectors::nbools_to_nbitsets (DIMENSION)>;
}

VECTOR_TYPES (
  posets::vectors::array_backed_fixed<test_value_type>,
  posets::vectors::array_ptr_backed_fixed<test_value_type>,
  posets::vectors::simd_array_backed_fixed<test_value_type>,
  posets::vectors::simd_array_ptr_backed_fixed<test_value_type>,
  posets::vectors::simd_array_backed_sum_fixed<test_value_type>,
  posets::vectors::simd_array_sum_and_bitset_fixed<test_value_type>,
  posets::vectors::simd_vector_and_bitset_fixed<test_value_type>,
  posets::vectors::vector_backed<test_value_type>,
  posets::vectors::simd_vector_backed<test_value_type>
  );

using set_types = template_type_list<
  posets::downsets::bboxtree_backed,
  posets::downsets::comptrie_backed,
  posets::downsets::kdtree_backed,
  posets::downsets::vector_or_kdtree_backed,
  posets::downsets::vector_backed,
  posets::downsets::rank_bucketed_vector_backed,
  posets::downsets::filtered_vector_backed,
  posets::downsets::vector_backed_bin,
  posets::downsets::vector_backed_one_dim_split_intersection_only,
  posets::downsets::sharingtree_backed,
  posets::downsets::simple_sharingtree_backed,
  posets::downsets::sharingtrie_backed,
  posets::downsets::skiplist_backed,
  posets::downsets::cst_backed
  >;


namespace {
  void usage (const char* progname, bool error = true) {
    std::cout << "usage: " << progname << " [-v -v -v...] [--params PARAMS] [--seed N] SETTYPE VECTYPE\n";

    std::cout << "List of parameters:\n";
    for (auto& [p, v] : params)
      std::cout << "  " << p << " (default: " << v << ")\n";
    std::cout << '\n';
    std::cout << "  SETTYPE:\n  - all\n";
    for (auto&& s : set_names) {
      auto start = s.find_last_of (':') + 1;
      std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << '\n';
    }

    std::cout << "  VECTYPE:\n  - all\n";
    for (auto&& s : vector_names) {
      auto start = s.find_last_of (':') + 1;
      std::cout << "  - " << s.substr (start, s.find_first_of ('>') - start + 1) << '\n';
    }

    exit (error ? 1 : 0);
  }

  const auto LONG_OPTIONS =
      std::to_array<struct option> ({{.name="seed",    .has_arg=required_argument, .flag=nullptr, .val='s'},
                                     {.name="verbose", .has_arg=no_argument,       .flag=nullptr, .val='v'},
                                     {.name="params",  .has_arg=required_argument, .flag=nullptr, .val='p'},
                                     {.name="help",    .has_arg=no_argument,       .flag=nullptr, .val='h'},
                                     {.name=nullptr,   .has_arg=0,                 .flag=nullptr, .val= 0}});
}

// NOLINTBEGIN(bugprone-exception-escape)
int main (int argc, char* argv[]) {
  const char* prog = argv[0];

  register_maker<false> ((vector_types*) nullptr, (set_types*) nullptr);

  while (true) {
    const int c = getopt_long (argc, argv, "hs:vp:", LONG_OPTIONS.data (), nullptr);
    if (c == -1)
      break;
    switch (c) {
      case 's':
        starting_seed = std::strtoul (optarg, nullptr, 10);
        break;
      case 'v':
        ++utils::verbose;
        break;
      case 'p':
        while (optarg != nullptr) {
          char* comma = strchr (optarg, ',');
          if (comma != nullptr)
            *comma = '\0';
          char* equal = strchr (optarg, '=');
          if (equal == nullptr) {
            std::cerr << "invalid parameter setting: " << optarg << '\n';
            usage (prog);
          }
          *equal = '\0';
          const std::string p {optarg};
          const std::string arg {equal + 1};
          auto pk = std::views::keys (params);
          if (std::ranges::find (pk, p) == std::ranges::end (pk)) {
            std::cerr << "unknown parameter: " << p << '\n';
            usage (prog);
          }
          params[p] = std::stoul (arg);
          optarg = (comma != nullptr) ? comma + 1 : nullptr;
        }
        break;
      case 'h':
        usage (prog, false);
        break;
      default:
        usage (prog);
    }
  }

  const decltype (distrib)::param_type distrib_params (0, (int32_t) params["maxval"]);
  distrib.param (distrib_params);

  if (argc - optind != 2)
    usage (prog);

  auto downarg = std::string {argv[optind]};
  auto vecarg = std::string {argv[optind + 1]};

  std::list<std::string> downs;
  std::list<std::string> vecs;
  auto& tests = test_list<result_t>::list;
  auto to_string_view = [] (auto&& str) {
    return std::string_view(&*str.begin(), std::ranges::distance(str));
  };

  for (auto&& da : downarg | std::ranges::views::split (',') |
                       std::ranges::views::transform (to_string_view))
    for (const auto& k : std::views::keys (tests))
      if (da == "all" or k.starts_with ("posets::downsets::"s + std::string (da))) {
        auto sub = k.substr (0, k.find ('<'));
        if (std::ranges::find (downs.begin (), downs.end (), sub) == downs.end ())
          downs.push_back (sub);
      }

  for (auto&& va : vecarg | std::ranges::views::split (',') |
                       std::ranges::views::transform (to_string_view))
    for (const auto& k : std::views::keys (tests)) {
      auto v = k.substr (k.find ('<'));
      if (va == "all" or v.starts_with ("<posets::vectors::"s + std::string (va)))
        if (std::ranges::find (vecs.begin (), vecs.end (), v) == vecs.end ())
          vecs.push_back (v);
    }

  if (downs.empty () or vecs.empty ()) {
    std::cout << "error: no such implementation.\n";
    return 1;
  }

  std::map<std::string, result_t> all_res;
  for (auto& ds : downs)
    for (auto& v : vecs) {
      posets::vectors::bool_threshold = DIMENSION;
      posets::vectors::bitset_threshold =
          DIMENSION - std::min (params["bitset_tail"], static_cast<size_t> (DIMENSION));
      all_res[ds + v] = tests[ds + v] ();
    }
  for (auto& res : all_res) {
    std::cout << "- " << res.first << ":\n";
    for (auto& test : res.second)
      std::cout << "  - " << test.first << ": " << test.second << "\n";
  }
  return 0;
}
// NOLINTEND(bugprone-exception-escape)
