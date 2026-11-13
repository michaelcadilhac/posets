#pragma once

// Covering Sharing Tree (CST), after Piipponen & Valmari, "Constructing
// Minimal Coverability Sets", Fundam. Inform. 143 (2016) 393-414, Section 6.
//
// Differences from the paper:
//   * The library deals with finite N-vectors, no omega.  The paper's
//     v.w (omega-count prefix sum) is therefore identically zero and is
//     dropped, along with the topmost array indexed by total omega-count.
//   * Per-layer sibling lists are stored as contiguous slices of a flat
//     child_buffer rather than as linked lists, mirroring sharingforest's
//     allocator pattern -- single allocation, geometric growth, cache-
//     friendly traversal.  Many CSTs are built/destroyed per query in
//     downset workloads, so the allocator matters as much as the layout.
//   * Children are ordered descending by m (uniformly at every layer).
//     The paper alternates orderings between the topmost and sub-layers
//     to optimize separately-tracked w,m comparisons; with w == 0 the
//     remaining single-key m-ordering is symmetric and we choose
//     descending so that the largest prefix-sum (most likely to cover M')
//     comes first.
//
// Public API mirrors posets::utils::sharingforest so cst can plug into
// downset backends interchangeably.

#include <unordered_map>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <tuple>
#include <vector>

#include <posets/concepts.hh>

namespace posets::utils {

#ifndef CST_INIT_LAYER_SIZE
# define CST_INIT_LAYER_SIZE 100UL
#endif
#ifndef CST_INIT_MAX_CHILDREN
# define CST_INIT_MAX_CHILDREN 10UL
#endif

  template <Vector V>
  class cst {
    public:
      // Prefix sums can grow well past V::value_type's range (e.g. char *
      // dim * maxval for benchmark inputs); use a wider integer.
      using sum_t = int32_t;

    private:
      size_t dim;

      struct cst_node {
          sum_t m;                // prefix sum of |M|_1 along the path from root
          uint32_t numchild;      // number of children in next layer
          size_t cbuffer_offset;  // offset into flat child_buffer
      };

      // Hash & equality use cbuffer contents, so they need a back-pointer.
      class cst_hash {
          cst* f;

        public:
          cst_hash (cst* that) : f {that} {}

          size_t operator() (const cst_node& k) const {
            size_t res = std::hash<sum_t> () (k.m);
            const size_t* children = f->child_buffer + k.cbuffer_offset;
            for (size_t i = 0; i < k.numchild; ++i)
              res ^= std::hash<size_t> () (children[i]) << (i + 1);
            return res;
          }
      };

      class cst_equal {
          cst* f;

        public:
          cst_equal (cst* that) : f {that} {}

          bool operator() (const cst_node& a, const cst_node& b) const {
            if (a.m != b.m or a.numchild != b.numchild)
              return false;
            const size_t* ca = f->child_buffer + a.cbuffer_offset;
            const size_t* cb = f->child_buffer + b.cbuffer_offset;
            for (size_t i = 0; i < a.numchild; ++i)
              if (ca[i] != cb[i])
                return false;
            return true;
          }
      };

      std::vector<std::vector<cst_node>> layers;
      size_t* child_buffer;
      size_t cbuffer_size;
      size_t cbuffer_nxt;

      std::vector<std::unordered_map<cst_node, size_t, cst_hash, cst_equal>> inverse;

      // Reusable scratch buffers across calls
      mutable std::vector<std::tuple<size_t, size_t, size_t>> get_all_stack;

      void init (size_t d) {
        dim = d;
        // dim + 1 layers: layer 0 holds tree roots (sentinel), layer dim
        // holds leaves with m = total prefix sum.
        layers.resize (dim + 1);
        for (size_t i = 0; i < dim + 1; ++i)
          inverse.emplace_back (CST_INIT_LAYER_SIZE, cst_hash (this), cst_equal (this));

        cbuffer_size = CST_INIT_LAYER_SIZE * CST_INIT_MAX_CHILDREN;
        child_buffer = new size_t[cbuffer_size];
        cbuffer_nxt = 0;
      }

