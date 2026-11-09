# Evidence access and limits

Copied source/evidence bytes retain their original absolute path bindings.
`copy-origins.json` maps each packaged file to its original task locator and
SHA256. `inventory.json` and `SHA256SUMS` bind the package itself. Paths and
hashes identify data; they do not establish a public download service.

The original task workspace retains raw reports, captured traces, invocation
logs/specs, binary maps, source/build snapshots and binaries at the locators in
the manifests/digests. The completed phase-one numerical evidence is embedded
as `evidence/acacia/final-primary/phase-one-native-evidence.tar.gz` (SHA256
`53ecde5845352ffcef89a3c438f7495ab7df5312ed675c282860d29a26b82fbd`).
It contains all 22,860 native result rows and all 22,860 memory-sidecar rows,
with a per-file inventory. Fresh extraction was verified separately. Other raw
artifacts still require access to their recorded task-local paths; there is no
external download service implied by those locators. Exact captured-workload
replay is not possible from this compact package alone.

Excluded artifacts include all ELFs/objects/build directories, .git/source
clones, offline dependency caches, corpora and large raw trace/report/log trees.
The 45 MB `liboxidd_ffi_c.a`, large target/test introspection snapshots and
multi-megabyte full replay summaries are referenced by the frozen manifests
instead of embedded. The complete compact held-out digests are embedded,
including original raw-report/plan locators and all censor counts. Primary
rows were copied only after completion, summary-only verification, final binding
checks and independent review. No growing primary file is included.

Original Acacia and all nested original WIP are outside the package allowlist.
The ten-file patch was checked against committed Acacia86dd; its files are
listed in `references.json`. No ignored/untracked checkout sweep was used.
The original-checkout guard compares HEAD, status, index entries, binary diff
and tracked file bytes, not all ignored/untracked filesystem bytes. Its helper
is included for provenance, but the user's WIP/baseline checkpoint is not.

Measurement distinctions that must survive derived reports:

- Combined24 and per-backend registry compilation are different compiler
  contexts. Supplemental compilation preserves both vector types and the
  kernel/timers; it does not replace the combined observations or solver tests.
- Whole-CPre, first construction and second fresh construction have different
  lifetimes/input cardinalities. The copied lifetime clarification governs
  interpretation. Replay `rebuild` is not an in-place lazy maintenance timer.
- Scope `memory.peak`, maximum process RSS, allocator requested/live/peak bytes,
  estimated vector payload and known index storage are different measurements.
  They must not be combined as interchangeable memory/RSS/cache residency.
- Incomplete saved CPre frames are preserved failures, not repaired inputs.
  The source parser correctly rejected the captured partial action/after frame.
  That first larger-TRAIN rejection triggered a structural audit of 191 saved
  frames. Derived complete-frame subcohorts were then frozen before fresh
  eligible repetitions and held-out runs. The original failed epoch, exclusions
  and original-to-derived IDs remain separate; eligibility used framing alone,
  without answer/timing selection, repairs, replacements or pooling of epochs.
- Source-oracle failures, invalid/malformed rows, timing censoring, missing
  resources and pair coverage belong in every comparison denominator. Exact
  paired ratios and separate medians are not interchangeable.
- The pristine anchor and candidate-default are separate comparators. Runtime
  portfolios, source revisions and build context limit attribution to a single
  downset. No default switch or universal speedup follows from this package.

The standard/large held-out digests and final policy freeze are immutable
completed evidence. The official-launch directory retains the original start
bindings; final-primary separately contains completed outcomes, completion and
invariance records, both audits and the numerical archive. Native failure labels
and kernel memory events are separate: 25 rows were MEMOUT, while 70 rows had
OOM-kill events (including 45 TIMEOUT rows). Each of the ten affected arms hit
the same seven cases; block16's six MEMOUT labels do not imply more OOM events
than the other nine arms. No labels or scores were rewritten. Original small
synthetic pilot outputs remain separately named historical evidence.

Package preparation performed only small static JSON/source/hash assertions.
It did not rerun selfchecks, builds, solver/replay timings or memory profiles.

