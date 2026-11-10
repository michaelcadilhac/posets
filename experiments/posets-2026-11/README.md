# Posets / Acacia evaluation companion, November 2026

This directory preserves the small sources, patches, manifests and evidence
needed to audit the experiments associated with Posets PR40. It is an audit and
recreation companion, not a self-contained input or binary archive. See
[RECREATE.md](RECREATE.md) for a new independent run and
[EVIDENCE.md](EVIDENCE.md) for evidence access and measurement limits.

The phase-one production Posets revision is
`cc058ca96ebc667f727ea2f0c2360b77d60192d3`; the public harness revision at package
preparation is `789bf99346d41a30f1b454ff58385b6f8a2961a9`. The replay baseline is
`7c407128326583abbb137fd3832857ed506e20fb`. No library defaults or production
headers are changed by this directory.

The full phase-one solver campaign is complete: 22,860 runs, covering all
1,524 cases for all 15 configurations. The default and lazy fractional-cascading
KD tree solved the same 1,369 cases. Their PAR2 totals differ by only 0.072%;
this single campaign does not establish a performance advantage or justify a
default switch. Completed tables, both reviews and a verified raw numerical
archive are in `evidence/acacia/final-primary/`.

The subsequent default-rank optimization is a separate treatment, committed as
`30e66853832d98bcfaada296d892157b95a8150d`. It is absent from every phase-one
result. Its completed clean replay compares actual public `789bf993` (production headers
cc-equivalent) with actual `30e6685` on the same already-seen TRAIN inputs. It retains
80 fully paired metric cells and 6 source-censored metric cells, all regressions
and all three raw timing pairs. These are not fresh held-out observations or
solver-instance counts. Exact summaries, raw-cell TSVs and bounded interpretation
are in `default-rank/evidence/clean-replay/` and `default-rank/evidence/interpretation/`.

The recorded green CI run 42651196242 is for exact `30e6685`: four full suites each
82 passes, 1 intentional expected failure and 0 failures.
This is not a claim about a future package/documentation HEAD. Local validation
snapshots retain their original pending-CI wording; the later exact-commit CI
record is preserved separately. The completed instrumented work/heap and fixed
PMU companion is preserved under `default-rank/evidence/work-and-cache/`.
The separate actual-default Acacia campaign and final preservation proof are
complete. The observed solver result is negative: the optimized build solves one
fewer case. Final reports, both outcome audits and numerical evidence are under
`default-rank/reports/` and `default-rank/evidence/acacia/final-primary/`.
Package application/publication is a separate owner action.

## Contents and identity

- `patches/acacia-integration.patch`: the exact task-owned ten-file patch against
  Acacia `86dd487001dc75a3c1c0dfa9db50a9882a532db1`. SHA256:
  `98719f18ddfb296420da997e7655d32e3ba9b625c291a12b30c35f6394d93651`.
- `patches/posets-selector-v2.patch`: the identical measurement-only overlay for
  both replay source pins. SHA256:
  `2e19003c71b87dcb6dbafbe3eee99fa3e8fa6aec1d2fcf14904f3317a670d6b4`.
  It selects registration of one backend while retaining the original scalar
  and SIMD vector types. The replay kernel, CLI and timers remain unchanged.
- `scripts/acacia-campaign/`: exact task scripts, including the scoped runner,
  primary scheduler, paired replay controller with stop-on-correctness,
  summarizer, per-backend builder and bounded selfchecks. The patched Acacia
  replay driver and its tests are in the Acacia patch, not duplicated here.
- `scripts/historical/`: exact earlier combined-driver/controller/summarizer
  snapshots. These belong to their own hash-bound treatment; do not substitute
  them into later plans.
- `evidence/acacia/`: final checked build/configuration evidence, pristine anchor
  provenance, the original proposal, and separate official launch bindings.
  Proposal files intentionally retain their historical `pending-root-freeze`
  fields. The later `official-launch/` files are immutable approval, launch and
  freeze bindings only. Completed outcomes and the two reviewers' audits are
  in the separate `final-primary/` directory. That directory includes the
  2.6 MB raw numerical archive and its verified extraction inventory.
- `evidence/replay/`: frozen plans, source pins, real 96-ELF validation summary,
  source-rejection/eligibility records, and compact TRAIN/held-out digests with
  the original raw-report/plan locators and censor counts.
- `evidence/memory/`: measured memory/cache table and four-case cgroup lifecycle
  validation. `memory-lifecycle.jsonl` retains its terminal PASS text verbatim;
  it is not a pure JSON-lines stream.