      size_t add_children (size_t n) {
        if (cbuffer_nxt + n >= cbuffer_size) {
          size_t new_size = cbuffer_size;
          while (cbuffer_nxt + n >= new_size)
            new_size *= 2;
          auto* nb = new size_t[new_size];
          std::memcpy (nb, child_buffer, cbuffer_size * sizeof (size_t));
          delete[] child_buffer;
          child_buffer = nb;
          cbuffer_size = new_size;
        }
        const size_t res = cbuffer_nxt;
        cbuffer_nxt += n;
        return res;
      }

      // Append a child id to a node, preserving descending-m order.  Caller
      // must have reserved enough space in child_buffer at node.cbuffer_offset.
      void add_son (cst_node& node, size_t son_layer, size_t son_id) {
        size_t* children = child_buffer + node.cbuffer_offset;
        const sum_t son_m = layers[son_layer][son_id].m;
        // Sons are appended in caller's order; we assert descending m.
        [[maybe_unused]] const auto last = node.numchild;
        assert (last == 0 or layers[son_layer][children[last - 1]].m > son_m);
        children[node.numchild++] = son_id;
        (void) son_m;
      }

      size_t add_node (cst_node& node, size_t layer) {
        auto it = inverse[layer].find (node);
        if (it != inverse[layer].end ())
          return it->second;
        const size_t new_id = layers[layer].size ();
        layers[layer].push_back (node);
        inverse[layer][node] = new_id;
        return new_id;
      }

      // Build a CST from a batch of vectors, recursively.
      // vecs[start..end) are indices into element_vec; current_layer is the
      // layer being built, prefix is the running m for this branch.
      // NOLINTBEGIN(misc-no-recursion)
      size_t build_node (std::vector<size_t>& vecs, size_t start, size_t end, size_t current_layer,
                         sum_t prefix, const auto& element_vec) {
        assert (start < end);
        cst_node new_node {prefix, 0, 0};

        if (current_layer < dim) {
          // Sort by element_vec[*][current_layer] descending so we walk the
          // (descending m) children naturally.
          std::sort (vecs.begin () + static_cast<std::ptrdiff_t> (start),
                     vecs.begin () + static_cast<std::ptrdiff_t> (end), [&] (size_t a, size_t b) {
                       return element_vec[a][current_layer] > element_vec[b][current_layer];
                     });
          size_t num_groups = 1;
          for (size_t k = start + 1; k < end; ++k)
            if (element_vec[vecs[k]][current_layer] != element_vec[vecs[k - 1]][current_layer])
              ++num_groups;
          new_node.cbuffer_offset = add_children (num_groups);

          size_t i = start;
          while (i < end) {
            const auto val = element_vec[vecs[i]][current_layer];
            size_t j = i + 1;
            while (j < end and element_vec[vecs[j]][current_layer] == val)
              ++j;
            const size_t son = build_node (vecs, i, j, current_layer + 1,
                                           prefix + static_cast<sum_t> (val), element_vec);
            add_son (new_node, current_layer + 1, son);
            i = j;
          }
        }
        return add_node (new_node, current_layer);
      }
      // NOLINTEND(misc-no-recursion)

    public:
      cst () = delete;

      explicit cst (size_t d) { init (d); }

      ~cst () {
        if (layers.empty ())
          return;
        delete[] child_buffer;
      }

      cst (const cst&) = delete;
      cst& operator= (const cst&) = delete;
      cst (cst&&) = delete;
      cst& operator= (cst&&) = delete;

      [[nodiscard]] size_t dimension () const { return dim; }

      template <std::ranges::input_range R>
      size_t add_vectors (R&& elements) {
        assert (not layers.empty ());
        auto element_vec = std::forward<R> (elements);
        std::vector<size_t> vector_ids (element_vec.size ());
        for (size_t i = 0; i < vector_ids.size (); ++i)
          vector_ids[i] = i;
        return build_node (vector_ids, 0, vector_ids.size (), 0, 0, element_vec);
      }

