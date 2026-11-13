# Posets

Posets is a header-only C++20 library for downward closed sets. A downset stores
its maximal vectors. A query belongs to the set when some stored vector is at
least as large in every coordinate. The library provides vector, KD tree, trie,
and sharing-tree backends. Its main uses are parity games and LTL synthesis.

## Build and test

The library requires `std::experimental::simd`, provided by GCC's `libstdc++`.
Use GCC with libstdc++, Meson, and Ninja. From the repository root:

```sh
CXX=g++ CXXFLAGS="-DDIMENSION=100" meson setup build --buildtype debug
meson compile -C build -j 1
meson test -C build --no-rebuild --num-processes 1 --print-errorlogs
```

`DIMENSION` sets benchmark fixture sizes. It does not limit library vectors.
The suite runs compatible, native, and max SIMD variants serially. Test checks
stay active in release builds; library assertions follow `b_ndebug`.
The random downset and SIMD kernel tests are smoke tests. Matrix tests use an
independent scalar oracle for generator sets, membership, and recorded operations.

On macOS, use Homebrew GCC and its libstdc++:

```sh
brew install gcc@15 meson ninja
CXX="$(brew --prefix gcc@15)/bin/g++-15" CXXFLAGS="-DDIMENSION=100" \
  meson setup build --buildtype debug
meson compile -C build -j 1
meson test -C build --no-rebuild --num-processes 1 --print-errorlogs
```

