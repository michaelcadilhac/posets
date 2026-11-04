#include "allocation-failure.hh"
#include "matrix-check.hh"
#include <type_traits>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/vectors.hh>

namespace {
  using matrix_check::logical;
  using matrix_check::region;
  using matrix_check::require;

  region rows (size_t dimension, int count, int value) {
    region result;
    for (int i = 0; i < count; ++i) {
      logical row (dimension, 0);
      row[i] = value;
      result.push_back (std::move (row));
    }
    return result;
  }

  const region original = rows (5, 2, 3);
  const region rhs = [] {
    region result = rows (5, 1, 2);
    result[0][1] = 2;
    result.push_back (rows (5, 3, 4).back ());
    return result;
  }();
  const region united = [] {
    auto result = original;
    result.insert (result.end (), rhs.begin (), rhs.end ());
    return result;
  }();
  const region met = rows (5, 2, 2);
  const region grown = rows (7, 4, 4);
  const region shrunk = rows (3, 1, 1);

  template <class V>
  std::vector<V> values (const region& input) {
    std::vector<V> result;
    for (const auto& row : input)
      result.push_back (matrix_check::vector<V> (row));
    return result;
  }

  template <class S>
  void check (const S& actual, const region& expected) {
    region queries = expected;
    queries.push_back (logical (expected.empty () ? 5 : expected.front ().size (), -1));
    queries.push_back (logical (queries.back ().size (), 5));
    struct view {
        using value_type = std::ranges::range_value_t<S>;
        const S& storage;
        auto size () const { return storage.size (); }
        auto begin () const { return storage.begin (); }
        auto end () const { return storage.end (); }
        bool contains (const value_type& value) const {
          if constexpr (requires { storage.contains (value); })
            return storage.contains (value);
          else
            return storage.dominates (value);
        }
    };
    matrix_check::check (view {actual}, expected, queries);
    if constexpr (requires { actual.get_all (); }) {
      std::set<logical> paths;
      for (const auto& value : actual.get_all ())
        require (paths.insert (matrix_check::unpack (value)).second, "duplicate trie path");
      require (paths == matrix_check::maxima (expected), "trie paths differ from generators");
    }
    require (actual.get_backing_vector ().size () == actual.size (), "stale vector cache");
  }

  struct move_projection {
      template <class V>
      const V& operator() (V* const& value) const {
        return *value;
      }
      template <class V>
      V&& operator() (V*&& value) const {
        return std::move (*value);
      }
  };

