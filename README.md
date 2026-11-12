# Downset Manipulation Library
This header-only C++ library implements several data structures that can be
used to store and manipulate downward closed sets (a.k.a. downsets).

The implementations include:
* Vector-based data structures
* kd-tree-based data structures
* Sharing-tree data structures (much like binary decision diagrams)
* Sharing-trie data structures (like sharing-tree, which is actually a DFA, but kept as a tree)

## Applications
The downset data structures have been optimized for the following
applications:
* Parity-game solving
* Antichain-based temporal synthesis (from LTL specifications)

## Compilation
The library requires `std::experimental::simd`, provided by GCC's `libstdc++`.
On macOS, install Homebrew GCC rather than using Apple Clang's `libc++`:

```sh
brew install gcc@15 meson ninja
CXX="$(brew --prefix gcc@15)/bin/g++-15" CXXFLAGS="-DDIMENSION=100" \
  meson setup build --buildtype debug
meson compile -C build -j1
meson test -C build --no-rebuild --num-processes 1 --print-errorlogs
```

See [issue #1](https://github.com/michaelcadilhac/posets/issues/1) for the
original macOS toolchain diagnosis.

## Linting

Use LLVM 22.1.8, matching CI (install `python3-venv` on Debian/Ubuntu):

```sh
python3 -m venv ~/.cache/posets-lint
. ~/.cache/posets-lint/bin/activate
python3 -m pip install -r .github/requirements-lint.txt
clang-tidy tests/downset-bm.cc -- -Iinclude -Ilib --std=c++20
.github/run-clang-format.py --clang-format-executable clang-format -r include
```

## SIMD widths

`POSETS_SIMD_WIDTH` selects the compile-time lane count:

| Value | Meaning |
| --- | --- |
| `POSETS_SIMD_WIDTH_COMPATIBLE` (`0`) | Compatible ABI (128 bits on x86-64) |
| `POSETS_SIMD_WIDTH_NATIVE` (`1`, default) | Compilation target's native ABI |
| `POSETS_SIMD_WIDTH_MAX` (`2`) | Maximum fixed lane count for each element type |

For example, `-march=native` on AVX2 hardware or `-mavx2` selects 32-byte
blocks for int8/int16/int32/int64; `-mavx512f -mavx512bw` selects 64-byte blocks.
The default target without ISA flags may still use 16-byte blocks. There is no
runtime CPU dispatch: compile for the machines on which the program will run.
All vectorization in the library goes through `std::experimental::simd`.

Width follows the compiler's target flags; build all translation units that share
posets types with the same flags.

An explicit `POSETS_SIMD_WIDTH` overrides the documented legacy aliases:
`SIMD_IS_NATIVE=1` selects native even with `SIMD_IS_MAX=1`; otherwise
`SIMD_IS_MAX=1` selects max; otherwise an explicit `SIMD_IS_NATIVE=0` selects
compatible. With neither alias, or **only `SIMD_IS_MAX=0`**, selection now uses
the native default. This deliberately updates callers such as acacia-bonsai that
previously got the compatible ABI from `SIMD_IS_MAX=0` alone. To retain that
width, explicitly select `POSETS_SIMD_WIDTH=0` (or set both legacy aliases to 0).
Legacy Boolean spellings `true`/`false` are supported too.

`downset-matrix-{compatible,native,max} --metadata` reports actual lane counts,
block sizes and alignment. Native and max can be aliases for some element types;
max means the library implementation's maximum **lane count**, not the widest
single hardware register. Storage uses lane counts rather than assumed byte
sizes, scalar input loads are element-aligned, and C++20 allocators/new respect
SIMD over-alignment (including arrays of embedded SIMD objects).

Standalone matrix tests add `-march=native` by default on x86; other targets
keep the compiler's default target without extra ISA flags. This test-only
option does not change library or benchmark defaults. CI runs independent
Linux x86-64 and macOS arm64 jobs, each printing all three modes' metadata:

* Linux retains the native matrix suite and the portable Valgrind suite (with
  `-Dtest_native_isa=false` and `-march=x86-64 -mtune=generic`). Valgrind follows
  replay subprocesses.
* A separate Linux baseline suite uses `-Dtest_native_isa=false` with **no
  `-march` or `-m` flags**, exercising 128-bit compatible/native blocks.
* macOS arm64 uses Homebrew `gcc@15`, debug, `-DDIMENSION=100`, and default
  test options. Compatible/native use 128-bit NEON blocks. Max mode uses GCC's
  maximum fixed lane count and can span multiple hardware registers, including
  on baseline x86. All tests, including POSIX allocation-failure sweeps, run on
  macOS; no Valgrind or platform skips are needed.

## Citing
If you use this library for your academic work, please cite the following paper
```
@inproceedings{DBLP:conf/atva/CadilhacFPR25,
  author       = {Micha{\"{e}}l Cadilhac and
                  Vanessa Fl{\"{u}}gel and
                  Guillermo A. P{\'{e}}rez and
                  Shrisha Rao},
  editor       = {Meenakshi D'Souza and
                  Raghavan Komondoor and
                  B. Srivathsan},
  title        = {Data Structures for Finite Downsets of Natural Vectors: Theory and
                  Practice},
  booktitle    = {Automated Technology for Verification and Analysis - 23rd International
                  Symposium, {ATVA} 2025, Bengaluru, India, October 27-31, 2025, Proceedings},
  series       = {Lecture Notes in Computer Science},
  volume       = {16145},
  pages        = {425--446},
  publisher    = {Springer},
  year         = {2025},
  url          = {https://doi.org/10.1007/978-3-032-08707-2\_20},
  doi          = {10.1007/978-3-032-08707-2\_20},
  timestamp    = {Sun, 07 Dec 2025 22:09:30 +0100},
  biburl       = {https://dblp.org/rec/conf/atva/CadilhacFPR25.bib},
  bibsource    = {dblp computer science bibliography, https://dblp.org}
}
```

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
`downsets::block_sharingtrie_backed<V, B, Simd, Cache>` are additive experiments.
The downset has the same construction, membership, apply, union and intersection
interface as `sharingtrie_backed`. The umbrella-header switch is
`POSETS_ENABLE_DOWNSET_BLOCK_SHARINGTRIE_BACKED`; existing solver/backend defaults
are unchanged. Utility rebuilds use the existing sharingtrie's publish-after-build
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

The existing kd-tree now allocates a complete-heap envelope of
`2 * bit_ceil(n) - 1` nodes for nonempty input, with checked size arithmetic.
This nearly halves its previous allocation at powers of two; other sizes save
one slot. Relabeling reuses the actual allocated capacity, including after a
shrink followed by growth. It retains that high-water allocation until object
replacement or destruction; `storage()` reports the retained capacity.

Compressed tries no longer construct unused suffix colors or store a color in
every node. Their four 32-bit node fields occupy 16 bytes, and traversal scratch
reserves at most one pending sibling per coordinate. The shared block comparison
helper stops computing equality reductions once a query has satisfied strictness.

Sharing-tree query memoization uses packed 32-bit generation/strictness stamps
instead of clearing and refilling hash maps. Warm queries reuse their storage;
newly visited or growing layers may allocate. Each visited layer needs a stamp
slot for every historical node, and vector capacity may exceed that count.
Interned forest nodes and child spans still live as long as the shared forest;
this change does not reclaim historical roots. The same change fixes child-buffer
growth for large spans, child-hash shifts beyond the word width, and a union
draft's lifetime.
These changes require measured whole-operation and memory comparisons; none
implies a universal speedup.

The common kd-tree query code also fixes a pre-existing correctness issue found by
the independent oracle: with maxima `(0,4),(1,3),(2,2),(3,1),(4,0)`, a non-strict
query `(2,3)` previously returned true. Splitting an already-credited axis from
an equal bound to a greater bound credited it twice. Non-strict crossings now
require `old < query <= new`; strict crossings require `old <= query < new`.
Comparisons against upstream `71918d6e113f33b9986968d9ae9c8ae5ea49ff27` should
state that all candidate/control builds include this common correctness fix.

## Reproducing the synthetic layout experiment

Build correctness with assertions, and use release builds for timing. The native
ISA option applies the same target flags to every layout/control executable.
For a portable comparison use `-Dtest_native_isa=false` consistently instead.

```sh
meson setup build-debug --buildtype=debug -Dtest_native_isa=true
meson compile -C build-debug -j2
meson test -C build-debug --no-rebuild -j2 --print-errorlogs

meson setup build-release --buildtype=release -Dtest_native_isa=true -Db_lto=true -Db_lto_threads=1
meson compile -C build-release -j2 layout-bm-baseline layout-bm-trie-compact \
  layout-bm-kd-narrow layout-bm-comp-simd layout-memory-baseline \
  layout-memory-trie-compact layout-memory-kd-narrow layout-memory-comp-simd
```

`layout-bm-CONTROL BACKEND SHAPE DIM N QUERIES ROUNDS CHECK` emits one
space-separated `key=value` record. Controls are `baseline`, `trie-compact`,
`kd-narrow`, `comp-simd`; backend names are printed by invoking it without args.
Shapes are `random`, `prefix`, `suffix`: binary `-1/0` equal-Hamming-weight
antichains, with possible duplicate generators. Prefix/suffix fixtures hold half
the coordinates fixed. Half the queries are stored generators, one quarter are
random within-domain equal-weight vectors, and one quarter force an above-domain
coordinate. Actual hits are reported. Wider alphabets, downset algebra and
synthetic operation replay remain available through `downset-matrix-*`.

```sh
# Independent logical oracle outside the phase timers; not a performance sample.
build-debug/tests/layout-memory-baseline b8simd suffix 65 128 128 1 1

# Clean timing example; repeat across shapes, controls and independent runs.
build-release/tests/layout-bm-baseline b8simd random 128 256 256 8 0
build-release/tests/layout-bm-trie-compact trie prefix 128 256 256 8 0
build-release/tests/layout-bm-kd-narrow kd random 128 256 256 8 0
build-release/tests/layout-bm-comp-simd comp suffix 128 256 256 8 0
```

`build_ns` measures one index build from already-materialized input vectors.
`query_ns` measures only repeated membership calls, including query
packing/permutation and cache generations. Fixture generation, oracle validation,
output and destruction are outside these phase timers. Whole-process `perf` or
Callgrind counts include those other operations and have a different scope. Run
profiles and timings after all builds have stopped, and do not mix debug timings
with release samples. These fixtures and replay tests are synthetic; they are not
captured Acacia workloads or evidence for a production-default selection.

`storage()` reports known retained capacity bytes: object, node allocation,
separate label allocation, generator objects, query scratch and stamp cache.
Inline node labels are not counted twice. `logical_labels` counts active label
bytes, excluding padded tail lanes; `separate_labels` counts the entire allocated
label capacity. Reachable-node count and allocated capacity are distinct,
including after kd-tree shrinking and moves. The per-instance comptrie view
charges its shared permutation capacity; summing multiple instances can double
count that shared allocation. Allocator metadata, opaque Vector-owned heap
payloads and the static permutation registry are excluded from `known_retained`.
`estimated_vector_payload_bytes` is separate: for this tool's scalar Vector it
uses rounded block lengths and assumes no extra container capacity; it is not
added to the known-byte total. Cache bytes describe software memoization storage,
not hardware cache residency.

Use `layout-memory-*` for allocation accounting. Only those executables replace
C++ allocation functions; clean `layout-bm-*` binaries retain the normal allocator
and report `heap_tracking=0`. The tracker counts requested live C++ heap payload
(including array cookies), excluding its own headers/alignment, malloc metadata,
direct malloc allocations, stacks and RSS. Logical fixture rows, materialized
inputs and query vectors already exist at `heap_before`; `build_heap_peak` is the
absolute live-byte peak during construction, and `build_extra_peak` subtracts that
starting baseline. `heap_after` releases the caller's moved-from input buffer
first, but still includes fixtures and queries: it is not index-only retention.
The memory executable separately rebuilds an existing index and emits
`rebuild_heap_before/peak/after`, `rebuild_extra_peak` and allocation counts, so
coexisting old/new index storage is visible. Those rebuilds are outside the phase
timers and absent from the clean executable. `query_allocations=0` is checked over
the post-build query window. Do not use instrumented allocation-build timings as
clean performance results.

## Lazy bucket KD with fractional cascading

`downsets::lazy_kdtree_backed<V, Cascade = true>` is an additive experimental
backend; `false` is the matched independent-search control. Enable the umbrella
header with `POSETS_ENABLE_DOWNSET_LAZY_KDTREE_BACKED`. Existing defaults do not
select it. The backend owns one dense vector of live maximal generators and a
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

An internal catalog is sampled: its end position **never** proves the subtree
empty. Only geometry, exact live counts and exact leaf bounds prune. Balanced
bucket trees give a geometric sum of catalog lengths below twice the point
count; retained capacities and bridge/map bytes are reported separately.
`Cascade=false` keeps the same geometry, bucket order and point comparisons,
omits all internal catalogs/bridges, and binary-searches each visited leaf. An
assertion-enabled fixture visits 32 leaves and checks one root search versus 32
independent leaf searches, with identical leaf visits/comparisons. This proves
the search work removed, not an end-to-end speedup.

`lazy_kdtree_policy` can be passed after either construction input. Defaults are
leaf size 16, minimum indexed set/buffer sizes 32, buffer fraction 0.25 and dead-ID
fraction 0.25. The buffer fraction must be in `(0,1]`, and the dead fraction in
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
The existing layout tool adds `lazy-fc` and `lazy-search`; its memory variants
retain the same construction/replacement peak accounting. The downset matrix
adds `lazy_kdtree_backed` and `lazy_kdtree_uncascaded`, including existing
contains/CPre operation replay. Allocation-failure tests cover resetting and
non-resetting insertion, full rebuild, apply, intersection, partial union,
throwing Vector moves/comparisons, allocation-free ordinary queries and moves.
