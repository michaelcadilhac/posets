#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#include <posets/concepts.hh>
#include <posets/vectors/traits.hh>

namespace posets::downsets {
  template <Vector V>
  class vector_backed_bin {
    public:
      using value_type = V;

      vector_backed_bin (V&& v) {
        bins.resize (v.size ());
        insert (std::move (v));
      }

      vector_backed_bin (std::vector<V>&& elements) noexcept {
        assert (not elements.empty ());
        bins.resize (elements[0].size ());
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      vector_backed_bin (size_t starting_bins_size) { bins.resize (starting_bins_size); }

    public:
      vector_backed_bin (const vector_backed_bin&) = delete;
      vector_backed_bin (vector_backed_bin&&) = default;
      vector_backed_bin& operator= (vector_backed_bin&&) = default;
      vector_backed_bin& operator= (const vector_backed_bin&) = delete;

      bool operator== (const vector_backed_bin& other) = delete;

      [[nodiscard]] bool contains (const V& v) const {
        const size_t bin = bin_of (v);

        if (bin >= bins.size ())
          return false;
        for (auto it = bins.begin () + bin; it != bins.end (); ++it)
          for (const auto& e : *it)
            if (v.partial_order (*e).leq ())
              return true;
        return false;
      }

      [[nodiscard]] auto size () const { return all_vs.size (); }

      bool insert (V&& v, bool antichain = true) {
        const size_t bin = bin_of (v);

        if (antichain) {
          auto start = std::min (bin, bins.size () - 1);
          [[maybe_unused]] bool must_remove = false;

          size_t i = start;
          do {
            // This is like remove_if, but allows breaking.
            auto result = bins[i].begin ();
            auto end = bins[i].end ();

            for (auto it = result; it != end; ++it) {
              auto res = v.partial_order (**it);
              if (not must_remove and res.leq ()) {  // v is dominated.
                // if must_remove is true, since we started with an antichain,
                // it's not possible that res.leq () holds.  Hence we don't check for
                // leq if must_remove is true.
                return false;
              }
              if (res.geq ()) {     // v dominates *it
                must_remove = true; /* *it should be removed */
                all_vs.erase (*it);
                // do not increase result (so that it will be erased later).
              }
              else {               // *it needs to be kept
                if (result != it)  // This can be false only on the first element.
                  *result = std::move (*it);
                ++result;
              }
            }

            if (result != bins[i].end ())
              bins[i].erase (result, bins[i].end ());

            i = (i + 1) % bins.size ();
          } while (i != start);
        }

        if (bin >= bins.size ())
          bins.resize (bin + 1);
        all_vs.push_front (std::move (v));
        bins[bin].push_back (all_vs.begin ());
        return true;
      }

      void union_with (vector_backed_bin&& other) {
        for (auto&& v : other.all_vs)
          insert (std::move (v));
      }

      void intersect_with (vector_backed_bin&& other) {
        if (bins.empty ())
          return;
        vector_backed_bin intersection (bins.size ());

        size_t bin = bins.size () / 2;

        do {
          for (auto& x : bins[bin]) {
            bool dominated = false;

            // These can dominate x
            for (size_t i = bin; i < other.bins.size (); ++i) {
              for (auto& el : other.bins[i]) {
                V&& v = x->meet (*el);
                if (v == *x)
                  dominated = true;
                // TODO ("Check v == el too?  See if this is good tradeoff.");
                intersection.insert (std::move (v));
                if (dominated)
                  break;
              }
              if (dominated)
                break;
            }
            if (dominated)
              continue;

            // These cannot dominate x
            for (ssize_t i = std::min (bin, other.bins.size () - 1); i >= 0; --i) {
              for (auto it = other.bins[i].begin (); it != other.bins[i].end ();
                   /* in-body */) {
                V&& v = x->meet (**it);
                if (v == **it) {
                  intersection.insert (std::move (v));
                  if (it != other.bins[i].end () - 1)
                    std::swap (*it, other.bins[i].back ());
                  other.bins[i].pop_back ();
                }
                else {
                  intersection.insert (std::move (v));
                  ++it;
                }
              }
            }
          }
          bin = (bin == bins.size () - 1 ? 0 : bin + 1);
        } while (bin != bins.size () / 2);

        *this = std::move (intersection);
      }

      template <typename F>
      [[nodiscard]] vector_backed_bin apply (const F& lambda) const {
        vector_backed_bin res (bins.size ());
        for (auto& el : all_vs)
          res.insert (lambda (el));

        return res;
      }

      [[nodiscard]] auto& get_backing_vector () { return all_vs; }

      [[nodiscard]] const auto& get_backing_vector () const { return all_vs; }

      [[nodiscard]] auto begin () const { return all_vs.begin (); }

      [[nodiscard]] auto end () const { return all_vs.end (); }

    private:
      std::list<V> all_vs;
      using bins_t = std::vector<std::vector<typename decltype (all_vs)::iterator>>;
      bins_t bins;  // [n] -> all the vectors with v.bin() = n

      // Surely: if bin_of (u) > bin_of (v), then v can't dominate u.
      [[nodiscard]] size_t bin_of (const V& v) const {
        if constexpr (vectors::has_bin<V>::value)
          return v.bin ();
        return 0;
      }
  };

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const vector_backed_bin<V>& f) {
    for (auto&& el : f)
      os << el << std::endl;

    return os;
  }
}
