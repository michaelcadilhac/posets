#include "allocation-failure.hh"
#include "matrix-check.hh"

#include <iostream>
#include <sstream>

#include <posets/downsets/vector_or_kdtree_backed.hh>
#include <posets/vectors.hh>

namespace {
  using namespace matrix_check;
  using base = posets::vectors::vector_backed<int>;
  struct injected_failure {};
  class value : public base {
      explicit value (base&& v) : base (std::move (v)) {}

    public:
      using base::base;
      static inline bool fail_meet = false;
      value copy () const { return value (base::copy ()); }
      value join (const value& v) const { return value (base::join (v)); }
      value meet (const value& v) const {
        if (fail_meet)
          throw injected_failure {};
        return value (base::meet (v));
      }
  };
  using set = posets::downsets::vector_or_kdtree_backed<value>;
  const region left {{0, 8}, {2, 6}, {4, 4}, {6, 2}};
  const region right {{1, 7}, {3, 5}, {5, 3}, {7, 1}};

  set make (region rows, bool tree) {
    // Construction chooses storage before reduction; duplicates force KD storage.
    if (tree)
      while (rows.size () < 5)
        rows.push_back (rows.front ());
    return build<set> (rows);
  }
  bool is_tree (const set& subject) {
    std::ostringstream out;
    out << subject;
    // KD streaming has two extra newlines: observe kind without a test-only API.
    return out.str ().ends_with ("\n\n\n");
  }
  region expected (bool inter, const region& a, const region& b) {
    region result = inter ? region {} : a;
    if (inter) {
      for (const auto& x : a)
        for (const auto& y : b)
          result.push_back ({std::min (x[0], y[0]), std::min (x[1], y[1])});
    }
    else
      result.insert (result.end (), b.begin (), b.end ());
    return result;
  }
  void combine (set& a, set&& b, bool inter) {
    if (inter)
      a.intersect_with (std::move (b));
    else
      a.union_with (std::move (b));
  }
  void algebra () {
    region probes;
    for (int x = -1; x <= 9; ++x)
      for (int y = -1; y <= 9; ++y)
        probes.push_back ({x, y});
    for (bool a_tree : {false, true})
      for (bool b_tree : {false, true})
        for (bool inter : {false, true}) {
          auto a = make (left, a_tree);
          auto b = make (right, b_tree);
          require (is_tree (a) == a_tree and is_tree (b) == b_tree, "operand kinds");
          combine (a, std::move (b), inter);
          check (a, expected (inter, left, right), probes);
          require (is_tree (a), "result must upgrade above 2*dimension");
          a = make (left, a_tree);
          b = make (right, b_tree);
          value::fail_meet = inter;
          bool threw = false;
          try {
            combine (a, std::move (b), inter);
          } catch (const injected_failure&) {
            threw = true;
          }
          value::fail_meet = false;
          require (threw == inter, "meet exception not reached");
          if (inter) {
            require (threw and is_tree (a) == (a_tree and b_tree), "meet exception dispatch");
            check (a, left, probes);
            if (not b_tree or a_tree)
              check (b, right, probes);
          }
        }
  }
  void transitions_and_moves () {
    for (bool tree : {false, true}) {
      auto a = make (left, tree);
      auto* self = &a;
      a.union_with (std::move (*self));
      a.intersect_with (std::move (*self));
      a = std::move (*self);
      require (is_tree (a) == tree, "self operations changed storage");
      auto moved = std::move (a);
      a = make (right, not tree);
      a = std::move (moved);
      check (a, left, right);
      auto mapped = a.apply ([] (const value& v) { return v.copy (); });
      check (mapped, left, right);
      a.intersect_with (make ({{-1, -1}}, false));
      check (a, {{-1, -1}}, left);
      require (not is_tree (a), "mixed intersection must demote");
      require (is_tree (make ({{0, 0}}, tree)) == tree, "pre-reduction threshold");
    }
    for (int n : {2, 3})
      for (bool reverse : {false, true})
        for (bool inter : {false, true}) {
          region rows (right.begin (), right.begin () + n);
          auto a = make (reverse ? rows : region {{0, 8}}, true);
          auto b = make (reverse ? region {{0, 8}} : rows, true);
          combine (a, std::move (b), inter);
          check (a, expected (inter, {{0, 8}}, rows), left);
          require (is_tree (a) == (n == 2), "subexponential KD dispatch boundary");
        }
  }
  void allocation_boundaries () {
    for (bool b_tree : {false, true}) {
      auto a = make ({{0, 8}}, true);
      auto b = make (right, b_tree);
      bool threw = false;
      try {
        allocations_left = 0;
        a.intersect_with (std::move (b));
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      allocations_left = -1;
      require (threw and is_tree (a), "failed LHS conversion published");
      check (a, {{0, 8}}, left);
      check (b, right, left);
    }
    auto a = make (left, false);
    auto b = make ({{8, 0}}, false);
    a.get_backing_vector ().reserve (8);
    bool threw = false;
    try {
      allocations_left = 0;
      a.union_with (std::move (b));
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    allocations_left = -1;
    require (threw and not is_tree (a), "failed promotion changed backing kind");
    check (a, expected (false, left, {{8, 0}}), right);
  }
}
int main () try {
  algebra ();
  transitions_and_moves ();
  allocation_boundaries ();
  std::cout << "hybrid operand kinds, thresholds, self/move and exception boundaries PASS\n";
} catch (const std::exception& error) {
  allocations_left = -1;
  std::cerr << error.what () << '\n';
  return 1;
}
