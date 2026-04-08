#pragma once

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/sharingforest.hh>

namespace posets::downsets {

  template <Vector V>
  class simple_sharingtree_backed {
    private:
      size_t dim;
      size_t root {};
      std::shared_ptr<utils::sharingforest<V>> forest;
      std::vector<V> vector_set;
      static std::map<size_t, std::weak_ptr<utils::sharingforest<V>>> forest_map;

      void init_forest (size_t dimkey) {
        auto res = simple_sharingtree_backed::forest_map.find (dimkey);
        if (res != simple_sharingtree_backed::forest_map.end ()) {
          // two cases now, either the pointer is live and we take a lock on it
          // or it's dead, and we need to update it
          const std::shared_ptr<utils::sharingforest<V>> live = res->second.lock ();
          if (live) {
            this->forest = live;
          }
          else {
            this->forest = std::make_shared<utils::sharingforest<V>> (dimkey);
            res->second = this->forest;
          }
        }
        else {
          this->forest = std::make_shared<utils::sharingforest<V>> (dimkey);
          simple_sharingtree_backed::forest_map.emplace (dimkey, this->forest);
        }
      }

      // Code borrowed from kdtree_backed, same idea as there
      // to keep the antichain of max elements only
      void reset_tree (std::vector<V>&& elements) noexcept {
        const size_t temp_tree = this->forest->add_vectors (std::move (elements), false);
        this->vector_set = this->forest->get_all (temp_tree);

        // we can use the temporary tree to eliminate dominated elements
        std::vector<V> antichain;
        std::vector<V> result;
        antichain.reserve (this->vector_set.size ());
        result.reserve (this->vector_set.size ());
        for (auto& e : this->vector_set) {
          if (not this->forest->covers_vector (temp_tree, e, true)) {
            antichain.push_back (e.copy ());
            result.push_back (std::move (e));
          }
        }

        this->root = this->forest->add_vectors (std::move (antichain), false);
        this->vector_set = std::move (result);
      }

      [[nodiscard]] bool is_antichain () const {
        for (auto it = this->vector_set.begin (); it != this->vector_set.end (); ++it) {
          for (auto it2 = it + 1; it2 != this->vector_set.end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        }
        return true;
      }

    public:
      using value_type = V;

      simple_sharingtree_backed () = delete;
      simple_sharingtree_backed (const simple_sharingtree_backed&) = delete;
      simple_sharingtree_backed (simple_sharingtree_backed&&) = default;
      simple_sharingtree_backed& operator= (const simple_sharingtree_backed&) = delete;
      simple_sharingtree_backed& operator= (simple_sharingtree_backed&&) = default;

      simple_sharingtree_backed (std::vector<V>&& elements) noexcept {
        init_forest (elements.begin ()->size ());
        reset_tree (std::move (elements));
        assert (this->is_antichain ());
      }

      simple_sharingtree_backed (V&& v) {
        init_forest (v.size ());
        this->root = this->forest->add_vectors (std::array<V, 1> {std::move (v)}, false);
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

      // Union in place
      void union_with (simple_sharingtree_backed&& other) {
        assert (other.size () > 0);
        std::vector<V*> undomd;
        undomd.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto& e : this->vector_set)
          if (not other.forest->covers_vector (other.root, e, true))
            undomd.push_back (&e);

        // for all elements in the other tree, if they are not dominated
        // (not necessarily strict) by this tree, we keep them
        for (auto& e : other.vector_set)
          if (not this->forest->covers_vector (this->root, e, false))
            undomd.push_back (&e);

        // ready to rebuild the tree now
        assert (not undomd.empty ());

        // We can now move the referenced elements
        std::vector<V> antichain;
        std::vector<V> result;
        antichain.reserve (undomd.size ());
        result.reserve (undomd.size ());
        for (auto& r : undomd) {
          antichain.push_back (r->copy ());
          result.push_back (std::move (*r));
        }

        this->root = this->forest->add_vectors (std::move (antichain), false);
        this->vector_set = std::move (result);
        assert (this->is_antichain ());
      }

      // Intersection in place
      void intersect_with (const simple_sharingtree_backed& other) {
        std::vector<V> intersection;
        bool smaller_set = false;

        for (const auto& x : this->vector_set) {
          assert (x.size () > 0);

          // If x is part of the set of all meets, then x will dominate the
          // whole list! So we use this to short-circuit the computation: we
          // first check whether x will be there (which happens only if it is
          // itself dominated by some element in other)
          const bool dominated = other.contains (x);
          if (dominated)
            intersection.push_back (x.copy ());
          else
            for (auto& y : other)
              intersection.push_back (x.meet (y));

          // If x wasn't in the set of meets, dominated is false and
          // the set of minima is different than what is in this->tree
          smaller_set or_eq not dominated;
        }

        // We can skip working all if we already have the antichain
        // of minimal elements
        if (not smaller_set)
          return;

        // Worst-case scenario: we do need to work
        this->reset_tree (std::move (intersection));
        assert (this->is_antichain ());
      }

      template <typename F>
      auto apply (const F& lambda) const {
        std::vector<V> ss;
        ss.reserve (this->vector_set.size ());

        for (const auto& v : this->vector_set)
          ss.push_back (lambda (v));

        return simple_sharingtree_backed (std::move (ss));
      }
  };

  template <Vector V>
  std::map<size_t, std::weak_ptr<utils::sharingforest<V>>>
      simple_sharingtree_backed<V>::forest_map;

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const simple_sharingtree_backed<V>& f) {
    for (auto&& el : f.get_backing_vector ())
      os << el << std::endl;
    return os;
  }
}
