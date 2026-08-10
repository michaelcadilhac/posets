#pragma once

// Downset backend over a Piipponen-Valmari covering sharing tree
// (utils::cst).  Mirrors simple_sharingtree_backed: the underlying CST
// is a forest shared across instances of the same dimension, antichain
// minimization is performed on the materialized backing vector after
// each set-level operation.  The CST itself answers covers() queries
// directly using the m-prefix-sum prune from Section 6 of the paper.

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/cst.hh>
#include <posets/utils/reduce.hh>

namespace posets::downsets {

  template <Vector V>
  class cst_backed {
    private:
      size_t root {};
      std::shared_ptr<utils::cst<V>> forest;
      std::vector<V> vector_set;
      static std::map<size_t, std::weak_ptr<utils::cst<V>>> forest_map;

      void init_forest (size_t dimkey) {
        auto res = cst_backed::forest_map.find (dimkey);
        if (res != cst_backed::forest_map.end ()) {
          const std::shared_ptr<utils::cst<V>> live = res->second.lock ();
          if (live)
            this->forest = live;
          else {
            this->forest = std::make_shared<utils::cst<V>> (dimkey);
            res->second = this->forest;
          }
        }
        else {
          this->forest = std::make_shared<utils::cst<V>> (dimkey);
          cst_backed::forest_map.emplace (dimkey, this->forest);
        }
      }

      // Build a temp tree, harvest its antichain via covers (strict mode),
      // then rebuild a clean tree with only those.  Same pattern as
      // simple_sharingtree_backed::reset_tree.
      void reset_tree (std::vector<V>&& elements) noexcept {
        this->vector_set = utils::reduce_to_maxima (std::move (elements));
        std::vector<V> antichain;
        antichain.reserve (this->vector_set.size ());
        for (const auto& e : this->vector_set)
          antichain.push_back (e.copy ());
        this->root = this->forest->add_vectors (std::move (antichain));
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it = this->vector_set.begin (); it != this->vector_set.end (); ++it)
          for (auto it2 = it + 1; it2 != this->vector_set.end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        return true;
      }

    public:
      using value_type = V;

      cst_backed () = delete;
      cst_backed (const cst_backed&) = delete;
      cst_backed (cst_backed&&) = default;
      cst_backed& operator= (const cst_backed&) = delete;
      cst_backed& operator= (cst_backed&&) = default;

      cst_backed (std::vector<V>&& elements) noexcept {
        init_forest (elements.begin ()->size ());
        reset_tree (std::move (elements));
        assert (this->is_antichain ());
      }

      cst_backed (V&& v) {
        init_forest (v.size ());
        std::vector<V> single;
        single.emplace_back (std::move (v));
        this->root = this->forest->add_vectors (std::move (single));
        this->vector_set = this->forest->get_all (this->root);
      }

      [[nodiscard]] auto size () const { return this->vector_set.size (); }
      auto begin () { return this->vector_set.begin (); }
      [[nodiscard]] auto begin () const { return this->vector_set.begin (); }
      auto end () { return this->vector_set.end (); }
      [[nodiscard]] auto end () const { return this->vector_set.end (); }
      [[nodiscard]] auto& get_backing_vector () { return vector_set; }
      [[nodiscard]] const auto& get_backing_vector () const { return vector_set; }

      [[nodiscard]] bool contains (const V& v) const {
        return this->forest->covers_vector (this->root, v);
      }

      void union_with (cst_backed&& other) {
        assert (other.size () > 0);
        std::vector<V*> undomd;
        undomd.reserve (this->size () + other.size ());
        for (auto& e : this->vector_set)
          if (not other.forest->covers_vector (other.root, e, true))
            undomd.push_back (&e);
        for (auto& e : other.vector_set)
          if (not this->forest->covers_vector (this->root, e, false))
            undomd.push_back (&e);
        assert (not undomd.empty ());

        std::vector<V> antichain;
        std::vector<V> result;
        antichain.reserve (undomd.size ());
        result.reserve (undomd.size ());
        for (auto& r : undomd) {
          antichain.push_back (r->copy ());
          result.push_back (std::move (*r));
        }
        this->root = this->forest->add_vectors (std::move (antichain));
        this->vector_set = std::move (result);
        assert (this->is_antichain ());
      }

      void intersect_with (const cst_backed& other) {
        std::vector<V> intersection;
        bool smaller_set = false;

        for (const auto& x : this->vector_set) {
          assert (x.size () > 0);
          const bool dominated = other.contains (x);
          if (dominated)
            intersection.push_back (x.copy ());
          else
            for (auto& y : other)
              intersection.push_back (x.meet (y));
          smaller_set or_eq not dominated;
        }

        if (not smaller_set)
          return;

        this->reset_tree (std::move (intersection));
        assert (this->is_antichain ());
      }

      template <typename F>
      [[nodiscard]] auto apply (const F& lambda) const {
        std::vector<V> ss;
        ss.reserve (this->vector_set.size ());
        for (const auto& v : this->vector_set)
          ss.push_back (lambda (v));
        return cst_backed (std::move (ss));
      }
  };

  template <Vector V>
  std::map<size_t, std::weak_ptr<utils::cst<V>>> cst_backed<V>::forest_map;

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const cst_backed<V>& f) {
    for (auto&& el : f.get_backing_vector ())
      os << el << std::endl;
    return os;
  }
}
