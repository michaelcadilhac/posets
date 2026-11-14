# Backend contracts and storage

See the [main README](../README.md) for use, builds, and backend choices.

## Sharingtrie allocation failures

`sharingtrie_backed` construction propagates allocation errors. Its rvalue
construction input may be consumed by antichain reduction if construction fails;
retry construction with fresh input. Completed objects always own their storage.

`utils::sharingtrie::relabel_trie`, and downset union/intersection, provide the
strong exception guarantee: on failure, both operands' generators and membership
remain unchanged and can be queried, retried, moved or destroyed. Rebuilds prepare
an owning node array, colors, query caches and reserved vector storage separately,
then move generators and publish with non-throwing swaps. Even projected pointers
into the old trie stay valid until that final step. Vectors with potentially
throwing moves use `copy()` instead. Projections must select values without
mutating them; repeated references to the same movable source are not supported.
During a rebuild, old and replacement storage coexist, with a temporary
source-pointer array to avoid copying ordinary vectors. Colors are globally
unique across layers, so the query cache needs only two generation stamps per
color, without multiplying its size again by the vector dimension. Even debug
antichain validation runs before publication, so throwing comparisons preserve
the same guarantees.

Empty-set algebra is supported; nonempty zero-dimensional sharingtrie inputs
remain outside the contract. SIMD arrays and vector storage use C++20 owning
allocation with the element type's alignment. Allocation sweeps in
`sharingtrie-allocation` cover early and late build/cache failures, aliased
relabels, both algebra operations, aligned allocations and retry/destruction.

## Experimental blocked trie and layout controls

`utils::block_sharingtrie<V, B = 8, Simd = true, Cache = true>` and
`downsets::block_sharingtrie_backed<V, B, Simd, Cache>` are experimental backends.
The downset has the same construction, membership, apply, union and intersection
interface as `sharingtrie_backed`. The umbrella-header switch is
`POSETS_ENABLE_DOWNSET_BLOCK_SHARINGTRIE_BACKED`. The unconfigured umbrella
includes this component. Including it does not select a solver backend.
Utility rebuilds use the existing sharingtrie's publish-after-build
transaction, including aliased projections and the throwing-move copy fallback.
The utility accepts empty collections, rejects nonempty zero-dimensional inputs
and dimension mismatches with `std::invalid_argument`, and empties moved sources.
Downset inputs must have a common positive dimension, as for the existing trie.
Query scratch is retained and mutable, so concurrent queries on one instance need
external synchronization, as with the existing sharingtrie and kd-tree.

Each node holds a contiguous child range and a consecutive block of `B` packed
labels. Groups are lexicographically descending; a failed later coordinate does
not prune later incomparable siblings. Strictness owed carries across blocks.
Only active coordinates are compared, including a scalar final partial chunk;
zero-filled storage padding never supplies strictness. Query packing reads the
public coordinate interface, so generic vectors and Boolean wrappers need no
contiguous-storage promise. Its cost is included in every query.

`Simd=false` selects a coordinate loop (the compiler may still auto-vectorize it).
`Simd=true` uses `experimental/simd` with `min(B, selected SIMD lanes)` lanes;
`B` is independent of hardware width. Exact signatures include depth, the full
block and **all** ordered child colors. Globally unique colors index `2*C`
generation stamps, including strictness owed. `Cache=false` is a separate control
with the same blocked shape and comparisons. Wide blocks can increase the root's
branch count and scan cost; no universal speedup is assumed.

These independent compile-time controls default to `0`:

| Control | Experiment | Cost or limit |
|---|---|---|
| `POSETS_SHARINGTRIE_COMPACT_NODES` | Copy reachable nodes into an exact-sized array before coloring | The original array and compact array coexist during rebuild |
| `POSETS_KDTREE_NARROW_INDICES` | Use checked `uint32_t` leaf indices | At most `UINT32_MAX` input vectors; split axes and signed label values retain their domains |
| `POSETS_COMPTRIE_SIMD_PATHS` | Compare compressed paths with the same bounded-load SIMD helper | Pack the complete permuted query once per call, including early rejections |

Define configuration macros consistently across translation units. These controls
change layouts or inline code and must not differ within one program. Node sizes
and retention depend on element type, ABI and fixture; the diagnostics report the
actual instantiated sizes. Across-sibling tiled SIMD and contiguous
generator-payload slabs remain design candidates. The separate lazy bucket KD
backend and its fractional-cascading control are described below.

The legacy KD tree retains a checked complete-heap envelope of
`2 * bit_ceil(n) - 1` nodes. Relabeling reuses its high-water capacity.
Sharing forests retain interned nodes and child spans for their full lifetime.
Their query stamps also cover historical nodes. See the
[experiment guide](../experiments/README.md) for historical comparisons and fixes.

