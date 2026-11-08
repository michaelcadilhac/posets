#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <type_traits>
#include <cxxabi.h>
#include <string>

#include <posets/concepts.hh>

/// FOREACH
// clang-format off
#define PARENS ()
#define EXPAND(arg) EXPAND1(EXPAND1(EXPAND1(EXPAND1(arg))))
#define EXPAND1(arg) EXPAND2(EXPAND2(EXPAND2(EXPAND2(arg))))
#define EXPAND2(arg) EXPAND3(EXPAND3(EXPAND3(EXPAND3(arg))))
#define EXPAND3(arg) EXPAND4(EXPAND4(EXPAND4(EXPAND4(arg))))
#define EXPAND4(arg) arg
#define FOR_EACH(macro, ...)                                    \
  __VA_OPT__(EXPAND(FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define FOR_EACH_HELPER(macro, a1, ...)                         \
  macro(a1)                                                     \
  __VA_OPT__(FOR_EACH_AGAIN PARENS (macro, __VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER
// clang-format on

template <typename SetType>
class test_t;

template <typename ResType>
struct generic_test {
    virtual ResType operator () () = 0;  // NOLINT(portability-template-virtual-member-function)
    virtual ~generic_test () = default;
};

template <typename ResType>
struct test_list {
    static inline std::map<std::string, std::function<ResType ()> > list;
};

template <typename... Types>
struct type_list;

template <template <typename Arg> typename... Types>
struct template_type_list;

template <typename T> struct vector_name;
static std::set<std::string> set_names, vector_names;


#define get_vector_name(V) std::string {vector_name<V>::str}
template <typename S>
struct set_name {
    static std::string get () {
      int status;
      char* name = abi::__cxa_demangle (typeid(S).name (), nullptr, nullptr, &status);
      const std::string result (name);
      free (name);
      return result.substr (0, result.find_first_of ('<'));
    }
};
#define get_set_name(S) set_name<S>::get ()

template <typename S>
struct full_name {
    static auto get () {
      using value_type = typename S::value_type;
      return get_set_name (S) + "<" + get_vector_name (value_type) + ">";
    }
};
#define get_full_name(S) full_name<S>::get ()

#define DEFINE_VECTOR_NAME(V) template <> struct vector_name<V> { static constexpr auto str = #V; };
#define DEFINE_VECTOR_NAMES(...)
#define VECTOR_TYPES(...) FOR_EACH(DEFINE_VECTOR_NAME, __VA_ARGS__)     \
  using vector_types = type_list<__VA_ARGS__>;

namespace posets::downsets {
  template <typename VecType>
  struct all { using value_type = VecType; };
}

namespace posets::vectors {
  struct all {};
}

template <>
struct vector_name<posets::vectors::all> {
    static constexpr auto str = "posets::vectors::all";
};

// One set, one vector
template <bool CreateAll = true, template <typename V> typename SetType, typename VecType>
void register_maker (type_list<VecType>*, SetType<VecType>* = nullptr) {
  vector_names.insert (vector_name<VecType>::str);

  using res_t = decltype(std::declval<test_t<SetType<VecType>>> () ());
  auto ts = get_full_name (SetType<VecType>);
  auto test = [ts] () {
    std::cout << "[--] running tests for " << ts << std::endl << std::flush;
    if constexpr (std::is_same_v<res_t, void>) {
      test_t<SetType<VecType>> () ();
      std::cout << "\r[OK]" << '\n';
    }
    else {
      auto res = test_t<SetType<VecType>> () ();
      std::cout << "\r[OK]" << '\n';
      return res;
    }
  };

  auto& tests = test_list<std::invoke_result_t<decltype(test)>>::list;
  tests[ts] = test;

  if constexpr (CreateAll) {
    auto setallvec = get_set_name (SetType<VecType>) + "<posets::vectors::all>";

    for (auto&& ts : {
        get_full_name (posets::downsets::all<VecType>),
        setallvec,
        get_full_name (posets::downsets::all<posets::vectors::all>)
      }) {
      auto prev = tests[ts];
      tests[ts] = [prev, test] () {
        if (prev) prev ();
        return test ();
      };
    }
  }
}

// One set, multiple vectors
template <bool CreateAll = true, template <typename V> typename SetType, typename VecType,
          typename VecType2, typename... VecTypes>
void register_maker (type_list<VecType, VecType2, VecTypes...>*, SetType<VecType>* = nullptr) {
  set_names.insert (get_set_name (SetType<VecType>));
  register_maker<CreateAll, SetType> ((type_list<VecType>*) nullptr);
  register_maker<CreateAll, SetType> ((type_list<VecType2, VecTypes...>*) nullptr);
}

// Multiple sets, multiple vectors
template <bool CreateAll = true, template <typename V> typename SetType,
          template <typename V> typename... SetTypes, typename... VecTypes>
void register_maker (type_list<VecTypes...>* v,
                     template_type_list<SetType, SetTypes...>* = nullptr) {
  register_maker<CreateAll, SetType> (v);
  register_maker<CreateAll, SetTypes...> (v);
}
