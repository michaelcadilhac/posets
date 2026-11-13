#include <string_view>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <posets/downsets/cst_backed.hh>
#include <posets/utils/cst.hh>
#include <posets/vectors.hh>

namespace {
  void require (bool ok, const char* message) {
    if (not ok) {
      std::cerr << message << '\n';
      std::exit (1);
    }
  }

  template <class T>
  using vector = posets::vectors::vector_backed<T>;

  // Advertise a dimension without allocating a payload. Rejection must happen
  // before reading coordinates, including through the downset constructor.
  template <class T>
  class dimension_probe : public vector<T> {
      size_t dimension;

    public:
      explicit dimension_probe (size_t k) : vector<T> (0U), dimension {k} {}
      explicit dimension_probe (std::span<const T> raw) : dimension_probe (raw.size ()) {}
      dimension_probe (dimension_probe&&) = default;
      dimension_probe& operator= (dimension_probe&&) = default;
      [[nodiscard]] size_t size () const { return dimension; }
      [[nodiscard]] T operator[] (size_t) const {
        require (false, "dimension rejection must precede coordinate access");
        return 0;
      }
      [[nodiscard]] dimension_probe copy () const { return dimension_probe (dimension); }
      [[nodiscard]] dimension_probe meet (const dimension_probe&) const { return copy (); }
      [[nodiscard]] dimension_probe join (const dimension_probe&) const { return copy (); }
  };

  template <class T>
  [[nodiscard]] vector<T> make_vector (const std::vector<T>& raw) {
    return vector<T> (std::span<const T> (raw));
  }

  template <class T>
  [[nodiscard]] auto build (posets::utils::cst<vector<T>>& tree,
                            const std::vector<std::vector<T>>& raw) {
    std::vector<vector<T>> generators;
    generators.reserve (raw.size ());
    for (const auto& g : raw)
      generators.push_back (make_vector (g));
    return tree.add_vectors (std::move (generators));
  }

  template <class T>
  [[nodiscard]] bool scan (const std::vector<std::vector<T>>& generators,
                           const std::vector<T>& query, bool strict) {
    for (const auto& g : generators) {
      bool covered = true;
      bool different = false;
      for (size_t i = 0; i < query.size (); ++i) {
        covered = covered and query[i] <= g[i];
        different = different or query[i] != g[i];
      }
      if (covered and (not strict or different))
        return true;
    }
    return false;
  }

  template <class T>
  void check_query (const posets::utils::cst<vector<T>>& tree, size_t root,
                    const std::vector<std::vector<T>>& generators, const std::vector<T>& raw) {
    const auto query = make_vector (raw);
    for (const bool strict : {false, true})
      require (tree.covers_vector (root, query, strict) == scan (generators, raw, strict),
               "CST membership disagrees with plain coordinate scan");
  }

  void prefix_regression () {
    posets::utils::cst<vector<int>> tree (2);
    const int hi = std::numeric_limits<int>::max ();
    const std::vector<std::vector<int>> generators {{hi, 1}};
    const auto root = build (tree, generators);
    for (const auto& q : std::vector<std::vector<int>> {{hi, 1}, {hi, 0}, {hi, 2}, {0, 0}})
      check_query (tree, root, generators, q);

    // Construction is small; only the query prefix overflows int32_t on main.
    posets::utils::cst<vector<int>> small (2);
    const std::vector<std::vector<int>> control {{1, 1}};
    check_query (small, build (small, control), control, std::vector<int> {hi, 1});
  }

  void arity_regression () {
    posets::utils::cst<vector<int>> tree (2);
    std::vector<std::vector<int>> generators;
    for (int i = 0; i <= 63; ++i)
      generators.push_back ({i, 63 - i});
    const auto root = build (tree, generators);
    for (int x = -1; x <= 64; ++x)
      for (int y = -1; y <= 64; ++y)
        check_query (tree, root, generators, std::vector<int> {x, y});
    require (tree.check_layer_order (), "wide sibling list remains sorted");
  }

  void narrowing_regression () {
    posets::utils::cst<vector<int64_t>> tree (1);
    const int64_t large = int64_t {1} << 32;
    const std::vector<std::vector<int64_t>> generators {{large}};
    const auto root = build (tree, generators);
    for (const int64_t q : {int64_t {1}, large - 1, large, large + 1})
      check_query (tree, root, generators, std::vector<int64_t> {q});
  }

  template <class Exception, class F>
  void expect_exception (F&& operation) {
    try {
      operation ();
    } catch (const Exception&) {
      return;
    }
    require (false, "unsupported CST dimensions must be rejected before arithmetic");
  }