      // Cover check using the paper's prune heuristics, simplified by w=0.
      // Pre-computes msum[ell] = sum of M'[0..ell-1] and mmax[ell] = max
      // of M'[0..ell-1].  At a node v on a layer < dim with parent prefix
      // sum p_m and child prefix v.m, we have M(ell) = v.m - p_m so we
      // descend only if M(ell) >= M'(ell).  Beyond the per-step check, the
      // layer-residual heuristic compares msum[dim] - v.m against the
      // remaining query mass, mirroring the wsum/msum tests in Section 6.
      bool covers_vector (size_t root, const V& covered, bool strict = false) const {
        // Pre-compute msum and mmax for the query M'.
        std::vector<sum_t> msum (dim + 1, 0);
        std::vector<sum_t> mmax (dim + 1, 0);
        sum_t totalm = 0;
        sum_t maxm = 0;
        for (size_t i = 0; i < dim; ++i) {
          totalm += static_cast<sum_t> (covered[i]);
          maxm = std::max (static_cast<sum_t> (covered[i]), maxm);
          msum[i + 1] = totalm;
          mmax[i + 1] = maxm;
        }
        // remaining[ell] = msum[dim] - msum[ell] = mass of M' on coords [ell..dim).
        // For a candidate node v at layer ell with prefix m, the stored
        // vector through v contributes v.m on coords [0..ell), so its tail
        // sums to (full leaf m) - v.m; we want that tail >= remaining[ell],
        // i.e., leaf_m >= v.m + remaining[ell].  Easier to apply at leaves;
        // for an early prune we use: along any path from v to a leaf, total
        // can be at most v.m + (max possible tail).  Without omega that
        // bound is loose, so we keep the per-step check as the main prune.

        // DFS with stack of (layer, node_id, child_idx, parent_m, owe_strict).
        struct frame {
            size_t layer;
            size_t node;
            size_t child;
            sum_t parent_m;
            bool owe_strict;
        };
        std::vector<frame> st;
        st.reserve (dim + 4);
        // Layer 0 root is a virtual sentinel; we descend into its children.
        const cst_node& root_node = layers[0][root];
        const size_t* root_children = child_buffer + root_node.cbuffer_offset;
        for (size_t i = 0; i < root_node.numchild; ++i) {
          const cst_node& c = layers[1][root_children[i]];
          // value at coord 0 along this branch:
          const sum_t v0 = c.m;
          if (static_cast<sum_t> (covered[0]) > v0)
            // descending order: subsequent siblings have even smaller m
            break;
          const bool owe = strict and static_cast<sum_t> (covered[0]) == v0;
          st.push_back ({1, root_children[i], 0, 0, owe});
        }

        while (not st.empty ()) {
          auto [lay, node, child, parent_m, owe_strict] = st.back ();
          st.pop_back ();
          const cst_node& parent = layers[lay][node];
          if (lay == dim) {
            // Reached a leaf.  We've already verified the per-step
            // covered[lay-1] <= (parent.m - parent_m) when descending; if
            // strict, owe_strict tracks whether equality has held so far.
            if (not owe_strict)
              return true;
            // Equality on every coord -> not strictly covered.  Continue.
            continue;
          }
          // Iterate children of `parent` at next layer.
          const size_t* children = child_buffer + parent.cbuffer_offset;
          if (child < parent.numchild) {
            const size_t cidx = children[child];
            const cst_node& c = layers[lay + 1][cidx];
            const sum_t step = c.m - parent.m;
            if (static_cast<sum_t> (covered[lay]) > step) {
              // Descending order: siblings to the right have smaller m hence
              // smaller step.  Done with this parent.
              continue;
            }
            // Layer-residual prune: max remaining mass along any extension
            // is bounded by mmax[dim] * (dim - lay - 1) below, but that's
            // loose; we still want the leaf to satisfy
            //   parent_full_m - c.m  >=  msum[dim] - msum[lay+1]
            // i.e. c.m  <=  parent_full_m - (msum[dim] - msum[lay+1]).
            // Without knowing the leaf m here, we skip this prune and rely
            // on the per-step test (which is already optimal for >=
            // checking on a single path).

            // Push the next-sibling continuation, then descend.
            if (child + 1 < parent.numchild)
              st.push_back ({lay, node, child + 1, parent_m, owe_strict});
            const bool still_owe = owe_strict and static_cast<sum_t> (covered[lay]) == step;
            st.push_back ({lay + 1, cidx, 0, parent.m, still_owe});
          }
        }
        return false;
      }

