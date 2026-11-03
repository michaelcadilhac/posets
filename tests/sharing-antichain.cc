#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <posets/downsets/sharingtree_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/vectors.hh>

namespace {
  using row = std::vector<std::int8_t>;
  using rows = std::vector<row>;

  void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }

  bool leq (const row& a, const row& b) {
    require (a.size () == b.size (), "scalar dimension mismatch");
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }

  rows maxima (rows input) {
    std::sort (input.begin (), input.end ());
    input.erase (std::unique (input.begin (), input.end ()), input.end ());
    rows result;
    for (size_t i = 0; i < input.size (); ++i) {
      bool dominated = false;
      for (size_t j = 0; j < input.size (); ++j)
        dominated or_eq i != j and leq (input[i], input[j]);
      if (not dominated)
        result.push_back (input[i]);
    }
    return result;
  }

  template <typename D>
  D make (const rows& input) {
    std::vector<typename D::value_type> vectors;
    for (const auto& value : input)
      vectors.emplace_back (row {value});
    return D {std::move (vectors)};
  }

  template <typename D>
  void check (const D& actual, const rows& expected, size_t dimension) {
    const auto wanted = maxima (expected);
    require (actual.size () == wanted.size (), "noncanonical size");
    rows observed;
    for (const auto& value : actual) {
      row scalar;
      for (size_t i = 0; i < value.size (); ++i)
        scalar.push_back (value[i]);
      observed.push_back (std::move (scalar));
    }
    std::sort (observed.begin (), observed.end ());
    require (observed == wanted, "generators are not exactly the unique maxima");
    require (actual.get_backing_vector ().size () == wanted.size (), "stale materialized cache");
    require (actual.size () == observed.size (), "stale size cache");

    // Independent logical-coordinate membership, including signed bottom.
    rows queries {row (dimension, -1), row (dimension, 0), row (dimension, 1)};
    queries.insert (queries.end (), expected.begin (), expected.end ());
    if (dimension == 3) {
      for (int a = -1; a <= 1; ++a)
        for (int b = -1; b <= 1; ++b)
          for (int c = -1; c <= 1; ++c)
            queries.push_back ({static_cast<std::int8_t> (a), static_cast<std::int8_t> (b),
                                static_cast<std::int8_t> (c)});
    }
    for (const auto& query : queries) {
      bool covered = false;
      for (const auto& value : expected)
        covered or_eq leq (query, value);
      require (actual.contains (typename D::value_type {row {query}}) == covered,
               "downset membership differs from scalar oracle");
    }
  }

  template <typename D>
  void operations (const rows& a, const rows& b, size_t dimension) {
    auto joined = make<D> (a);
    check (joined, a, dimension);  // populate both caches before mutation
    {
      auto other = make<D> (b);
      joined.union_with (std::move (other));
    }  // the result must own its generators after the other source is gone
    auto united = a;
    united.insert (united.end (), b.begin (), b.end ());
    check (joined, united, dimension);
    joined.union_with (std::move (joined));
    check (joined, united, dimension);

    auto intersected = make<D> (a);
    check (intersected, a, dimension);
    rows meets;
    {
      const auto other = make<D> (b);
      intersected.intersect_with (other);
      for (const auto& x : a)
        for (const auto& y : b) {
          row meet;
          for (size_t i = 0; i < dimension; ++i)
            meet.push_back (std::min (x[i], y[i]));
          meets.push_back (std::move (meet));
        }
    }
    check (intersected, meets, dimension);
    intersected.intersect_with (intersected);
    check (intersected, meets, dimension);
  }

  template <typename D>
  void positive_dimensions () {
    const auto empty = make<D> ({});
    bool called = false;
    const auto mapped_empty = empty.apply ([&] (const auto& value) {
      called = true;
      return value.copy ();
    });
    require (not called, "empty apply called its mapping");
    check (mapped_empty, {}, 3);
    const rows tiny {{1, 0, 1}, {0, 0, 1}, {0, 1, 0}};
    auto source = make<D> (tiny);
    check (source, tiny, 3);
    auto identity = source.apply ([] (const auto& value) { return value.copy (); });
    check (identity, tiny, 3);
    // Monotone projection creates strict dominance between formerly incomparable rows.
    auto projected = source.apply (
        [] (const auto& value) { return typename D::value_type {row {0, value[1], 0}}; });
    check (projected, {{0, 0, 0}, {0, 1, 0}}, 3);
    check (source, tiny, 3);

    for (size_t dimension : {1, 4, 15, 16, 17, 63, 64, 65, 91}) {
      row a (dimension, 0), b (dimension, 0);
      a.front () = 1;
      b.back () = 1;
      const rows input {a, b, row (dimension, 0), a, row (dimension, -1)};
      check (make<D> (input), input, dimension);
      operations<D> ({a}, {b, row (dimension, -1)}, dimension);
      operations<D> ({}, {a}, dimension);
      operations<D> ({a}, {}, dimension);
      operations<D> ({}, {}, dimension);
    }
    const rows four {{1, 1, 0, 1}, {1, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 1, 0}};
    check (make<D> (four), four, 4);

    // Every Boolean 3D generator subset, and every pair of its 20 antichains.
    std::vector<rows> antichains;
    for (unsigned mask = 0; mask < 256; ++mask) {
      rows input;
      for (unsigned value = 0; value < 8; ++value)
        if (mask & (1U << value))
          input.push_back ({static_cast<std::int8_t> (value & 1),
                            static_cast<std::int8_t> ((value >> 1) & 1),
                            static_cast<std::int8_t> ((value >> 2) & 1)});
      check (make<D> (input), input, 3);
      if (maxima (input).size () == input.size ())
        antichains.push_back (std::move (input));
    }
    require (antichains.size () == 20, "wrong scalar Boolean antichain count");
    for (const auto& a : antichains)
      for (const auto& b : antichains)
        operations<D> (a, b, 3);
  }

  template <typename V>
  void run () {
    positive_dimensions<posets::downsets::sharingtree_backed<V>> ();
    positive_dimensions<posets::downsets::sharingtrie_backed<V>> ();
    using tree = posets::downsets::sharingtree_backed<V>;
    check (make<tree> ({{}, {}}), {{}}, 0);
    operations<tree> ({{}}, {{}}, 0);
    operations<tree> ({}, {{}}, 0);
    operations<tree> ({{}}, {}, 0);
    // sharingtrie's existing nonempty zero-dimensional precondition is unchanged.
  }
}

int main () {
  const auto previous = std::cout.rdstate ();
  std::cout.setstate (std::ios::failbit);  // suppress narration, retain assertions
  try {
    run<posets::vectors::vector_backed<std::int8_t>> ();
    run<posets::vectors::simd_vector_backed<std::int8_t>> ();
    std::cout.clear (previous);
    std::cout << "canonical sharing backends: scalar + SIMD "
              << 8 * posets::utils::simd_traits<std::int8_t>::simd_size << " bits PASS\n";
  } catch (const std::exception& error) {
    std::cout.clear (previous);
    std::cerr << error.what () << '\n';
    return 1;
  }
}