## Lazy bucket KD with fractional cascading

`downsets::lazy_kdtree_backed<V, Cascade = true>` is an additive experimental
backend; `false` is the matched independent-search control. The unconfigured umbrella includes it. Configured builds can enable it with
`POSETS_ENABLE_DOWNSET_LAZY_KDTREE_BACKED=1`. Inclusion does not select a solver
backend. The backend owns one dense vector of live maximal generators and a
frozen bucket KD index plus a pending insertion buffer. It does not retain copied
historical generator payloads. Leaves resolve stable IDs through an ID-to-dense-
slot map. Swap-erasure updates both maps; retired IDs are never reused within an
epoch. Rebuilding publishes new geometry, catalogs and densely renumbered maps
together. Exact live descendant counts prune wholly retired subtrees, and retired
payloads are never accessed. Split geometry and copied filter keys remain frozen
between rebuilds; deletion makes their bounds conservative.

All leaf buckets are sorted on the same fixed filter coordinate, currently **0**.
Internal augmented catalogs merge every second entry (positions 1, 3, 5, ...) from
both child catalogs. Each entry stores lower-bound bridges into both children.
One binary search at the root propagates to a child with at most one predecessor
correction, including empty catalogs, end positions and duplicate keys. For
membership, the root uses `lower_bound(q[0])` and correction `previous >= q[0]`;
for reporting points dominated by an insertion it uses `upper_bound(q[0])` and
correction `previous > q[0]`. Both bridge types remain **lower bounds of the next
parent key**. No `q+1` arithmetic or key sentinel is used. Full Vector partial
orders decide dominance and strictness: a strict dominator may have an equal
filter coordinate. Generic vector storage remains opaque; SIMD-backed vectors
supply SIMD through their existing partial-order implementation.

An internal catalog is sampled: its end position never proves the subtree empty.
Geometry, exact live counts, and conservative coordinate bounds on frozen
subtrees and sealed pending blocks can prune. Passing a bound never proves
membership. Balanced bucket trees give a geometric sum of catalog lengths below twice the point
count; retained capacities and bridge/map bytes are reported separately.
`Cascade=false` keeps the same geometry, bucket order and point comparisons,
omits all internal catalogs/bridges, and binary-searches each visited leaf. A
correctness fixture visits 32 leaves and checks one root search versus 32
independent leaf searches, with identical leaf visits/comparisons. This proves
the search work removed, not an end-to-end speedup.

`lazy_kdtree_policy` can be passed after either construction input. Defaults are
leaf size 16, minimum indexed set/buffer sizes 32, buffer fraction 0.25,
dead-ID fraction 0.25, and `pending_block_size=16`. Sealed pending blocks retain
coordinate bounds used to skip comparison scans. The buffer fraction must be
in `(0,1]`, and the dead fraction in
`[0,1)` (either signed zero is accepted for the latter). Binary64 policy validation
uses integer bit ordering, including for subnormals, infinities and NaNs, so
`-Ofast` cannot change these accepted intervals. Other floating representations
use standard classification/comparisons and inherit the compiler's floating-point
assumptions. The buffer trigger grows proportionally to live size; tiny sets
stay buffer-only. Physical pending tombstones count toward maintenance, so
repeated dominating inserts do not accumulate unbounded retired IDs.
The optional `max_index_bytes` trigger counts frozen index and ID-buffer/map
capacity when dead history can be reclaimed; it is a maintenance trigger, not a
hard process-memory limit. Thresholds are heuristics, with no amortized runtime
claim. Coordinate 0 and default bucket sizes are controls to measure, not claimed
optimal choices. An explicit `rebuild()` resets the ID epoch without changing the
represented set.

Vector-input construction validates all dimensions before consuming payloads.
Up to `bulk_scan_limit` inputs (default 4096), it first looks for one representative
dominating all rows visited so far. Covered/equal rows preserve it; a strict
dominator replaces it. If this completes, only that row is staged, avoiding full
sort scratch for dominance chains, duplicates, or a dominant row followed by a
covered cloud. An incomparable row falls back to sorting all original indices in
descending coordinate lexicographic order and scanning retained maxima. A later
candidate cannot dominate an earlier retained maximum; duplicates are covered.
This uses no coordinate sums or Vector total-order operator, including for
full-width signed values. It then stages dense maps and one frozen index (or a
pending buffer below `min_index_size`) before moving final survivors. Allocation
or comparison failure in this path leaves the input payloads unchanged; throwing
moves retain the copy fallback. The input vector container itself is not stolen
or cleared. Larger inputs use indexed insertion, whose construction can consume
earlier inputs before a later failure; setting `bulk_scan_limit=0` forces that
control. The scan has quadratic worst-case comparison work: 4096 is an initial
heuristic, not a measured universal crossover. Apply and intersection keep their
streaming reducers to limit transient payload storage.

