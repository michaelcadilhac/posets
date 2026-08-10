#pragma once

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <posets/concepts.hh>
#include <posets/utils/sharingforest.hh>

namespace posets::downsets {

  template <Vector V>
  class sharingtree_backed {
    private:
      size_t root {};
      std::shared_ptr<utils::sharingforest<V>> forest;
      mutable std::optional<std::vector<V>> mat;
      mutable std::optional<size_t> size_c;
      static std::map<size_t, std::weak_ptr<utils::sharingforest<V>>> forest_map;

      void invalidate_caches () {
        mat.reset ();
        size_c.reset ();
      }

      [[nodiscard]] std::vector<V>& ensure_materialized () {
        if (not mat)
          mat = forest->get_all (root);
        return *mat;
      }

      [[nodiscard]] const std::vector<V>& ensure_materialized () const {
        if (not mat)
          mat = forest->get_all (root);
        return *mat;
      }

      void init_forest (size_t dimkey) {
        auto res = sharingtree_backed::forest_map.find (dimkey);
        if (res != sharingtree_backed::forest_map.end ()) {
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
          sharingtree_backed::forest_map.emplace (dimkey, this->forest);
        }
      }

    public:
      using value_type = V;

      sharingtree_backed () = delete;
      sharingtree_backed (const sharingtree_backed&) = delete;
      sharingtree_backed (sharingtree_backed&&) = default;
      sharingtree_backed& operator= (const sharingtree_backed&) = delete;
      sharingtree_backed& operator= (sharingtree_backed&&) = default;

      sharingtree_backed (std::vector<V>&& elements) noexcept {
        init_forest (elements.begin ()->size ());
        this->root = this->forest->add_vectors (std::move (elements));
      }

      sharingtree_backed (V&& v) {
        init_forest (v.size ());
        this->root = this->forest->add_vectors (std::array<V, 1> {std::move (v)});
        this->size_c = 1;
      }

      [[nodiscard]] size_t size () const {
        if (not size_c)
          size_c = forest->count_vectors (root);
        return *size_c;
      }
      auto begin () { return ensure_materialized ().begin (); }
      [[nodiscard]] auto begin () const { return ensure_materialized ().begin (); }
      auto end () { return ensure_materialized ().end (); }
      [[nodiscard]] auto end () const { return ensure_materialized ().end (); }
      [[nodiscard]] auto& get_backing_vector () { return ensure_materialized (); }
      [[nodiscard]] const auto& get_backing_vector () const { return ensure_materialized (); }

      [[nodiscard]] bool contains (const V& v) const {
        return this->forest->covers_vector (this->root, v);
      }

      // Union in place
      void union_with (sharingtree_backed&& other) {
        assert (forest == other.forest);
        this->root = this->forest->st_union (this->root, other.root);
        invalidate_caches ();
      }

      // Intersection in place
      void intersect_with (const sharingtree_backed& other) {
        assert (forest == other.forest);
        this->root = this->forest->st_intersect (this->root, other.root);
        invalidate_caches ();
      }

      template <typename F>
      [[nodiscard]] auto apply (const F& lambda) const {
        const auto& materialized = ensure_materialized ();
        std::vector<V> ss;
        ss.reserve (materialized.size ());

        for (const auto& v : materialized)
          ss.push_back (lambda (v));

        return sharingtree_backed (std::move (ss));
      }
  };

  template <Vector V>
  std::map<size_t, std::weak_ptr<utils::sharingforest<V>>> sharingtree_backed<V>::forest_map;

  template <Vector V>
  inline std::ostream& operator<< (std::ostream& os, const sharingtree_backed<V>& f) {
    for (auto&& el : f.get_backing_vector ())
      os << el << std::endl;
    return os;
  }
}
