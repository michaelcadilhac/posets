#pragma once

// Independent logical oracle: no Posets partial-order, padding or pruning code.
#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace matrix_check {
  using logical = std::vector<int>;
  using region = std::vector<logical>;

  inline void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }
  inline bool below (const logical& a, const logical& b) {
    require (a.size () == b.size (), "oracle dimension mismatch");
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }
  inline std::set<logical> maxima (const region& values) {
    std::set<logical> result;
    for (const auto& v : values)
      if (std::ranges::none_of (values, [&] (const auto& w) { return v != w and below (v, w); }))
        result.insert (v);
    return result;
  }
  template <class V>
  V vector (const logical& values) {
    std::vector<typename V::value_type> raw (values.begin (), values.end ());
    return V (std::span<const typename V::value_type> (raw));
  }
  template <class S>
  S build (const region& values) {
    std::vector<typename S::value_type> raw;
    for (const auto& value : values)
      raw.push_back (vector<typename S::value_type> (value));
    return S (std::move (raw));
  }
  template <class V>
  logical unpack (const V& value) {
    logical result;
    for (size_t i = 0; i < value.size (); ++i)
      result.push_back (int (value[i]));
    return result;
  }
  template <class S>
  void check (const S& actual, const region& expected, const region& queries) {
    const auto wanted = maxima (expected);
    std::set<logical> seen;
    for (const auto& value : actual)
      require (seen.insert (unpack (value)).second, "duplicate maximum");
    require (seen == wanted and actual.size () == wanted.size (), "incorrect maximal generators");
    for (const auto& query : queries) {
      const bool answer =
          std::ranges::any_of (expected, [&] (const auto& v) { return below (query, v); });
      require (actual.contains (vector<typename S::value_type> (query)) == answer,
               "incorrect membership");
    }
  }
  template <class V>
  void pair (const logical& raw, const logical& other) {
    auto a = vector<V> (raw);
    auto b = vector<V> (other);
    require (a.partial_order (b).leq () == below (raw, other), "signed/padding order");
    require (a.partial_order (b).geq () == below (other, raw), "signed/padding reverse order");
    logical meet;
    logical join;
    for (size_t i = 0; i < raw.size (); ++i) {
      meet.push_back (std::min (raw[i], other[i]));
      join.push_back (std::max (raw[i], other[i]));
    }
    const auto met = a.meet (b);
    const auto joined = a.join (b);
    require (unpack (met) == meet and met == vector<V> (meet), "signed/padded min");
    require (unpack (joined) == join and joined == vector<V> (join), "signed/padded max");
    auto copy = a.copy ();
    const logical bottom (raw.size (), -1);
    copy.meet_with (vector<V> (bottom));
    require (unpack (a) == raw and a == vector<V> (raw), "copy aliases source/padding");
    auto moved = std::move (copy);
    V* alias = &moved;
    moved = std::move (*alias);
    require (unpack (moved) == bottom and moved == vector<V> (bottom),
             "self move lost coordinates");
    a = std::move (b);
    require (unpack (a) == other and a == vector<V> (other), "move assignment/padding");
  }
  template <class V>
  void vectors () {
    for (size_t dim : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 129u}) {
      for (int value : {-1, 0, 1, 99}) {
        logical raw (dim, value);
        logical other (dim, value);
        if (dim)
          other.back () = value == -1 ? 0 : -1;
        pair<V> (raw, other);
      }
    }
  }
  template <class S>
  void run (const std::string& which) {
    using V = typename S::value_type;
    if (which == "vectors") {
      vectors<V> ();
      return;
    }
    if (which == "empty") {
      auto empty = build<S> ({});
      const region query {{-1, -1}, {0, 0}};
      check (empty, {}, query);
      auto mapped = empty.apply ([] (const auto& v) { return v.copy (); });
      check (mapped, {}, query);
      empty.union_with (build<S> ({}));
      check (empty, {}, query);
      empty.union_with (build<S> ({{0, 0}}));
      check (empty, {{0, 0}}, query);
      empty.intersect_with (build<S> ({}));
      check (empty, {}, query);
      auto nonempty = build<S> ({{0, 0}});
      nonempty.union_with (build<S> ({}));
      check (nonempty, {{0, 0}}, query);
      empty.intersect_with (build<S> ({{0, 0}}));
      check (empty, {}, query);
      return;
    }
    if (which == "zero") {
      auto zero = build<S> ({{}});
      check (zero, {{}}, {{}});
      auto mapped = zero.apply ([] (const auto& v) { return v.copy (); });
      check (mapped, {{}}, {{}});
      zero.union_with (build<S> ({{}}));
      zero.intersect_with (build<S> ({{}}));
      check (zero, {{}}, {{}});
      return;
    }
    require (which == "nonempty", "unknown semantic case");
    for (size_t dim : {2u, 3u, 17u, 33u, 65u, 129u}) {
      region left (5, logical (dim, -1));
      region right (3, logical (dim, -1));
      for (size_t i = 0; i < dim; ++i) {
        const bool boolean = i >= dim / 2;
        for (size_t j = 0; j < left.size (); ++j)
          left[j][i] = boolean ? int ((i + j) % 2) - 1 : int ((i + j) % 4) - 1;
        for (size_t j = 0; j < right.size (); ++j)
          right[j][i] = boolean ? int ((i + j + 1) % 2) - 1 : int ((i + 2 * j) % 4) - 1;
      }
      left.back () = left.front ();  // Duplicate plus sharing/repeated-coordinate patterns.
      left.push_back (logical (dim, -1));
      region queries = left;
      queries.insert (queries.end (), right.begin (), right.end ());
      queries.push_back (logical (dim, -1));
      logical high (dim, 99);
      std::fill (high.begin () + dim / 2, high.end (), 0);
      queries.push_back (high);
      if (dim <= 3) {
        for (size_t code = 0; code < (size_t {1} << (2 * dim)); ++code) {
          logical query;
          for (size_t i = 0; i < dim; ++i)
            query.push_back (i >= dim / 2 ? int ((code >> (2 * i)) & 1) - 1
                                          : int ((code >> (2 * i)) & 3) - 1);
          queries.push_back (std::move (query));
        }
      }
      auto a = build<S> (left);
      check (a, left, queries);
      auto owned = a.apply ([] (const auto& v) { return v.copy (); });
      {
        auto temporary = build<S> (right);
        a = std::move (temporary);
      }
      check (owned, left, queries);
      check (a, right, queries);
      a = build<S> (left);
      a.union_with (build<S> (right));
      region united = left;
      united.insert (united.end (), right.begin (), right.end ());
      check (a, united, queries);
      auto b = build<S> (left);
      b.intersect_with (build<S> (right));
      region intersection;
      for (const auto& x : left)
        for (const auto& y : right) {
          logical value;
          for (size_t i = 0; i < dim; ++i)
            value.push_back (std::min (x[i], y[i]));
          intersection.push_back (std::move (value));
        }
      check (b, intersection, queries);
      auto c = owned.apply ([] (const auto& value) {
        logical lowered = unpack (value);
        for (auto& x : lowered)
          x = std::max (-1, x - 1);
        return vector<V> (lowered);
      });
      region lowered = left;
      for (auto& value : lowered)
        for (auto& x : value)
          x = std::max (-1, x - 1);
      check (c, lowered, queries);
    }
  }
}
