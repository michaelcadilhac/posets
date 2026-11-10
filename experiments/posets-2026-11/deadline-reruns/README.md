# Deadline checks and final Acacia comparison

The final comparison gives no convincing reason to replace Acacia's ranked-vector backend with lazy-FC KD. Both solve the same 1,368 of 1,524 cases at 17 seconds, and the same eight additional cases in the conditional 30-second follow-up. The small timing differences do not establish a winner. Existing production optimizations are retained.

| Stage | Original ranked default | Retained lazy-FC KD |
|---|---:|---:|
| 17 seconds, full 1,524 cases: solved | 1,368 | 1,368 |
| Total PAR2 seconds | 5,946.655226 | 5,940.166345 |
| 30 seconds, 156 prior timeouts: solved | 8 | 8 |
| Conditional PAR2 seconds | 9,054.294433 | 9,054.435914 |

See [REPORT.txt](REPORT.txt) for conclusions, the Spot-error interruption, and limits. [source-and-protocol.json](source-and-protocol.json) identifies the actual configurations. [final17](final17/) and [conditional30](conditional30/) contain separate tables and evidence; their denominators and scores are not pooled. [numerical](numerical/) contains raw native/resource tables with source hashes and extraction verification. [reviews](reviews/) preserves independent reviews with their original, bounded scopes.

The 450-call historical deadline check did not reproduce a serious loss in the optimized ranked default. A matched regular-sharingtree check on Alarm solved 0/5 runs with old query maps and 5/5 with retained generation stamps. The map observations are censored, so there is no timing speedup ratio. These checks do not attribute all differences between tree and ranked backends.

All full Acacia runs here use 16 signed-char SIMD lanes (128 bits), the shipping `otf_sparse_formula_parity` portfolio, empty flags, 8 GiB and no swap. `-march=native` does not override Acacia's explicit vector-width setting. Separate 64-lane TRAIN replay results were mixed; they are not native-width full-solver results. Further width work is deferred to [Acacia issue 380](https://github.com/gaperez64/acacia-bonsai/issues/380).

The earlier KD-round-2 package's `789` baseline is the **previous ranked implementation**, not the original `e167` control used here. Earlier package bytes remain unchanged. Implementation `d363b9f` passed its recorded CI; this is not a claim about a later evidence-only commit. Publication history belongs to the containing Git commit and [PR40](https://github.com/michaelcadilhac/posets/pull/40).

Exact copies retain historical absolute paths and bytes. `origins.json` names their sources and labels explicit JSON projections with every omitted pointer. Historical pending wording in copied preparation/review records describes their original stage, not current completion. `RECREATE.txt` separates recreation instructions from executed historical code. Only redacted original-checkout preservation proof is included; no WIP contents, process environment dump, executable, corpus or cache is bundled.

`MANIFEST.json` inventories package payload files. `SHA256SUMS` covers that payload and the manifest; it excludes itself. `STATIC-CHECKS.json` records bounded assembly checks, not new solver tests. Full private build/input/invariance graphs remain externally hash-referenced; their presence here is not promised.