A failed insertion preserves logical generators and membership. Earlier
successful reserves can change capacities and invalidate iterator addresses.
Each insertion stages allocations and throwing copies before committing its
logical update. The fast path requires both nothrow move construction and move
assignment; otherwise survivors are copied into unpublished storage. Whole union
is incremental and provides the **basic** guarantee: earlier accepted insertions
remain if a later one throws. Its RHS stays unchanged until success, then becomes
empty. Apply and intersection stream into fresh epochs; intersection publishes
only a complete result. Empty sets and safe moved-from reuse are supported;
nonempty zero-dimensional inputs and inconsistent dimensions are rejected.
Iteration and `get_backing_vector()` expose the dense live generators. As for
existing indexed backends, arbitrary external generator edits invalidate the
index and antichain invariants: use `clear()` or reassignment instead.
`rebuild()` preserves generators rather than normalizing arbitrary edits; its
input must already retain common positive dimensions and a canonical antichain. Mutations
invalidate iterators and must not race with queries. Optional statistics also
need synchronization if queries share an instance.

The non-owning `utils::bucket_kdtree<V, Cascade>` utility exposes the frozen
index for independent testing. Its rebuild input IDs must be unique; its caller
keeps each live payload valid and supplies consistent access/live predicates.
`retire(id)` is idempotent. No ID may acquire another payload without rebuilding
all geometry and catalogs. The utility ID-to-leaf map scales with the supplied
ID span; the owning backend freezes densely renumbered IDs. `check_bridges(key, upper)` checks exact cascaded
positions against independent standard-library searches.

Define `POSETS_LAZY_KDTREE_STATS=1` consistently across translation units to
collect query/node/leaf visits, full comparisons, binary searches and bridge
corrections. Index query counts exclude requests answered by the pending
buffer; pending comparison counts include membership and retirement checks.
`storage().bulk_comparisons` separately counts preflight and coverage partial-order
calls in bulk construction, including duplicate rejection; it excludes the coordinate
comparisons used for lexicographic sorting. Frozen index counters restart on
index replacement, whereas pending and bulk comparison counters persist until
`reset_query_statistics()` (which clears all three) or object clearing/replacement.
Successful intersection installs the counters of its fresh streaming result. Use a
Vector-level partial-order counter for total construction work across paths;
query counters cannot recover work discarded by intermediate index replacements.
Reset statistics after construction to measure later query phases. Storage output records
`bulk_scan_limit`, `query_stats_enabled`, and `bulk_comparisons` for provenance;
zero bulk comparisons with statistics disabled do not mean no scan work occurred.
Default builds compile the increments and counter storage out.
`storage()` reports actual node/key/bridge/ID-map capacities, pending/live/retired
counts and rebuild/compaction counts. Known retained bytes include generator
objects but exclude opaque Vector-owned heap payloads and allocator overhead.
Frozen nodes share one lower/upper coordinate slab; sealed pending blocks share
another. Both slabs are separate from generator payloads, align their bases to
64 bytes, and use zero padding for SIMD comparisons. Aligned C++ allocation keeps
wide corners from straddling cache lines. They carry no cached sums. `bound_bytes` and
`pending_bound_bytes` include their complete allocated capacities in the known-byte
total. Building replacements and sealing a pending block finish all allocations
and coordinate operations before publishing. Retirement can leave conservative
bounds; passing a bound remains only permission to inspect live points.
The existing layout tool adds `lazy-fc` and `lazy-search`; its memory variants
retain the same construction/replacement peak accounting. The downset matrix
adds `lazy_kdtree_backed` and `lazy_kdtree_uncascaded`, including existing
contains/CPre operation replay. Allocation-failure tests cover resetting and
non-resetting insertion, full rebuild, apply, intersection, partial union,
throwing Vector moves/comparisons, allocation-free ordinary queries and moves.

## Filtered vector cache

`filtered_vector_backed` stores each selected dimension's threshold bitmaps in a
contiguous word slab. A membership query intersects one word at a time and keeps
only selected mask pointers on the stack; warm queries allocate no memory.
Rebuilds stage all selected dimensions and publish only after successful
construction. A failure leaves the cache dirty, so the next query retries.
Construction and cache rebuilds propagate allocation failures.

Iteration returns const generators. Requesting mutable `get_backing_vector()`
permanently selects linear membership queries for that object, since a retained
reference can change generator order or coordinates after any cache rebuild.
Such edits must still preserve common dimensions and the caller's intended
antichain invariant. Mutations and lazy cache rebuilding need external
synchronization when an object is shared across threads.