  template <class T>
  void domain () {
    using tree_type = posets::utils::cst<vector<T>>;
    static_assert (
        std::is_same_v<typename tree_type::sum_t, posets::utils::vector_sum_t<vector<T>>>);
    constexpr int exponent =
        std::numeric_limits<typename tree_type::sum_t>::digits - std::numeric_limits<T>::digits;
    if constexpr (std::numeric_limits<size_t>::digits > exponent)
      expect_exception<std::length_error> (
          [] { const tree_type unsupported ((size_t {1} << exponent) + 1); });
    expect_exception<std::length_error> ([] {
      using probe = dimension_probe<T>;
      static_assert (posets::Vector<probe>);
      std::vector<probe> generators;
      generators.emplace_back (std::numeric_limits<size_t>::max ());
      const posets::downsets::cst_backed<probe> unsupported (std::move (generators));
    });
    // A sentinel layer also needs storage, even for the full-width domain.
    expect_exception<std::length_error> (
        [] { const tree_type unsupported (std::numeric_limits<size_t>::max ()); });
    tree_type tree (2);
    expect_exception<std::invalid_argument> (
        [&] { (void) build (tree, std::vector<std::vector<T>> {{1}}); });
    const std::vector<std::vector<T>> generators {{1, 1}};
    const auto root = build (tree, generators);
    expect_exception<std::invalid_argument> (
        [&] { (void) tree.covers_vector (root, make_vector (std::vector<T> {1})); });
    check_query (tree, root, generators, std::vector<T> {1, 1});
  }

  template <class T>
  void extremes () {
    const T lo = std::numeric_limits<T>::min ();
    const T hi = std::numeric_limits<T>::max ();
    posets::utils::cst<vector<T>> tree (3);
    const std::vector<std::vector<T>> generators {
        {hi, hi, hi}, {lo, lo, lo}, {hi, lo, hi}, {lo, hi, lo}, {0, 0, 0}};
    const auto root = build (tree, generators);
    for (T x : {lo, T {-1}, T {0}, T {1}, hi})
      for (T y : {lo, T {-1}, T {0}, T {1}, hi})
        for (T z : {lo, T {-1}, T {0}, T {1}, hi})
          check_query (tree, root, generators, std::vector<T> {x, y, z});
    const auto all = tree.get_all (root);
    require (all.size () == generators.size (), "extreme generators round-trip");
    for (const auto& g : generators)
      require (std::ranges::any_of (all, [&] (const auto& v) { return v == make_vector (g); }),
               "prefix subtraction recovers every extreme coordinate");
#ifndef NDEBUG
    if constexpr (std::numeric_limits<T>::digits == 63) {
      // NOLINTNEXTLINE(misc-const-correctness): cout writes through this stream's buffer.
      std::ostringstream rendered;
      auto* saved = std::cout.rdbuf (rendered.rdbuf ());
      tree.print_layer (root);
      std::cout.rdbuf (saved);
      const auto text = rendered.str ();
      require (
          not std::ranges::search (text, std::string_view {"27670116110564327421"}).empty () and
              not std::ranges::search (text, std::string_view {"-27670116110564327424"}).empty (),
          "debug output preserves positive and negative 128-bit prefixes");
    }
#endif
  }

  template <class T>
  void oracle () {
    std::mt19937 rng (4950);
    for (size_t dim = 1; dim <= 4; ++dim) {
      posets::utils::cst<vector<T>> tree (dim);
      for (size_t batch = 0; batch < 32; ++batch) {
        std::vector<std::vector<T>> generators (1 + (rng () % 16), std::vector<T> (dim));
        for (auto& g : generators)
          for (auto& coordinate : g)
            coordinate = static_cast<T> (static_cast<int> (rng () % 5) - 2);
        const auto root = build (tree, generators);
        // Duplicate vectors and a different insertion order must intern to the same root.
        auto reordered = generators;
        reordered.push_back (generators.front ());
        std::ranges::reverse (reordered);
        require (build (tree, reordered) == root, "equal generator languages share a root");
        size_t queries = 1;
        for (size_t i = 0; i < dim; ++i)
          queries *= 7;
        for (size_t index = 0; index < queries; ++index) {
          std::vector<T> query (dim);
          size_t digits = index;
          for (auto& coordinate : query) {
            coordinate = static_cast<T> (static_cast<int> (digits % 7) - 3);
            digits /= 7;
          }
          check_query (tree, root, generators, query);
        }
        require (tree.check_layer_order (), "signed sibling lists remain sorted");
        auto expected = generators;
        std::ranges::sort (expected);
        expected.erase (std::unique (expected.begin (), expected.end ()), expected.end ());
        std::vector<std::vector<T>> actual;
        for (const auto& g : tree.get_all (root)) {
          std::vector<T> raw (dim);
          for (size_t i = 0; i < dim; ++i)
            raw[i] = g[i];
          actual.push_back (std::move (raw));
        }
        std::ranges::sort (actual);
        require (actual == expected, "interning preserves exactly the generator language");
      }
    }
  }
}

int main (int argc, char** argv) try {
  const std::string_view mode = argc > 1 ? argv[1] : "all";
  if (mode == "all" or mode == "prefix")
    prefix_regression ();
  if (mode == "all" or mode == "arity")
    arity_regression ();
  if (mode == "all" or mode == "narrowing")
    narrowing_regression ();
  if (mode == "all" or mode == "extremes") {
    extremes<int8_t> ();
    extremes<int16_t> ();
    extremes<int32_t> ();
    extremes<int64_t> ();
  }
  if (mode == "all" or mode == "oracle") {
    oracle<int8_t> ();
    oracle<int32_t> ();
    oracle<int64_t> ();
  }
  if (mode == "all" or mode == "domain") {
    domain<int8_t> ();
    domain<int16_t> ();
    domain<int32_t> ();
    domain<int64_t> ();
  }
} catch (const std::exception& e) {
  std::cerr << e.what () << '\n';
  return 1;
}
