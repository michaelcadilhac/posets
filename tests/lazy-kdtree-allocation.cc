#include "allocation-failure.hh"
#include "matrix-check.hh"
#include <type_traits>

#include <iostream>
#include <optional>
#include <set>

#include <posets/downsets/lazy_kdtree_backed.hh>
#include <posets/vectors.hh>

namespace {
  using namespace matrix_check;
  region original () {
    region result;
    for (int i = 0; i < 12; ++i)
      result.push_back ({i, 11 - i, -1});
    return result;
  }
  const region rhs {{5, 8, 0}, {8, 5, 0}};
  template <class S>
  std::set<logical> canonical (const S& set) {
    std::set<logical> result;
    for (const auto& v : set)
      result.insert (unpack (v));
    return result;
  }
  template <class V, bool Cascade>
  long construction_sweep (int mode) {
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    auto expected = original ();
    expected.insert (expected.end (), rhs.begin (), rhs.end ());
    expected.push_back (expected.front ());
    expected.push_back ({-1, -1, -1});
    if (mode == 3 or mode == 4)
      expected = {{-1, -1, -1}, {0, 0, -1}, {1, 1, 0}, {1, 1, 0}, {2, 2, 0}};
    else if (mode == 5)
      expected = region (8, logical {1, 1, -1});
    else if (mode == 6)
      expected = {{0, 0, -1}, {1, 1, -1}, {1, 1, -1}, {2, 2, 0}, {3, 0, 0}, {4, 4, 0}};
    posets::downsets::lazy_kdtree_policy policy;
    policy.leaf_size = 2;
    policy.min_index_size = mode == 0 ? 1000 : 3;
    if (mode == 4)
      policy.min_index_size = 1;
    policy.min_buffer = 4;
    policy.bulk_scan_limit = mode == 2 ? 0 : 4096;
    for (long fail = 0; fail < 2048; ++fail) {
      struct reset {
          ~reset () { allocations_left = -1; }
      } guard;
      std::vector<V> input;
      const auto restore_input = [&] {
        input.clear ();
        for (const auto& row : expected)
          input.push_back (vector<V> (row));
      };
      restore_input ();
      std::optional<S> result;
      bool threw = false;
      try {
        allocations_left = fail;
        result.emplace (std::move (input), policy);
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      require (input.size () == expected.size (), "constructor stole the input container");
      if (threw) {
        if (mode != 2 or
            not(std::is_nothrow_move_constructible_v<V> and std::is_nothrow_move_assignable_v<V>))
          for (size_t i = 0; i < input.size (); ++i)
            require (unpack (input[i]) == expected[i], "bulk failure consumed constructor input");
        // The indexed fallback retains its earlier-input consumption contract.
        restore_input ();
        result.emplace (std::move (input), policy);
      }
      check (*result, expected, expected);
      if (mode != 2)
        require (result->storage ().created_ids == result->size () and
                     result->storage ().compactions == 1,
                 "bulk constructor retained intermediate epochs");
      if (not threw)
        return fail;
    }
    throw std::runtime_error ("lazy constructor allocation sweep did not finish");
  }
  template <class V, bool Cascade>
  long sweep (int operation) {
    using S = posets::downsets::lazy_kdtree_backed<V, Cascade>;
    posets::downsets::lazy_kdtree_policy policy;
    policy.leaf_size = 2;
    policy.min_index_size = 3;
    policy.min_buffer = 4;
    for (long fail = 0; fail < 2048; ++fail) {
      struct reset {
          ~reset () { allocations_left = -1; }
      } guard;
      auto initial = original ();
      std::vector<V> values;
      for (const auto& v : initial)
        values.push_back (vector<V> (v));
      S subject (std::move (values), policy);
      subject.rebuild ();
      auto other = build<S> (rhs);
      const logical new_raw = operation == 1 ? logical {20, 20, 20} : logical {-1, -1, 20};
      auto candidate = vector<V> (new_raw);
      std::optional<S> result;
      const auto run = [&] {
        switch (operation) {
          case 0: result.emplace (subject.apply ([] (const V& v) { return v.copy (); })); break;
          case 1:
          case 2: subject.insert (std::move (candidate)); break;
          case 3: subject.rebuild (); break;
          case 4: subject.intersect_with (other); break;
          case 5: subject.union_with (std::move (other)); break;
          default: throw std::runtime_error ("unknown operation");
        }
      };
      bool threw = false;
      try {
        allocations_left = fail;
        run ();
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      if (threw) {
        require (canonical (other) == maxima (rhs), "failure consumed RHS");
        if (operation != 5) {
          check (subject, initial, initial);
          require (unpack (candidate) == new_raw, "failure moved candidate");
        }
        else {
          region prefix = initial;
          bool allowed = canonical (subject) == maxima (prefix);
          for (const auto& v : other) {
            prefix.push_back (unpack (v));
            allowed or_eq canonical (subject) == maxima (prefix);
          }
          require (allowed, "basic union left an invalid partial result");
        }
        run ();  // Retry the same valid operands, including partial union.
      }
      region expected = initial;
      if (operation == 1 or operation == 2)
        expected.push_back (new_raw);
      if (operation == 4) {
        expected.clear ();
        for (const auto& a : initial)
          for (const auto& b : rhs)
            expected.push_back (
                {std::min (a[0], b[0]), std::min (a[1], b[1]), std::min (a[2], b[2])});
      }
      if (operation == 5)
        expected.insert (expected.end (), rhs.begin (), rhs.end ());
      check (subject, expected, expected);
      if (operation == 0)
        check (*result, initial, initial);
      if (operation == 5)
        require (other.empty (), "successful union left stale RHS index");
      // Generic Vector operations may themselves throw; ordinary vectors must
      // need no post-build query allocation, including through FC and the buffer.
      if constexpr (std::is_nothrow_move_constructible_v<V>) {
        const auto query = vector<V> (expected.front ());
        allocations_left = 0;
        require (subject.contains (query), "query result");
        (void) subject.dominates (query, true);
        allocations_left = -1;
      }
      allocations_left = 0;
      {
        S moved (std::move (subject));
        auto* self = &moved;
        moved = std::move (*self);
        subject = std::move (moved);
      }
      allocations_left = -1;
      if (not threw)
        return fail;
    }
    throw std::runtime_error ("lazy allocation sweep did not finish");
  }
  class throwing_move : public posets::vectors::vector_backed<int8_t> {
      using base = posets::vectors::vector_backed<int8_t>;
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
      auto partial_order (const throwing_move& other) const {
        allocation_failure::charge ();
        return base::partial_order (other);
      }
      auto operator[] (size_t coordinate) const {
        allocation_failure::charge ();
        return base::operator[] (coordinate);
      }
      bool operator< (const throwing_move&) const {
        throw std::logic_error ("Vector total order must not be used by lazy construction");
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
  template <class V, bool Cascade>
  void test () {
    for (int mode = 0; mode < 7; ++mode)
      std::cout << "cascade=" << Cascade << " construction_mode=" << mode
                << " injected_sites=" << construction_sweep<V, Cascade> (mode) << " PASS\n";
    for (int operation = 0; operation < 6; ++operation)
      std::cout << "cascade=" << Cascade << " operation=" << operation
                << " injected_sites=" << sweep<V, Cascade> (operation) << " PASS\n";
  }
}
int main () try {
  test<posets::vectors::vector_backed<int8_t>, false> ();
  test<posets::vectors::vector_backed<int8_t>, true> ();
  test<posets::vectors::simd_vector_backed<int8_t>, true> ();
  test<throwing_move, false> ();
  test<throwing_move, true> ();
} catch (const std::exception& error) {
  allocations_left = -1;
  std::cerr << error.what () << '\n';
  return 1;
}