- `probes/boolean-oracle-14.cc`: the exact integration probe source. Its 112
  configuration × vector-flavor × dimension cases preserve independent Boolean
  expectations; the final check summary records the executed four build modes.
- `recreation/`: explicitly derived instructions/data, not historical commands.
- `copy-origins.json`, `inventory.json`, `SHA256SUMS`: original locators and exact
  bytes/hashes of the packaged files. Original absolute paths are preserved.

## Frozen full-solver comparison

There are 14 candidate configurations and one separately built pristine anchor.
The candidate default is a common-update control, not the pristine anchor.

| Label | Candidate downset / optional treatment |
|---|---|
| candidate-default | `rank_bucketed_vector_backed` |
| kd | `kdtree_backed` |
| kd-narrow | KD with `POSETS_KDTREE_NARROW_INDICES=1` |
| trie | `sharingtrie_backed` |
| trie-compact | trie with `POSETS_SHARINGTRIE_COMPACT_NODES=1` |
| comp | `comptrie_backed` |
| comp-simd | comptrie with `POSETS_COMPTRIE_SIMD_PATHS=1` |
| block8 | `block_sharingtrie_backed<V,8,true,true>` |
| block8-uncached | `block_sharingtrie_backed<V,8,true,false>` |
| block16 | `block_sharingtrie_backed<V,16,true,true>` |
| lazy-fc | `lazy_kdtree_backed<V,true>` |
| lazy-search | `lazy_kdtree_backed<V,false>` |
| sharingtree | `sharingtree_backed` |
| simple-sharingtree | `simple_sharingtree_backed` |

All candidate configurations use the same optional shared core with `pic:false`;
`solve_game_vector.cc` and `version.cc` are the varying translation units. The
shared core does not instantiate the differing selected downsets. The anchor is
Acacia `86dd4870` with its bundled Posets
`e167f43646b880c69046bab7110dbb327686c024`, without the integration patch.

Primary runtime flags are empty. The compiled shipping preset is
`otf_sparse_formula_parity`, with compatible128 SIMD selection, 17 seconds per
instance, 8 GiB RAM, no swap, serial execution, seed 260617 and 32-instance
batches. No CPU quota or allowed-CPU override is supplied. There are 1524 mapped
instances per arm (22,860 completed rows). Actual compiler/link flags,
runtime libraries, configurations and ELF hashes are in the build manifests;
compatible128 does not imply that compiler auto-vectorization is disabled.

The reporting policy compares every candidate both with the pristine anchor and
with candidate-default. The pristine comparison includes the common Posets and
integration/build-context change. Single-campaign differences are observations,
not significance tests or a universal speedup claim. Portfolio execution can
take routes that do not use the selected downset. Separate forced-backward smoke
evidence consists of forced CLI route requests, source/type/configuration
bindings and actual answers. Release `NO_VERBOSE` suppresses backend-entry
traces; runtime entry, route frequency and per-primary-instance route attribution
remain unobserved.

## Replay treatments and status

Combined24 and supplemental per-backend registry compilation are distinct
treatments and must not be pooled. The supplemental builds apply the exact same
selector overlay to both source pins; each ELF retains both original vector
types. Original combined-compatible source-oracle ELFs stay hash-bound. Scalar
qualification, effective widths, native/max alias proof, paired seeds/order and
all failure/censor counts remain part of the saved protocol and digests.

The choice freeze is preserved at
`evidence/final-evaluation-choice-freeze-cc058ca.json`, SHA256
`7576e49ba0657726a94852d269e19c6384cd89312a3e308580f5f71d8968dba5`.
It precedes held-out execution. The standard and large held-out JSON digests have
SHA256 `2e96fc107a9b4f96205d49ca83f8dfd1e490d10e4df98d4fb9e18217ca80a975`
and `caf51b168248014b3f5ad1414e2ae45b39a756adaf93740f24abd38349d9fb9b`.

Package assembly uses static file/hash/JSON checks. It does not rerun copied
scripts or workloads. The numerical archive was separately created and verified
in the existing cgroup wrapper after the primary campaign completed; that record
is included. Historical validation outcomes belong to their saved records.

## Separate default-rank follow-up

`default-rank/core/` preserves the exact reviewed source/test patch and source
manifest. `default-rank/sources/` preserves exact helper/profile/test source
freezes with their original bindings; historical pending/status statements in
those source freezes are not rewritten. Completed validation/CI/review records
are separate evidence. The original 121 exact copies remain unchanged.