## Default-rank evidence boundary

The `default-rank/` addition is separate from the completed phase-one cc/789
campaign. Its baseline is actual `789bf993` with cc-equivalent production headers and
its candidate is actual `30e6685`. The recorded exact-commit green CI is immutable historical
CI evidence; no later package HEAD is claimed green. Both source pins and build
contexts remain explicit rather than relabeled as historical 7c/cc binaries.

Clean replay is completed on the previously seen 12 standard and 5 larger complete-frame TRAIN identities.
Its80 fully paired metric cells and 6 source-censored metric cells include all
raw three-pair values, censor denominators and regressions. The compact TSVs are
embedded exact bytes; full task-local reports/builds/logs remain referenced and
are not made publicly downloadable by absolute locators. P (median paired ratios)
and V (ratio of variant medians) are distinct. No significance or general workload
estimate follows from three observations, and tiny windows retain their limits.

The work/heap and PMU companion is complete. Exact paired numerical data,
interpretations, source/plan/executor freezes and completed review ledgers are
allowlisted. It retains all 192 accepted work calls, 12 unprofiled pass records,
990 matched phase pairs and all 24 PMU calls/96 readings. Unqualified identities
stay unprofiled; longer instrumented success never upgrades a clean censor.

Both independent interpretations found identical tracked allocation/requested
heap measures, fewer rank/comparison attempts and some additional move
assignments. No captured no-op intersection or measured heap saving is claimed.
PMU cycle/instruction improvements coexist with collector L1D regressions and
no robust LLC benefit. All original clean timing regressions remain visible.
Instrumentation/pinning/compiler context and lifetime limits prevent substituting
these counters for clean timings or final solver evidence.

Large private artifact closures, the duplicated detailed profile plan, ELFs,
dependency caches, trace payloads, logs and environment blobs remain external.
Their original bindings/locators are retained; no absolute path implies a public
download. The one 998,115-byte full paired work-data addition has its own exact
hash/size exception rather than a broader size limit. Copied intermediate report
and source-freeze status text remains historical, including superseded next steps.

The two-arm actual-default campaign, numerical archive, final compact original-
checkout proof, outcome reviews and final readable reports are complete. The
baseline solves 1,369 cases versus 1,368 optimized, with no gains/one loss and
16.237269 seconds worse total PAR2. Preserve this negative outcome alongside the
component work/instruction reductions; no whole-solver benefit, repeatability or
precise causal attribution is established. Independent private Rust artifact
bytes differ despite matched source/dependency revisions/settings.

The numerical archive is exact independently verified data, not an ELF/corpus
archive. Its four native/memory TSVs and six total members retain all 3,048 rows
per kind. Native labels, strict scoring and OOM-event counts remain separate;
all event observations are present and zero in this follow-up. The unchanged
summary-only pass reproduced the automatic result. The 4,124,966-byte invariance
manifest (SHA256 `3a8c65130e68489d41342f4e9bbf13a916c97c5eb8a3deed532c5a51f64dc106`)
is external; compact exact handoff/audits retain its 14,981-file scope.

Original summary `495aad8b1f5101a90e4e96104dce9cfed946a924f71ca3ceab50fbfbef66a0a3`
remains external and unchanged. `summary-without-host-details.json` removes ONLY
top-level `/cpu_host`; all other parsed keys/values are preserved. Separate
projection provenance binds original/projection bytes and the selected hardware
context. This is explicit metadata minimization, not a byte-equality claim or a
numerical transform. The new exact frozen manifest's environment has only
LD_LIBRARY_PATH, explicitly approved; full environment/host files are excluded.
Historical earlier exact copies are untouched.

The compact original-checkout proof reports all five repositories and six match
booleans per repository with input/record hashes. It excludes raw before/current
status text and WIP filenames, and does not compare ignored/untracked bytes.
Final reviewer ledgers are exact outcome-stage snapshots; later package audits
are separate. Five updater fixtures passed under the existing lock/8 GiB/no-swap
wrapper; the updater/test bytes are unchanged from that tested V2. No package
application or new workload is performed by assembling this V3 source draft.
