#include "matrix-check.hh"

#include <cstdint>
#include <exception>
#include <iostream>
#include <utility>

#include <posets/utils/kdtree.hh>
#include <posets/vectors.hh>

namespace {
  using matrix_check::logical;
  using matrix_check::region;
  using matrix_check::require;

  template <class V>
  std::vector<V> values (const region& rows) {
    std::vector<V> result;
    for (const auto& row : rows)
      result.push_back (matrix_check::vector<V> (row));
    return result;
  }

  bool covered (const region& rows, const logical& query, bool strict) {
    return std::ranges::any_of (rows, [&] (const logical& row) {
      return matrix_check::below (query, row) and (not strict or query != row);
    });
  }

  template <class V>
  void check (const posets::utils::kdtree<V>& tree, const region& rows) {
    region probes = rows;
    probes.emplace_back (rows.front ().size (), -2);
    probes.emplace_back (rows.front ().size (), 20);
    for (int repeat = 0; repeat < 3; ++repeat)
      for (const bool strict : {false, true, false, true})
        for (const auto& raw : probes)
          require (tree.dominates (matrix_check::vector<V> (raw), strict) ==
                       covered (rows, raw, strict),
                   "KD scratch reuse differs from scalar oracle");
  }

  region ladder () {
    region rows;
    for (int i = 0; i < 17; ++i)
      rows.push_back ({i, 16 - i, 0, 0, 0, 0, 0});
    return rows;
  }

  template <class V>
  void lifecycle () {
    // Repeated equality/greater splits must not credit the same axis twice.
    const region rows {{0, 4}, {1, 3}, {2, 2}, {3, 1}, {4, 0}};
    posets::utils::kdtree<V> tree (values<V> (rows));
    for (int repeat = 0; repeat < 3; ++repeat)
      for (const bool strict : {false, true}) {
        require (not tree.dominates (matrix_check::vector<V> ({2, 3}), strict),
                 "KD credited an axis twice");
        require (tree.dominates (matrix_check::vector<V> ({1, 1}), strict),
                 "KD lost successful path after failed query");
        check (tree, rows);
      }
    posets::utils::kdtree<V> moved (std::move (tree));
    check (moved, rows);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    tree.relabel_tree (values<V> (ladder ()));
    check (tree, ladder ());
    tree = std::move (moved);
    check (tree, rows);
    auto* self = &tree;
    tree = std::move (*self);
    check (tree, rows);
    for (const auto& replacement :
         {ladder (), region {{1}}, rows, region {{0, 0}, {2, 0}, {0, 2}}, ladder ()}) {
      tree.relabel_tree (values<V> (replacement));
      check (tree, replacement);
    }
  }

  enum class FAILURE : std::uint8_t { NONE, COORDINATE, ORDER, COMPARISON };
  FAILURE active_failure = FAILURE::NONE;
  long calls_left = -1;
  struct query_failure : std::exception {};

  void charge (FAILURE operation) {
    if (active_failure == operation and calls_left-- == 0)
      throw query_failure {};
  }

  // Fault injection and typed results intentionally replace the base operations.
  // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)
  class throwing_vector : public posets::vectors::vector_backed<int16_t> {
      using base = posets::vectors::vector_backed<int16_t>;
      using inner_order =
          decltype (std::declval<const base&> ().partial_order (std::declval<const base&> ()));
      class order {
          inner_order inner;

        public:
          explicit order (inner_order&& value) : inner (value) {}
          bool leq () {
            charge (FAILURE::COMPARISON);
            return inner.leq ();
          }
          bool geq () {
            charge (FAILURE::COMPARISON);
            return inner.geq ();
          }
      };
      explicit throwing_vector (base&& value) : base (std::move (value)) {}

    public:
      using base::base;
      throwing_vector (const throwing_vector&) = delete;
      throwing_vector (throwing_vector&&) = default;
      throwing_vector& operator= (throwing_vector&&) = default;
      [[nodiscard]] auto operator[] (size_t coordinate) const {
        charge (FAILURE::COORDINATE);
        return base::operator[] (coordinate);
      }
      [[nodiscard]] auto partial_order (const throwing_vector& other) const {
        charge (FAILURE::ORDER);
        return order {base::partial_order (other)};
      }
      [[nodiscard]] throwing_vector copy () const { return throwing_vector (base::copy ()); }
      [[nodiscard]] throwing_vector meet (const throwing_vector& other) const {
        return throwing_vector (base::meet (other));
      }
      [[nodiscard]] throwing_vector join (const throwing_vector& other) const {
        return throwing_vector (base::join (other));
      }
  };
  // NOLINTEND(bugprone-derived-method-shadowing-base-method)
  static_assert (posets::Vector<throwing_vector>);

  void exception_recovery () {
    const auto rows = ladder ();
    posets::utils::kdtree<throwing_vector> tree (values<throwing_vector> (rows));
    const auto query = matrix_check::vector<throwing_vector> ({8, 8, 0, 0, 0, 0, 0});
    for (const auto operation : {FAILURE::COORDINATE, FAILURE::ORDER, FAILURE::COMPARISON})
      for (const bool strict : {false, true}) {
        bool complete = false;
        for (long fail = 0; fail < 512; ++fail) {
          bool threw = false;
          bool found = false;
          active_failure = operation;
          calls_left = fail;
          try {
            found = tree.dominates (query, strict);
          } catch (const query_failure&) {
            threw = true;
          }
          active_failure = FAILURE::NONE;
          check (tree, rows);
          if (not threw) {
            require (found == not strict, "KD injected query changed its result");
            require (fail > 0, "KD query fault was not injected");
            complete = true;
            break;
          }
        }
        require (complete, "KD query failure sweep did not finish");
      }
    tree.relabel_tree (values<throwing_vector> ({{0, 4}, {4, 0}}));
    check (tree, {{0, 4}, {4, 0}});
  }
}

int main () try {
  lifecycle<posets::vectors::vector_backed<int16_t>> ();
  lifecycle<posets::vectors::simd_vector_backed<int16_t>> ();
  lifecycle<posets::vectors::vector_backed<double>> ();
  exception_recovery ();
  std::cout << "KD scratch restoration, lifecycle and query exception recovery PASS\n";
} catch (const std::exception& error) {
  std::cerr << error.what () << '\n';
  return 1;
}
