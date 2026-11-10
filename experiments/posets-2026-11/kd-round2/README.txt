KD round 2 is complete. The retained legacy scratch change improved many replay
cells but did not beat either ranked default in the full Acacia campaign. The
ranked backend remains the default. All maintenance, catalog-layout and leaf-filter
experiments from this round were rejected; their patches and negative results are
preserved.

Read REPORT.txt for the full decision and limits. outcomes/solver-comparison.json
and .csv contain the exact three-arm totals and comparisons. Over 1,524 cases per
arm, the original default solved 1,368, the optimized default 1,367 and legacy KD
1,365. KD gained no cases and lost three against the original default. Those three
losses were TIMEOUT results without OOM-kill events. Seven separate KD runs had
OOM-kill evidence: two native MEMOUT and five native TIMEOUT, all common-unsolved
cases. The kill-counter sum of 15 is not an invocation count.

timing-rows.tsv retains all 478 replay metric cells: 460 paired and 18 censored.
Each paired cell has three observations. Ratios are medians of paired ratios,
not ratios of endpoint medians. Original summaries under evidence/summaries are
exact copies. Some historical scope sentences name the wrong source bases;
source-and-protocol.json and the actual-arm record identify the bound revisions.
All these replay inputs were previously seen TRAIN data.

patches/ contains the retained legacy change and rejected experiments, with each
patch's starting and ending revision recorded in the identity map. The filter
patch applies after the packed-catalog patch; the complete layout/filter patch is
an alternative. The round-2 SIMD experiment uses leaf blocks/coordinate filters
on the existing lazy KD engine, not a new block-vector branching topology.

profiles/ contains the complete small work/heap phase table and findings. The four
profiled lazy variants made 10,149,479 full-order attempts on the quoted CPre frame,
versus 5,376,016 and 5,273,875 for the ranked controls. Offset-only was not profiled.
Known-retained values include diagnostic statistics and exclude opaque Vector
heap; they are not shipping sizeof values or total backend heap. No new PMU or
cache-miss measurements were collected, and no cache cause is inferred.

acacia/ contains compact build, gate, release, launch and preservation evidence.
Exact records keep their historical pending wording. The current completed results
are in FINAL-RESULTS.json. Nb reviewed completed gates/release and final outcomes;
Barbosa reviewed preparation source/specs, reporting source, and final outcomes.
Their final outcome approvals do not approve final publication prose. Only redacted
preservation proofs are copied; raw original status/diff/WIP details stay private.

Two metadata projections are explicitly labelled: the final summary omits only
/cpu_host and the handoff omits only /bindings. Projection metadata binds their
unchanged external originals and exact omissions; projections are not exact copies.
The earlier final-review-checkpoint projection follows the same documented rule.
The failed first invariance audit and its executable-selector correction are
preserved; no acceptance policy or solver result changed. The large corrected
binding inventory is external and hash-bound.

outcomes/archive/acacia-kd-three-arm-numerical.tar.gz contains the compact raw and
strict-wall numerical evidence. Its member manifest, member checksums and completed
extraction verification are alongside it. Member checksums apply after extraction;
the top-level SHA256SUMS verifies this package's files, including the archive.
No ELFs, build caches, corpus, unrestricted environment snapshot or raw process
logs are included. The frozen numerical campaign manifest contains only the
LD_LIBRARY_PATH environment binding. External raw/build records remain referenced
by their historical paths and hashes; reproduction requires those retained inputs
and dependencies. Relative paths in exact copies resolve against origins.json.

Implementation d363b9f passed push CI run 42729670700, including all three jobs.
That green claim does not cover a later documentation/package commit. The earlier
15-arm and focused-default campaigns are separate evidence and are not pooled with
this three-arm campaign. Different private static Rust archives limit causal
attribution to the KD change alone; NO_VERBOSE does not reveal route frequencies.

MANIFEST.json and SHA256SUMS use paths relative to this directory. Run
`sha256sum -c SHA256SUMS` here to verify the files. All result data and root final
prose/package review are complete; the package is ready for publication. Publication
history is recorded in the containing Git commit and PR 40. This package makes no
claim about CI for a later evidence-only HEAD. The old experiment package, its
README and its inventory have not been modified.
