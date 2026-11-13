#include "check.hh"

// Low-level tests for posets::utils::cst (covering sharing tree, after
// Piipponen & Valmari, "Constructing Minimal Coverability Sets",
// Fundam. Inform. 143 (2016) 393-414).  Mirrors sttests.cc so that the
// CST utility can be benchmarked apples-to-apples against sharingforest.

#include <cstdint>
#include <vector>

#include <posets/utils/cst.hh>
#include <posets/vectors.hh>

namespace utils = posets::utils;

using test_value_type = std::int8_t;
using VType = posets::vectors::vector_backed<test_value_type>;

static std::vector<VType> vvtovv (const std::vector<std::vector<test_value_type>>& vv) {
  std::vector<VType> out;
  out.reserve (vv.size ());
  for (size_t i = 0; i < vv.size (); ++i)
    out.emplace_back (VType (std::move (vv[i])));
  return out;
}

int main () {
  // ----- Single-tree covers tests on a 3-dim CST -----------------------
  utils::cst<VType> f {3};
  std::vector<std::vector<test_value_type>> data {{6, 3, 2}, {5, 5, 4}, {2, 6, 2}};
  auto idcs = f.add_vectors (vvtovv (data));

  std::vector<test_value_type> v = {2, 2, 2};
  CHECK (f.covers_vector (idcs, VType (std::move (v))));
  v = {6, 3, 1};
  CHECK (f.covers_vector (idcs, VType (std::move (v))));
  v = {6, 3, 2};
  CHECK (f.covers_vector (idcs, VType (std::move (v))));
  // strict variant: equal element is not strictly covered
  v = {6, 3, 2};
  CHECK (not f.covers_vector (idcs, VType (std::move (v)), true));
  v = {8, 3, 1};
  CHECK (not f.covers_vector (idcs, VType (std::move (v))));
  v = {9, 4, 8};
  CHECK (not f.covers_vector (idcs, VType (std::move (v))));
  // every component strictly below a stored vector
  v = {1, 2, 1};
  CHECK (f.covers_vector (idcs, VType (std::move (v)), true));

  // ----- A 10-dim long-vector test, exercising the prune heuristics ----
  utils::cst<VType> f10 {10};
  auto longidcs = f10.add_vectors (vvtovv ({{2, 4, 1, 8, 7, 4, 1, 10, 2, 8}}));
  v = {2, 4, 1, 8, 5, 4, 1, 8, 1, 9};  // 9 in last slot dominates
  CHECK (not f10.covers_vector (longidcs, VType (std::move (v))));
  v = {2, 4, 1, 6, 7, 4, 1, 10, 0, 4};  // strictly below
  CHECK (f10.covers_vector (longidcs, VType (std::move (v))));
  v = {2, 4, 1, 6, 7, 4, 1, 10, 0, 4};
  CHECK (f10.covers_vector (longidcs, VType (std::move (v)), true));

  // ----- Add a second tree to the same forest --------------------------
  auto idcs2 = f.add_vectors (vvtovv ({{7, 4, 3}, {4, 8, 4}, {2, 5, 6}}));
  v = {2, 2, 2};
  CHECK (f.covers_vector (idcs2, VType (std::move (v))));
  v = {7, 3, 1};
  CHECK (f.covers_vector (idcs2, VType (std::move (v))));
  v = {8, 3, 1};
  CHECK (not f.covers_vector (idcs2, VType (std::move (v))));
  v = {8, 5, 3};
  CHECK (not f.covers_vector (idcs2, VType (std::move (v))));

  // The original tree must still answer correctly after a second insert
  v = {2, 2, 2};
  CHECK (f.covers_vector (idcs, VType (std::move (v))));
  v = {6, 3, 1};
  CHECK (f.covers_vector (idcs, VType (std::move (v))));
  v = {8, 3, 1};
  CHECK (not f.covers_vector (idcs, VType (std::move (v))));

  // ----- Shared-prefix tree: forces the heuristics to trigger ----------
  auto idcs3 = f.add_vectors (vvtovv ({{3, 2, 2}, {3, 4, 1}, {3, 2, 3}, {3, 4, 0}}));
  v = {3, 2, 3};
  CHECK (f.covers_vector (idcs3, VType (std::move (v))));
  v = {3, 2, 4};
  CHECK (not f.covers_vector (idcs3, VType (std::move (v))));
  v = {3, 4, 1};
  CHECK (f.covers_vector (idcs3, VType (std::move (v))));
  v = {3, 4, 2};
  CHECK (not f.covers_vector (idcs3, VType (std::move (v))));

  // Sibling lists at every layer must respect the (w, m) order
  CHECK (f.check_layer_order ());
  CHECK (f10.check_layer_order ());

  // ----- 4-dim shared-suffix tree --------------------------------------
  utils::cst<VType> f4 {4};
  auto fourdim =
      f4.add_vectors (vvtovv ({{3, 2, 2, 1}, {4, 1, 2, 1}, {5, 0, 2, 1}}));
  v = {3, 0, 2, 2};
  CHECK (not f4.covers_vector (fourdim, VType (std::move (v))));
  v = {1, 0, 1, 1};
  CHECK (f4.covers_vector (fourdim, VType (std::move (v))));

  // ----- Union: result covers everything from either operand -----------
  auto uRoot = f.st_union (idcs, idcs2);
  v = {7, 4, 1};
  CHECK (f.covers_vector (uRoot, VType (std::move (v))));
  v = {5, 5, 3};
  CHECK (f.covers_vector (uRoot, VType (std::move (v))));
  v = {8, 3, 1};
  CHECK (not f.covers_vector (uRoot, VType (std::move (v))));
  // Anything covered by either tree is covered by the union
  v = {2, 6, 2};
  CHECK (f.covers_vector (uRoot, VType (std::move (v))));
  v = {2, 5, 6};
  CHECK (f.covers_vector (uRoot, VType (std::move (v))));

  // ----- Intersection: only meets are covered --------------------------
  auto iRoot = f.st_intersect (idcs, idcs2);
  v = {7, 4, 1};
  CHECK (not f.covers_vector (iRoot, VType (std::move (v))));
  v = {5, 5, 3};
  CHECK (not f.covers_vector (iRoot, VType (std::move (v))));
  v = {3, 5, 4};  // meet of (5,5,4) and (4,8,4) and (2,6,2) and (7,4,3)? at least (5,5,4)^(4,8,4) = (4,5,4)
  CHECK (f.covers_vector (iRoot, VType (std::move (v))));

  // get_all should round-trip the antichain (size = number of distinct maximals)
  auto all = f.get_all (idcs);
  CHECK (all.size () == 3);

  return 0;
}
