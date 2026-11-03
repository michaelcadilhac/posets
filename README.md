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

`SIMD_IS_MAX=0` uses the compatible ABI (128-bit on x86-64), while
`SIMD_IS_MAX=1` retains the library's maximum fixed lane count. The additive
`SIMD_IS_NATIVE=1` explicitly takes precedence and uses the compiler target's
native lane count. Defaults and existing callers retain their previous meaning.
These are compile-time choices; use the same target flags for width comparisons.
`downset-matrix-{compatible,native,max} --metadata` reports the actual lane counts.
The native and max modes may instantiate the same vector type and should then be
reported as aliases rather than independent treatments.

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
