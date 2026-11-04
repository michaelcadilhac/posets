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
Due to our use of `std::experimental` we prefer `libstdc++` thus `g++`. Even on OSX, we
recommend installing `g++` say via Homebrew. Set `CXX` to your newly installed `g++`
before setting up the build with `meson`.

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
Use the same selector and target flags in every translation unit.

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

Standalone matrix tests retain `-march=native` by default. CI runs the complete
suite natively and prints each mode's actual widths. Its separate Valgrind
build sets `-Dtest_native_isa=false` and explicit x86-64/generic compiler flags;
compatible/native/max then refer to that portable compiler target, not the host
widths. This test-only option does not change library or benchmark defaults.
Valgrind follows replay subprocesses; both lanes run the complete semantic suite.

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
The temporary replacement increases peak rebuild memory (old and new storage
coexist) and adds a source-pointer array to avoid copying ordinary vectors.

Empty-set algebra is supported; nonempty zero-dimensional sharingtrie inputs
remain outside the contract. SIMD arrays and vector storage use C++20 owning
allocation with the element type's alignment. Allocation sweeps in
`sharingtrie-allocation` cover early and late build/cache failures, aliased
relabels, both algebra operations, aligned allocations and retry/destruction.
