# Selected-rank replay tools V2 — static correction draft, not executed

These are task-owned copies for the prospective default-rank follow-up. Original
Acacia, nested WIP, phase-one scripts/sources/ELFs and captured results are never
edited by this draft. No copied/new helper or test was imported or executed to
prepare it. No source clone, build, map, measured revision, anchor or result was
manufactured. `source-manifest.json` inventories actual tool source files only.

Core V3 now has completed local assertion/exception/Memcheck/owned-file style
gates and independent approval; its actual CI18 gate is still pending. This
helper V2 snapshot is source-only and still needs both reviews, a separate
explicit runtime release, offline integrity checks and actual build/ELF pilots
before replay. Phase-one completion does not authorize those runs.
Writing a plan or reading these commands is not permission to execute them.

## Narrow opt-in

Without `--rank-only`, generator/runner/loader inventory remains the original
exact 48 entries: all24 backends × compatible/native. Legacy selector-overlay
source checks remain on that branch. The default combined path also remains.

With `--rank-only`, build plans and maps declare
`inventory="rank-compatible-native"`, exactly two ordered entries for
`rank_bucketed_vector_backed`, compatible then native. The builder, loader,
paired writer, plan, both variant maps and every replay invocation must agree
explicitly. The original two `VECTOR_TYPES` remain in each singleton ELF.
Scalar qualification runs once; clean timing retains the two effective SIMD
widths. Max is an alias only after the existing actual full-probe proof.

Public789 already contains the corrected V2 selector registration. Selected mode
therefore does **not** invent a selector-only source commit or an applied overlay.
It records each actual clean Git HEAD and an explicit `baseline`/`candidate`
source role, with common base `789bf99346d41a30f1b454ff58385b6f8a2961a9`.

`rank_contract.py` pins the independently approved Core V3 source manifest
`93e77732baec145529cd48fece48647db7e4312ee3c296751aa440536d260f02`
and patch `889f94fef69e5dcb263e2370c4b9b5b96cd542df6cea4294cab997affc97461d`.
Baseline HEAD must be exactly789. Candidate's net changed paths/bytes must be
the reviewed rank header and three bounded test/build files. Kernel, CLI,
registration and oracle helper bytes remain789. The base include tree must be
byte-equivalent to productioncc; this is a proof about the baseline, not a
claim that candidate headers are unchanged. Source/Git/common directories must
resolve inside the isolated Acacia `_deps`, and approved source files are regular.

Root committed the identical reviewed Core V3 bytes as the actual source revision
`30e66853832d98bcfaada296d892157b95a8150d` after the saved gates; baseline
remains789. This pin is source evidence, not a release build/map/ELF claim. Those
artifacts still require the later scoped, matched build and pilot sequence.

If later runtime/style fixes require another candidate freeze, root and reviewers must
approve a new source-manifest binding and update this explicit policy. Refreshing
untrusted map/record/source hashes alone cannot enroll different source bytes.
This policy is deliberately experiment-specific, not a general subset API.

## Retained proof boundaries

The copied loader still checks real ELF magic, actual singleton backend metadata,
selected width, SIMD type/size/alignment, compiler, runtime libraries and emitted
comparison-kernel instruction hash. It inspects the original compatible/native/
max probes and proves native=max; it never creates a fake max ELF or full24 map.

All original compile/link template normalization and effective argv checks,
Ninja/database bindings, real scoped compile/link invocation/spec/log/helper
bindings and memory limits remain. Cross-variant checks retain compiler/width
context and additionally compare selected link templates and runtime-library
contexts. The source oracle is the original combined-compatible ELF built for
that **actual** source revision. Historical7c/cc ELFs are not relabelled.

The replay function/kernel/timers are unchanged. Clean selected invocations keep
source/check/timing caps30/2/2 seconds, rounds/repetitions1, 8G/no swap, AB/BA/AB
and seeds260617–19. Both controllers preserve source cleanliness, no-overwrite,
shared host lock/scope refusal, common-cell order auditing and all failure/censor
evidence. Selected plans use the stop-on-correctness controller directly.
The summarizer changes only the declared backend cardinality on the opt-in path;
pairing, ratios, missing data and censor treatment are retained.

`POSETS_RANK_STATS` must be undefined in clean selected builds; defining it as0
still enables instrumentation and is rejected. No profiling wrapper or timing
hook is added here. A future instrumented pass needs separately reviewed sources
and truthful noexcept-move/size traits, not a loophole in this clean-source policy.


## Corrections from preserved helper V1

The original `../tools/` bytes remain intact. `v1-to-v2.patch` exposes every
correction; `changes.patch` retains the full delta from the original phase-one
sources. `v1-snapshot.json` and `original-sources.json` preserve both levels of
source/dependency authority. Neither is a build/map/result manifest.

Selected paired invocations now accept one explicit canonical option schema:
each required full option occurs exactly once with one value (or an exact flag),
no extra positional tokens, equals-form aliases or abbreviations. This closes
Nbjorner's appended `--seed=7` counterexample and equivalent source/map/output
path overrides before launch. The parsed values, rather than another literal
index lookup, are compared with the plan. Both controllers also require the
actual JSON report seed to be an integer equal to the declared run seed.

Offline sources add canonical/effective-option negatives, refreshed invocation
bindings and strict reported-seed negatives. Direct paired-writer fixtures first
accept two matching contexts, then independently change a selected linker flag
or runtime library context and refresh all affected child/outer bindings; each
must reach the final cross-variant context guard. These fixtures use real temporary
source-authority Git identities but synthetic saved compiler/library metadata;
they do not execute a compiler, ELF, replay or controller main. The latter
controller call sites are reviewed statically; their common reported-seed guard
is exercised directly. Actual scoped pilots remain required.

