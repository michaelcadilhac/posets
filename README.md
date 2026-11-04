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