See [issue #1](https://github.com/michaelcadilhac/posets/issues/1) for the
original macOS toolchain diagnosis.

## Use the library

Include the headers you need. There is no library binary to link. Save this as
`example.cc`:

```cpp
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/vectors.hh>
#include <cstdint>
#include <span>

int main() {
  using V = posets::vectors::simd_vector_backed_sum<std::int8_t>;
  const std::int8_t coordinates[] = {1, 2};
  V point{std::span<const std::int8_t>(coordinates)};
  posets::downsets::rank_bucketed_vector_backed<V> set(point.copy());
  return set.contains(point) ? 0 : 1;
}
```

From a source checkout, supply both header roots. `lib` holds the bundled Boost
headers used by the aligned allocator and sharing forest:

```sh
g++ -std=c++20 -Iinclude -Ilib example.cc -o example
./example
```

For a Meson subproject, put Posets in `subprojects/posets`. In the parent's
`meson.build`, after its C++20 `project()` declaration, use:

```meson
posets = subproject('posets')
posets_dep = posets.get_variable('posets_dep')
executable('example', 'example.cc', dependencies: posets_dep)
```

`posets_dep` supplies both source header roots. Subproject use does not build the
standalone test executables or install headers.

For an installed consumer, stage a standalone build in a fresh directory:

```sh
meson setup build-install --prefix=/usr/local
meson compile -C build-install -j 1
DESTDIR="$PWD/stage" meson install -C build-install --no-rebuild
g++ -std=c++20 -Istage/usr/local/include example.cc -o example-installed
./example-installed
```

The install includes all public Posets headers and the bundled Boost subset
under `include/boost`, with its license under `share/licenses/posets`. It needs
no source-tree paths or separate Boost installation. Use a dedicated prefix when
keeping another Boost version. There is no pkg-config file or installed Meson
package; an installed Meson consumer can declare an include dependency on that
prefix's `include` directory.

The suite checks every public header alone, then compiles and runs a consumer
against a fresh staged install. The checks reject accidental use of host Boost.

## Contracts and backend choices

Vectors are move-only. Use `copy()` for an owning copy. Construction from a span
copies the coordinates. Vectors compared or stored together must have the same
dimension. Downsets expose distinct maximal generators through iteration.
`apply(f)` builds a new downset and removes dominated results. Union consumes an
rvalue RHS; intersection reads a const RHS. Treat consumed operands as objects
to destroy or reassign unless the backend explicitly promises reusable emptiness.

This table gives a safe use contract. A precondition is not a runtime validation
promise. Empty means an empty constructor range and empty-set algebra. A zero
column describes nonempty vectors with no coordinates.

| Backend | Empty | Zero dimension | Coordinate domain and exceptions |
|---|---|---|---|
| `rank_bucketed_vector_backed` | Supported | Supported | Signed integral exact ranks; uncached nonintegral vectors use constant rank. Insert failures preserve contents with nothrow moves; a custom comparison failure during compaction can leave an empty set. Union has no rollback and consumes its RHS even on failure. |
| `sharingtrie_backed` | Supported | Precondition: positive | Sum-based reduction needs the integral domain below. Rebuild and algebra give the strong guarantee; failed construction can consume its input. |
| `block_sharingtrie_backed` (experimental) | Supported | Rejected | Same reduction domain and transaction guarantee as sharingtrie. The utility throws `std::invalid_argument` for zero or mixed dimensions. |
| `lazy_kdtree_backed` (experimental) | Supported | Rejected | Coordinate comparisons, no rank sums. Dimension errors throw `std::invalid_argument`. Insert/intersection preserve logical contents on failure; union keeps earlier successful inserts. |
| `bboxtree_backed`, `sharingtree_backed`, `simple_sharingtree_backed`, `cst_backed` | Supported | Use positive dimensions | Sum-based reduction; CST also uses exact prefix sums. Allocation/comparison failures have no blanket strong guarantee. |
| `vector_backed`, `filtered_vector_backed`, `vector_backed_bin`, `vector_backed_one_dim_split_intersection_only`, `vector_or_kdtree_backed`, `kdtree_backed`, `comptrie_backed`, `skiplist_backed` | Use nonempty inputs | Use positive dimensions | Respect the vector's domain; reducers, cached sums and skiplist keys need the exact integral domain below. Several allocating paths are `noexcept` and terminate on failure. |
| `set_backed` | Supported | Use positive dimensions | Coordinate comparisons. The single-vector constructor allocates under `noexcept`; no blanket rollback guarantee. |

`get_backing_vector()` and mutable iterators are extraction interfaces. Do not edit
live generators and then query an indexed backend: ranks, trees, filters or
materialized views can become stale. Use a documented `clear()` or reassignment.
The `Downset` concept checks signatures, not these invariants.

Queries on KD trees and tries reuse mutable scratch. BBox and filtered backends
can build caches in const queries. Sharing-tree families also share forests and
registries across instances of the same vector type and dimension. Compressed
tries share a permutation registry. Synchronize access to shared state, including
construction and destruction. The library provides no general thread-safety
guarantee. Rank/BBox statistics use presence
semantics: defining their macro as `0` still enables them. Lazy KD statistics use
a numeric `#if` switch.

Boolean wrappers encode tail coordinates as `-1/0`. Define
`posets::vectors::bool_threshold` and `bitset_threshold` once if using those
wrappers. Keep the thresholds fixed while objects live, and keep each tail within
its representation's capacity. `x_and_wordvec` retains its own split metadata;
older wrappers reread the globals.

## Linting

Use LLVM 22.1.8, matching CI (install `python3-venv` on Debian/Ubuntu):

```sh
python3 -m venv ~/.cache/posets-lint
. ~/.cache/posets-lint/bin/activate
python3 -m pip install -r .github/requirements-lint.txt
clang-tidy tests/downset-bm.cc -- -Iinclude -Ilib --std=c++20
.github/run-clang-format.py --clang-format-executable clang-format -r include
```

This tidy command checks the included and instantiated surface of one translation
unit. The header-consumption tests cover independent header parsing.

## Sum and bin domains

Cached sums, reducer keys, rank buckets, skiplist keys and BBox keys share
`posets::utils::sum_t<T>` (or `vector_sum_t<V>` for a vector type). Signed
8/16/32-bit coordinates use `int64_t`, with maximum dimensions of
`2^56`, `2^48`, and `2^32` respectively. Cached constructors check the full
dimension before allocation or payload access; sum-using helpers and containers
also check custom vectors. Unsupported dimensions throw `std::length_error`.
With d coordinate value bits and p accumulator value bits, `k <= 2^(p-d)`
puts every intermediate sum in `[-2^p, 2^p-1]`, including all-negative inputs.
Signed 64-bit coordinates use GCC/Clang's signed 128-bit integer and admit every
`size_t` dimension: `(2^64 - 1) * 2^63 < 2^127`. Exact paths require signed
integral coordinates of at most 64 bits and `size_t` of at most 64 bits
(checked at compile time).

Boolean wrappers report the actual coordinate sum; their tails encode `-1/0`.
Custom `cached_sum()` implementations must also return the exact coordinate
sum. Key arrays passed to the reducer or BBox builder use
`std::vector<posets::utils::vector_sum_t<V>>`. Narrow-coordinate object layouts
and key widths are retained; full-width cached vectors and keys grow.

Uncached nonintegral vectors remain supported by rank buckets through a
constant rank and inclusive component comparisons; they do not use the
integral equal-rank shortcut. Nonintegral cached sums and sum-based reducers
are rejected at compile time.

`vector_backed_bin` uses `max(0, sum) / dimension` for cached vectors and
`max(0, first_coordinate) / dimension` for uncached integral vectors. These
keys clamp negatives to zero and saturate at `SIZE_MAX` before conversion,
so they are monotone on every supported `size_t` width. Bin-count growth is
checked before insertion; unrepresentable counts throw `std::length_error`.
Zero-dimensional and nonintegral vectors use bin zero. Acacia's signed-char dimensions remain within the `2^56` bound.

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

Native width follows the compiler's target flags; an explicit compatible selector
keeps the compatible lane count even with native target flags. Build all
translation units that share Posets types with the same flags and macros.

An explicit `POSETS_SIMD_WIDTH` overrides the documented legacy aliases:
`SIMD_IS_NATIVE=1` selects native even with `SIMD_IS_MAX=1`; otherwise
`SIMD_IS_MAX=1` selects max; otherwise an explicit `SIMD_IS_NATIVE=0` selects
compatible. With neither alias, or **only `SIMD_IS_MAX=0`**, selection uses
the native default. For compatible width, explicitly select `POSETS_SIMD_WIDTH=0` (or set both legacy aliases to 0).
Legacy Boolean spellings `true`/`false` are supported too.

Use one executable per mode to report actual lane counts:

```sh
for mode in compatible native max; do
  build/tests/downset-matrix-$mode --metadata
done
```

The metadata also reports block sizes and alignment.
Native and max can be aliases for some element types;
max means the library implementation's maximum **lane count**, not the widest
single hardware register. Storage uses lane counts rather than assumed byte
sizes, scalar input loads are element-aligned, and C++20 allocators/new respect
SIMD over-alignment (including arrays of embedded SIMD objects).

Standalone matrix, layout and related correctness executables add `-march=native`
by default on x86; other targets keep the compiler's default target without extra
ISA flags. This test-only option does not change library defaults. CI runs independent
Linux x86-64 and macOS arm64 jobs, each printing all three modes' metadata:

* Linux retains the native matrix suite and the portable Valgrind suite (with
  `-Dtest_native_isa=false` and `-march=x86-64 -mtune=generic`). Valgrind follows
  replay subprocesses. Normal suites also run the packaging checks. The Valgrind
  job skips that suite because it invokes compilers and build tools.
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

## Backend details

The [backend contract notes](docs/backends.md) describe trie transactions, blocked
trie controls, and lazy KD policies in detail. Compile layout and statistics
macros consistently in all translation units.

| Control (default `0`) | Effect |
|---|---|
| `POSETS_SHARINGTRIE_COMPACT_NODES` | Compact reachable trie nodes during rebuild; old and new arrays coexist. |
| `POSETS_KDTREE_NARROW_INDICES` | Use checked 32-bit leaf indices, limited to `UINT32_MAX` input vectors. |
| `POSETS_COMPTRIE_SIMD_PATHS` | Compare compressed paths with SIMD; each query packs its coordinates. |

These controls and `block_sharingtrie_backed` are experimental. Inclusion through
an umbrella header does not select an application backend.

## Synthetic layout tools

Use a debug build for diagnostics and a release build for timing. The native
ISA option applies the same target flags to every layout/control executable.
For a portable comparison use `-Dtest_native_isa=false` consistently instead.

```sh
meson setup build-debug --buildtype=debug -Dtest_native_isa=true
meson compile -C build-debug -j 1
meson test -C build-debug --no-rebuild --num-processes 1 --print-errorlogs

meson setup build-release --buildtype=release -Dtest_native_isa=true -Db_lto=true -Db_lto_threads=1
meson compile -C build-release -j 1 layout-bm-baseline layout-bm-trie-compact \
  layout-bm-kd-narrow layout-bm-comp-simd layout-memory-baseline \
  layout-memory-trie-compact layout-memory-kd-narrow layout-memory-comp-simd
```

`build-release/tests/layout-bm-CONTROL BACKEND SHAPE DIM N QUERIES ROUNDS CHECK`
emits one space-separated `key=value` record. Controls are `baseline`, `trie-compact`,
`kd-narrow`, `comp-simd`. Replace `CONTROL` with one of these names.
`layout-memory-CONTROL` accepts the same arguments. Calling either executable
without arguments prints the backend names and usage, then exits 1.
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
Lazy KD nodes and sealed pending blocks each hold two `V` bounds; their owned
coordinate buffers are also excluded. These bounds can add many heap allocations.
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

## Lazy KD bounds and memory

`lazy_kdtree_backed<V, true>` uses fractional cascading. The `false` control uses
independent leaf searches. Both use full vector comparisons to decide membership.
Geometry, exact live counts, and conservative coordinate bounds on frozen
subtrees and sealed pending blocks can prune. Passing a bound never proves
membership. A sampled catalog's end never proves that a subtree is empty.

Each indexed node and sealed pending block owns two `V` bounds. Dynamic vectors
can give each bound its own coordinate buffer. Those buffers are excluded from
`known_retained`; the byte count is not total index memory. The
[contract notes](docs/backends.md#lazy-bucket-kd-with-fractional-cascading) cover
pending blocks, maintenance thresholds, statistics, and exception guarantees.

Current navigation and historical README errata are in the
[experiment guide](experiments/README.md). The frozen package keeps its original
results, source snapshots, and manifests.
