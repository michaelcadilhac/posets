#include "check.hh"

#include <cstdint>
#include <vector>

#include <posets/utils/sharingtrie.hh>
#include <posets/vectors.hh>

namespace utils = posets::utils;

using test_value_type = std::int8_t;
using VType = posets::vectors::vector_backed<test_value_type>;

std::vector<VType> vvtovv (const std::vector<std::vector<test_value_type>>& vv) {
  std::vector<VType> out;
  for (size_t i = 0; i < vv.size (); ++i)
    out.emplace_back (VType (std::move (vv[i])));
  return out;
}

int main (int argc, const char* argv[]) {
  std::vector<std::vector<test_value_type>> data {{6, 3, 2}, {5, 5, 4}, {2, 6, 2}};
  utils::sharingtrie<VType> f1 (std::move (vvtovv (data)));
  std::cout << f1 << std::endl;
  CHECK (f1.get_all ().size () == 3);
  {
    std::vector<test_value_type> v = {5, 2, 1};
    VType vec (std::move (v));
    CHECK (f1.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {6, 3, 2};
    VType vec (std::move (v));
    CHECK (not f1.dominates (vec, true));
  }
  {
    std::vector<test_value_type> v = {6, 3, 2};
    VType vec (std::move (v));
    CHECK (f1.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {7, 7, 7};
    VType vec (std::move (v));
    CHECK (not f1.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {1, 6, 2};
    VType vec (std::move (v));
    CHECK (f1.dominates (vec));
  }


  data = {{7, 4, 3}, {4, 8, 4}, {2, 5, 6}, {1, 9, 9}};
  utils::sharingtrie<VType> f2 (std::move (vvtovv (data)));
  std::cout << f2 << std::endl;
  CHECK (f2.get_all ().size () == 4);
  {
    std::vector<test_value_type> v = {1, 6, 2};
    VType vec (std::move (v));
    CHECK (f2.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {7, 7, 7};
    VType vec (std::move (v));
    CHECK (not f2.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {2, 5, 6};
    VType vec (std::move (v));
    CHECK (f2.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {2, 5, 6};
    VType vec (std::move (v));
    CHECK (not f2.dominates (vec, true));
  }

  data = {{3, 2, 2, 2}, {4, 1, 2, 1}, {5, 0, 2, 1}};
  utils::sharingtrie<VType> f3 (std::move (vvtovv (data)));
  std::cout << f3 << std::endl;
  CHECK (f3.get_all ().size () == 3);
  {
    std::vector<test_value_type> v = {1, 2, 2, 1};
    VType vec (std::move (v));
    CHECK (f3.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {7, 7, 7, 0};
    VType vec (std::move (v));
    CHECK (not f3.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {4, 1, 2, 1};
    VType vec (std::move (v));
    CHECK (f3.dominates (vec));
  }
  {
    std::vector<test_value_type> v = {4, 1, 2, 1};
    VType vec (std::move (v));
    CHECK (not f3.dominates (vec, true));
  }

  data = {{-1, 0}, {-1, 1}, {-1, 0}, {-1, 1}, {-1, 0}, {0, -1}};
  utils::sharingtrie<VType> f4 (std::move (vvtovv (data)));
  std::cout << f4 << std::endl;
  {
    std::vector<test_value_type> v = {-1, 0};
    VType vec (std::move (v));
    CHECK (f4.dominates (vec));
    CHECK (f4.dominates (vec, true));
  }
  {
    std::vector<test_value_type> v = {-1, 1};
    VType vec (std::move (v));
    CHECK (f4.dominates (vec));
    CHECK (not f4.dominates (vec, true));
  }
  {
    std::vector<test_value_type> v = {0, -1};
    VType vec (std::move (v));
    CHECK (f4.dominates (vec));
    CHECK (not f4.dominates (vec, true));
  }

  return 0;
}
