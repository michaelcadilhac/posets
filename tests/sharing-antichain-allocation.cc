#include <type_traits>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

#include <posets/downsets/sharingtree_backed.hh>
#include <posets/vectors.hh>

namespace {
  long allocations_left = -1;
}

void* operator new (std::size_t bytes) {
  if (allocations_left == 0)
    throw std::bad_alloc ();
  if (allocations_left > 0)
    --allocations_left;
  if (void* memory = std::malloc (bytes ? bytes : 1))
    return memory;
  throw std::bad_alloc ();
}

void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void* operator new (std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new (bytes);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[] (std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new[] (bytes);
  } catch (...) {
    return nullptr;
  }
}
void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete (void* memory, const std::nothrow_t&) noexcept { ::operator delete (memory); }
void operator delete[] (void* memory, const std::nothrow_t&) noexcept {
  ::operator delete[] (memory);
}

namespace {
  using row = std::vector<std::int8_t>;
  using rows = std::vector<row>;
  using V = posets::vectors::vector_backed<std::int8_t>;
  using D = posets::downsets::sharingtree_backed<V>;
  static_assert (not std::is_nothrow_constructible_v<D, std::vector<V>&&>);

  D make (const rows& input) {
    std::vector<V> elements;
    for (const auto& value : input)
      elements.emplace_back (row {value});
    return D {std::move (elements)};
  }

  void check (const D& actual, rows expected) {
    std::sort (expected.begin (), expected.end ());
    rows observed;
    for (const auto& value : actual) {
      row scalar;
      for (size_t i = 0; i < value.size (); ++i)
        scalar.push_back (value[i]);
      observed.push_back (std::move (scalar));
    }
    std::sort (observed.begin (), observed.end ());
    if (observed != expected or actual.size () != expected.size () or
        actual.get_backing_vector ().size () != expected.size ())
      throw std::runtime_error ("allocation failure changed canonical generators/caches");
    for (const auto& query : expected)
      if (not actual.contains (V {row {query}}))
        throw std::runtime_error ("allocation failure changed membership");
    if (actual.contains (V {row {3, 3, 3}}))
      throw std::runtime_error ("allocation failure introduced membership");
  }

  long failures (int operation) {
    const rows original {{1, 0, 1}, {0, 1, 0}};
    const rows rhs {{2, 2, 2}};
    for (long fail = 0; fail < 512; ++fail) {
      auto subject = make (original);
      auto other = make (rhs);
      check (subject, original);  // populate materialized, size and membership caches
      try {
        allocations_left = fail;
        if (operation == 0)
          auto created = make ({{2, 0, 0}, {0, 0, 2}});
        else if (operation == 1)
          subject.union_with (std::move (other));
        else
          subject.intersect_with (other);
        allocations_left = -1;
        if (fail == 0)
          throw std::runtime_error ("allocation injection was not reached");
        return fail;  // every earlier scalar allocation site was injected once
      } catch (const std::bad_alloc&) {
        allocations_left = -1;
        check (subject, original);
        // Shared forest state must also remain usable after a caught failure.
        if (operation == 0) {
          auto created = make ({{2, 0, 0}, {0, 0, 2}});
          check (created, {{2, 0, 0}, {0, 0, 2}});
        }
        else if (operation == 1) {
          subject.union_with (std::move (other));
          check (subject, rhs);
        }
        else {
          subject.intersect_with (other);
          check (subject, original);
        }
      }
    }
    throw std::runtime_error ("allocation sweep did not reach successful completion");
  }
}

int main () {
  const auto previous = std::cout.rdstate ();
  std::cout.setstate (std::ios::failbit);  // retain assertions without narration
  try {
    std::array<long, 3> sites {};
    for (int operation = 0; operation < 3; ++operation)
      sites[operation] = failures (operation);
    std::cout.clear (previous);
    for (int operation = 0; operation < 3; ++operation)
      std::cout << "scalar operation=" << operation << " injected_sites=" << sites[operation]
                << " PASS\n";
  } catch (const std::exception& error) {
    allocations_left = -1;
    std::cout.clear (previous);
    std::cerr << error.what () << '\n';
    return 1;
  }
}