No C++ source/kernel, replay function, scheduling, flags, metrics, cap, censor
policy or default full48 path changed for this correction. No helper source was
imported, collected, compiled, linted or executed while preparing V2.

## Dependencies and future sequence

`original-sources.json` records every copied source's original hash and the
unchanged shared dependencies. `campaign.py`, `scoped_task.py`, `benchlib.py`
and `scope_memory.py` are referenced at their existing task-owned locations.
The tools use the existing isolated Acacia as cwd/support provider; they do not
copy/rebuild any original dependency tree. All executable steps belong in the
existing 8G/no-swap wrapper, serially, with no concurrent build/profile/timing.

1. Preserve the completed `../validation-results-v2/` and `../validation-results-v3/`
   evidence and approved `../candidate-draft-v3/` source freeze. The existing fresh
   baseline/candidate roots are under isolated `_deps`; verify their actual clean
   Git/common directories and exact reviewed bytes again before any build.
2. Keep baseline detached at789. Root owns committing only the four approved
   candidate paths and reporting the actual Git HEAD. Read that actual pin; never
   invent a revision
   or call789 "cc". No selector overlay is applied again. Keep all source/build
   and failure evidence in fresh paths, including any revised approval.
3. Configure matched clean release `build-replay-release` trees inside both
   sources using the same recorded O3-or-Ofast/LTO/native-ISA recipe and compiler.
   Build the three **original full24** matrix targets at `-j1`. Do not reuse the
   debug validation environment or enable rank statistics.
4. For each role, call the copied `freeze_replay.py --rank-only --source SOURCE
   --revision ACTUAL_HEAD --rank-source-role ROLE --rank-source-manifest APPROVAL
   --output NEW_ANCHOR`. Outputs must be fresh isolated `_deps` directories,
   separate from source and historical7c output. They contain actual three full24
   probes and actual-revision provenance, not a two-entry map. Selected mode
   avoids the legacy historical7c comparison; paired fairness is proved below.
5. Call `prepare-per-backend-build.py --rank-only --source SOURCE --base-revision
   789bf99346d41a30f1b454ff58385b6f8a2961a9 --rank-source-role ROLE
   --rank-source-manifest APPROVAL --anchor-provenance NEW_ANCHOR/provenance.json
   --output SOURCE/build-replay-release/NEW_SELECTED_DIRECTORY`, then
   `run-per-backend-build.py --rank-only ACTUAL_BUILD_PLAN`. Only two real ELFs
   per side are required. No map is emitted before both successful compile/link
   records exist and validate.
6. Perform actual loader/semantic pilots before timing. Use the copied driver
   with `--rank-only --backends rank_bucketed_vector_backed`, each actual map,
   source and anchor, fixed trace plan and explicit30/2/2 caps, rounds/repetitions1
   and 8G. Run correctness without `--measure` first, retain failures, verify
   runtime backend/width metadata and confirm all source/ABI/build evidence.
7. Create a fresh scoped command template (explicit future `--template` input);
   retain the original30/2/2 caps, both vector flavors, rounds/repetitions1 and
   clean flags. Do not rewrite the old phase-one template or its bindings.
   `prepare-paired-replay.py --rank-only --template NEW_TEMPLATE --baseline-map
   BASELINE_MAP --candidate-map CANDIDATE_MAP --trace-plan DECLARED_PLAN --prefix
   FRESH_PREFIX` checks both actual source roles, all2-entry inventories and
   cross-variant contexts, then writes six bound specs and a stop-controller plan.
8. Root freezes/reviews actual artifacts and assigns the timing lane. Execute
   the copied stop controller and summarizer only then. Standard12 and larger5
   eligible TRAIN are already seen data. Prior held-out names also remain seen
   regression/replication data, never fresh validation. No solver campaign,
   corpus adaptation, hardware-counter run or production default change is
   authorized by these helper sources.

The commands above use metavariables for artifacts that do not exist yet. The
helpers derive actual revisions, metadata and binary identities when executed;
this directory intentionally contains no example measurement map with fake data.

## Offline checks drafted, not run

The copied `test_posets_matrix.py` retains the existing full48 integrity,
source/parser/dispatch/scheduler tests. New tests cover the explicit opt-in,
missing/extra/duplicate/backend/width/source cases with refreshed bindings,
singleton/runtime-width mismatch, effective compiler flags/ISA changes,
source-authority refresh rejection, symlinks, clean-stat definitions and both
variant maps/plan/invocation/caps/seeds before the first paired invocation, including
noncanonical effective option overrides and actual reported integer seeds. Direct
writer context coverage is described above.

Loader tests use clearly synthetic ELF/Ninja observations to exercise the
unchanged downstream proof chain; their source-policy stub is isolated from
separate tests of the real source validator using temporary Git identities and
a deliberately synthetic test authority. They are not real build/metadata
evidence. Actual scoped build/ELF/pilot gates remain mandatory after source review.

After explicit release, run the file through an existing scoped spec with a
local installed pytest, `python -B -m pytest --noconftest -p no:cacheprovider`,
and the exact file path. Set the preserved pre-map driver reference environment
for legacy parity checks. Run configured static lint separately; retain first
failures/new attempt records. No test collection/import, syntax compilation,
lint, helper, Git-fixture model, compiler or benchmark was run for this draft.