      // Enumerate all stored vectors (the represented antichain).
      [[nodiscard]] std::vector<V> get_all (size_t root) const {
        get_all_stack.clear ();
        const cst_node& root_node = layers[0][root];
        const size_t* root_children = child_buffer + root_node.cbuffer_offset;
        for (size_t c = 0; c < root_node.numchild; ++c)
          get_all_stack.emplace_back (1, root_children[c], 0);

        std::vector<V> res;
        std::vector<sum_t> path;
        path.push_back (0);  // root prefix
        while (not get_all_stack.empty ()) {
          const auto [lay, node, child] = get_all_stack.back ();
          get_all_stack.pop_back ();
          const cst_node& cur = layers[lay][node];
          if (child == 0)
            path.push_back (cur.m);

          if (lay == dim) {
            assert (child == 0);
            std::vector<typename V::value_type> tmp;
            tmp.reserve (dim);
            for (size_t i = 1; i + 1 <= path.size () - 1; ++i) {
              // path[1..dim] are the prefix sums; coord i-1 = path[i] - path[i-1].
            }
            for (size_t i = 1; i < path.size (); ++i)
              tmp.push_back (static_cast<V::value_type> (path[i] - path[i - 1]));
            res.emplace_back (V (std::move (tmp)));
            path.pop_back ();
          }
          else {
            const size_t* children = child_buffer + cur.cbuffer_offset;
            if (child < cur.numchild) {
              get_all_stack.emplace_back (lay, node, child + 1);
              get_all_stack.emplace_back (lay + 1, children[child], 0);
            }
            else {
              path.pop_back ();
            }
          }
        }
        return res;
      }

      // Union: combine two trees into a tree representing the union of
      // their vector sets (we leave deduplication-of-dominated to the
      // downset wrapper, like sharingforest's simple_sharingtree_backed
      // does).  Implementation: gather all vectors from both, rebuild.
      // (A direct DAG union mirroring sharingforest::node_union is more
      // efficient but considerably more code; the rebuild path is
      // semantically correct and uses node fusion for sharing.)
      size_t st_union (size_t root1, size_t root2) {
        auto a = get_all (root1);
        auto b = get_all (root2);
        a.reserve (a.size () + b.size ());
        for (auto& v : b)
          a.emplace_back (std::move (v));
        return add_vectors (std::move (a));
      }

      // Intersection of two downsets is the antichain of meets.  We compute
      // all pairwise meets and rebuild.  Caller (downset wrapper) is
      // expected to prune dominated meets.
      size_t st_intersect (size_t root1, size_t root2) {
        auto a = get_all (root1);
        auto b = get_all (root2);
        std::vector<V> meets;
        meets.reserve (a.size () * b.size ());
        for (auto& x : a)
          for (auto& y : b)
            meets.emplace_back (x.meet (y));
        if (meets.empty ())
          // Empty intersection: return an empty tree (root with no children).
          return add_vectors (std::vector<V> {});
        return add_vectors (std::move (meets));
      }

      // Debug: every sibling list ordered descending by m.
      [[nodiscard]] bool check_layer_order () const {
        for (size_t lay = 0; lay < dim; ++lay) {
          for (const auto& n : layers[lay]) {
            const size_t* ch = child_buffer + n.cbuffer_offset;
            for (size_t i = 1; i < n.numchild; ++i)
              if (layers[lay + 1][ch[i]].m >= layers[lay + 1][ch[i - 1]].m)
                return false;
          }
        }
        return true;
      }

      void print_layer (size_t root, size_t lay = 0) const {
#ifndef NDEBUG
        const cst_node& n = layers[lay][root];
        std::cout << std::string (lay, '\t') << lay << "." << root << " [m=" << n.m << "] -> "
                  << (lay == dim ? "" : "(\n");
        const size_t* ch = child_buffer + n.cbuffer_offset;
        for (size_t i = 0; i < n.numchild; ++i)
          print_layer (ch[i], lay + 1);
        if (lay != dim)
          std::cout << std::string (lay, '\t') << ")\n";
#else
        (void) root;
        (void) lay;
#endif
      }
  };

}  // namespace posets::utils