  // All observations and input preparation happen outside the injected budget.
  // Sweep until success, so every early and late allocation (including coloring
  // and both dominates caches) has thrown once. Retry with the SAME operands.
  template <class V>
  long sweep (int operation) {
    using D = posets::downsets::sharingtrie_backed<V>;
    using T = posets::utils::sharingtrie<V>;
    static_assert (not std::is_nothrow_constructible_v<D, std::vector<V>&&>);
    static_assert (std::is_nothrow_move_constructible_v<T>);
    static_assert (std::is_nothrow_move_assignable_v<T>);
    for (long fail = 0; fail < 4096; ++fail) {
      struct reset_budget {
          ~reset_budget () { allocations_left = -1; }
      } guard;
      auto subject = matrix_check::build<D> (original);
      auto other = matrix_check::build<D> (rhs);
      auto trie = T (values<V> (original));
      auto external = values<V> (rhs);
      const auto& replacement = operation == 4 ? shrunk : grown;
      auto input = values<V> (operation < 3 ? original : replacement);
      auto single = matrix_check::vector<V> (original.front ());
      std::vector<V*> aliases;
      for (auto& value : trie)
        aliases.push_back (&value);
      for (auto& value : external)
        aliases.push_back (&value);
      std::optional<D> constructed;
      std::optional<T> utility;
      check (subject, original);
      check (other, rhs);
      check (trie, original);  // warm the DFS and stamp caches
      auto run = [&] {
        switch (operation) {
          case 0: constructed.emplace (std::move (input)); break;
          case 1: constructed.emplace (std::move (single)); break;
          case 2: utility.emplace (std::move (input)); break;
          case 3:
          case 4: trie.relabel_trie (std::move (input)); break;
          case 5: trie.relabel_trie (aliases, move_projection {}); break;
          case 6: subject.union_with (std::move (other)); break;
          case 7: subject.intersect_with (other); break;
          case 8: trie.relabel_trie (std::move (trie.get_backing_vector ())); break;
          default: throw std::runtime_error ("unknown allocation operation");
        }
      };
      auto success = [&] {
        if (operation == 0)
          check (*constructed, original);
        if (operation == 1)
          check (*constructed, {original.front ()});
        if (operation == 2)
          check (*utility, original);
        if (operation == 3 or operation == 4)
          check (trie, replacement);
        if (operation == 5)
          check (trie, united);
        if (operation == 6)
          check (subject, united);
        if (operation == 7)
          check (subject, met);
        if (operation == 8)
          check (trie, original);
      };
      bool threw = false;
      try {
        allocations_left = fail;
        run ();
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (not threw) {
        require (fail > 5, "sweep missed late build/cache allocations");
        success ();
        allocations_left = 0;
        return fail;
      }
      check (subject, original);
      check (other, rhs);
      check (trie, original);
      if (operation == 5)
        for (size_t i = 0; i < external.size (); ++i)
          require (matrix_check::unpack (external[i]) == rhs[i], "aliased external input moved");
      if (operation >= 2 and operation <= 4) {
        // Utility construction/relabel also preserve caller-owned vectors.
        for (size_t i = 0; i < input.size (); ++i)
          require (matrix_check::unpack (input[i]) == (operation == 2 ? original : replacement)[i],
                   "utility input moved on failure");
      }
      // Downset reduction is allowed to consume its rvalue construction input.
      if (operation == 0)
        input = values<V> (original);
      if (operation == 1)
        single = matrix_check::vector<V> (original.front ());
      run ();
      success ();
      // Destroy the rebuilt result and source operands with allocation disabled.
      allocations_left = 0;
      constructed.reset ();
      utility.reset ();
      {
        T moved (std::move (trie));
        trie = std::move (moved);
      }
      auto* self = &trie;
      trie = std::move (*self);
      // The budget guard resets only after all fixtures have been destroyed.
    }
    throw std::runtime_error ("allocation sweep did not complete");
  }

  template <class V>
  void projections () {
    using T = posets::utils::sharingtrie<V>;
    auto trie = T (values<V> (original));
    std::vector<V*> aliases;
    for (auto& value : trie)
      aliases.push_back (&value);
    int left = aliases.size ();
    try {
      trie.relabel_trie (aliases, [&] (V* value) -> V&& {
        if (--left == 0)
          throw std::bad_alloc ();
        return std::move (*value);
      });
      require (false, "throwing projection was not reached");
    } catch (const std::bad_alloc&) {
      check (trie, original);
    }
    // Lvalue projections use the copy path, which also preserves aliased values
    // until a complete replacement (including caches) has been built.
    for (long fail = 0; fail < 4096; ++fail) {
      try {
        allocations_left = fail;
        trie.relabel_trie (aliases, [] (V* value) -> const V& { return *value; });
        allocations_left = -1;
        check (trie, original);
        return;
      } catch (const std::bad_alloc&) {
        allocations_left = -1;
        check (trie, original);
      }
    }
    throw std::runtime_error ("copy projection sweep did not complete");
  }

  // Vector permits throwing moves. Inject those failures too, to verify that
  // the copy fallback never moves a live generator before publishing state.
  class throwing_move : public posets::vectors::vector_backed<std::int8_t> {
      using base = posets::vectors::vector_backed<std::int8_t>;
      static base&& checked (throwing_move& value) {
        allocation_failure::charge ();
        return std::move (value);
      }
      explicit throwing_move (base&& value) : base (std::move (value)) {}

    public:
      using base::base;
      throwing_move (const throwing_move&) = delete;
      throwing_move (throwing_move&& value) : base (checked (value)) {}
      throwing_move& operator= (throwing_move&& value) {
        base::operator= (checked (value));
        return *this;
      }
      throwing_move copy () const { return throwing_move (base::copy ()); }
      throwing_move meet (const throwing_move& other) const {
        return throwing_move (base::meet (other));
      }
      throwing_move join (const throwing_move& other) const {
        return throwing_move (base::join (other));
      }
  };
  static_assert (posets::Vector<throwing_move>);
  static_assert (not std::is_nothrow_move_constructible_v<throwing_move>);

  template <class V>
  void test (const char* name) {
    projections<V> ();
    for (int operation = 0; operation < 9; ++operation)
      std::cout << name << " operation=" << operation << " injected_sites=" << sweep<V> (operation)
                << " PASS\n";
    // Empty algebra, source reuse and self-move/rebuild are allocation-free where
    // possible, and must leave destruction safe after a successful transaction.
    using D = posets::downsets::sharingtrie_backed<V>;
    using T = posets::utils::sharingtrie<V>;
    auto subject = matrix_check::build<D> (original);
    auto empty = matrix_check::build<D> ({});
    allocations_left = 0;
    subject.union_with (std::move (subject));
    subject.intersect_with (subject);
    subject.union_with (std::move (empty));
    subject.intersect_with (empty);
    allocations_left = -1;
    check (subject, {});
    subject.union_with (matrix_check::build<D> (original));
    check (subject, original);
    T source (values<V> (original));
    T destination (std::move (source));
    check (source, {});
    source.relabel_trie (values<V> (rhs));
    check (source, rhs);
    destination = std::move (source);
    check (destination, rhs);
    check (source, {});
  }
}

int main () {
  try {
    test<posets::vectors::vector_backed<std::int8_t>> ("scalar");
    test<posets::vectors::simd_vector_backed<std::int8_t>> ("SIMD dynamic");
    test<posets::vectors::simd_array_backed<std::int8_t, 129>> ("SIMD embedded");
    test<posets::vectors::simd_array_ptr_backed<std::int8_t, 129>> ("SIMD pointer");
    projections<throwing_move> ();
    std::cout << "throwing move aliased relabel injected_sites=" << sweep<throwing_move> (5)
              << " PASS\n";
    using block = posets::utils::simd_traits<std::int8_t>::fssimd;
    try {
      allocations_left = 0;
      auto array = std::make_unique<block[]> (3);
      require (false, "aligned array failure was not reached");
    } catch (const std::bad_alloc&) {
      allocations_left = -1;
    }
    auto array = std::make_unique<block[]> (3);
    require (reinterpret_cast<std::uintptr_t> (array.get ()) % alignof (block) == 0,
             "misaligned SIMD array");
    allocations_left = 0;
    array.reset ();
    allocations_left = -1;
    require (allocation_failure::aligned_failures > 0, "aligned failure was not injected");
    require (allocation_failure::aligned_attempts > 0, "aligned allocation injection not reached");
  } catch (const std::exception& error) {
    allocations_left = -1;
    std::cerr << error.what () << '\n';
    return 1;
  }
}