The clean estimator P is the median of three matched candidate/baseline ratios;
V is the ratio of separate variant medians. Median-ms endpoints must not be
presented as arithmetic for P. There are only three observations and one replay
round each. Retained regressions and tiny windows prevent a universal speedup
claim; the completed solver follow-up remains a separate treatment.

Build includes logical-to-V staging and backend construction. Replay rebuild is
a second fresh constructor while the first frontier lives; CPre rebuild uses the
captured after-frontier. Whole CPre includes bottom/mapped/combined/RHS temporary
destruction but leaves the final frontier alive. Per-call and whole-ROI contains
argument lifetimes differ. Requested heap, net retention, RSS and cgroup peak are
distinct. A profiler row with `has_result=0` has unavailable result descriptors,
even when raw placeholders are zero; it does not report zero retention. No
instrumented clock or PMU observation is pooled with clean timing.

## Completed work/heap and PMU companion

All 192 qualified instrumented calls completed; 12 qualification-censored pass
records remain unprofiled. The 96 matched records contain 990 paired phases,
including repeated vector/width and operations/ROI controls. They are not 990
independent workloads. Allocations, requested bytes, extra tracked live peak and
net live-byte changes match in every paired phase. All 822 observable result
size/capacity pairs match; 168 ROI descriptors are unavailable. There was no
captured no-op intersection, so its positive selfcheck supplies functional
coverage without demonstrating a captured memory saving. Rank/partial-order
attempts did not increase; move assignments increased in 267 paired phases.

The fixed PMU companion completed 24 calls: four cells, three pairs, two roles.
All 96 user-only event readings were usable with enabled time equal to running
time. User cycles and instructions improved in every pair in these four cells.
Collector L1D read misses worsened in every pair, and LLC observations were small
or variable; no general cache benefit follows. The full paired phase/counter data,
raw PMU events, separate estimators, completion records and both interpretations
are embedded as exact bytes. Instrumented clocks are excluded.

The earlier clean elapsed regressions remain unchanged and unexplained, including
native collector predecessor P=1.1910 and compatible round-robin construction
P=1.0777. CPU-2-pinned instrumented user counters and unpinned clean elapsed
clocks are different treatments; the PMU result neither repairs those timings
nor establishes their cause. No later source tuning was performed.

The two-arm solver source comparison uses matched source/dependency revisions
and build settings. Independently built private Rust archives have different
bytes: this does not establish identical non-rank binary bytes or isolate a
small solver timing difference. Final solver outcomes and artifact/provenance
bindings must retain that limitation. The first 15-arm campaign remains separate.

## Completed two-arm default comparison

| Arm / Posets revision | Solved / 1,524 | PAR2 total seconds |
|---|---:|---:|
| baseline-default / 789bf993 | 1,369 | 5,926.874862 |
| optimized-default / 30e66853 | 1,368 | 5,943.112131 |

There are no gains and one loss, `robot_repair5.ltl`: baseline returned
UNREALIZABLE in 16.287060812 seconds, while optimized reached an actual native
TIMEOUT at 17.117991324 seconds and incurred the unchanged 34-second penalty.
The +16.237269-second PAR2 difference (+0.274%) does not establish an end-to-end
benefit. This is one unrepeated campaign on previously seen cases, not a causal
rank-only regression proof or a universal slowdown. No retuning, selective rerun,
rescoring or backend switch followed the result.

All 3,048 CPU, scope-peak, process-RSS and memory-event observations are valid.
There are no OOM events, native MEMOUT/errors, truth conflicts or decisive wall
cap overruns. Both independent outcome reviewers accepted the negative result.
The same strict 17-second, 8 GiB/no-swap, empty-flags shipping protocol applies.

The verified 349,264-byte `default-native-evidence.tar.gz` contains four native/
memory TSVs, 3,048 rows per kind and six archive members. Fresh extraction matched
all bytes/hashes. The full 4.1 MB final invariance manifest remains external with
its original locator/hash; compact handoff and audit records bind 14,981 verified
files, including 1,211 prior phase-one files. The new summary projection omits
only `/cpu_host` and is explicitly named/provenanced, not an exact original copy.

Selected saved hardware fields match: Intel i7-11850H, eight cores/16 logical CPUs.
No primary affinity/quota override or fixed-clock claim is made; PMU used CPU 2.
The compact final preservation proof matches six recorded fields in all five
original checkouts, excluding ignored/untracked file bytes. Raw status text and
WIP filenames stay private. Earlier exact copies and intermediate publications
remain unchanged. Exact code30e CI success is not a claim about a later docs HEAD.
