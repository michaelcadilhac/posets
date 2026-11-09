# Recreate in new isolated directories

These are derived portability instructions, not a transcription of executed
historical commands. They do not rewrite the frozen plans, paths, hashes or
source pins. A new build/run has new provenance and output paths. Do not point
these steps at an existing working checkout or its nested worktrees.

## Sources and dependencies

1. Verify this package from its directory with `sha256sum -c SHA256SUMS`.
   `inventory.json` explains which files are exact copies and which are derived.
2. Choose an empty study directory. Recreate the sibling layout below; the
   preserved scripts resolve their repository/helper paths using this layout:

   ```text
   STUDY/work/acacia-campaign/       # scripts/acacia-campaign/* copied here
   STUDY/work/acacia-posets-eval/    # fresh candidate Acacia clone
   STUDY/work/acacia-posets-eval/_deps/anchor-acacia/  # fresh pristine clone
   STUDY/work/acacia-posets-eval/_deps/...           # replay/dependency clones
   ```

3. Clone Acacia from `https://github.com/gaperez64/acacia-bonsai.git` into the
   candidate and pristine directories and check out
   `86dd487001dc75a3c1c0dfa9db50a9882a532db1`. This public URL comes from the
   original repository's verified remote; availability of that commit over the
   network was not independently fetched during package preparation. A source
   archive/mirror must expose the same commit if it is unavailable upstream.
4. Initialize submodules in these fresh clones. Apply
   `patches/acacia-integration.patch` with `git apply --index` to the candidate
   only, from its root, and check its Posets checkout out at
   `cc058ca96ebc667f727ea2f0c2360b77d60192d3`. The patch stages the Posets gitlink
   change too; ensure actual submodule bytes match it. Keep the anchor Posets at
   `e167f43646b880c69046bab7110dbb327686c024`. Obtain missing objects from a
   verified Posets remote/mirror, not from a mutable user's working tree.
5. Follow the pinned Acacia build instructions for dependencies. The candidate
   provenance records TLSF-tools `2debd5fc87e24de2ae0fb6008b3f7a7779432ff3` and
   OxiDD `288a6efcf553f18fe4bf722635157f2453bb2618`, compiler identity, library
   hashes and actual include/link paths. Use a common dependency stack for all
   arms and record the new resolved paths/hashes. This package contains no
   dependency installation or offline Cargo cache. Populate dependencies before
   using the recorded `--wrap-mode=nodownload` setup.

The historical environment file contains task-local library/Cargo paths.
Create a **new** environment file pointing to newly isolated dependencies; do
not reuse it unchanged. Never configure/build inside original repositories.

## Build and validate the solver arms

`recreation/fresh-candidate-setup-argv.json` supplies derived fresh setup argv.
Its deliberate difference from the historical initial setup is
`-Dbuild_tests=true`; the final executed reconfiguration and fresh introspection
already use true. The old initial setup file is included only as historical
evidence. Keep diagnostics false, the shipping preset/runtime portfolio,
compatible128 selection and the matched release flags. Do not introduce private
inlining attributes or extra global compiler/ISA flags.

Create new scope specs using that command, a `cwd` inside the new isolated
Acacia checkout, a new environment file, and a sufficient timeout. The existing
helper executes each spec under 8G/no-swap:

```sh
python3 -B STUDY/work/acacia-campaign/scoped_task.py NEW_SPEC.json NEW_RECORD.json
```

Use Python without `-O` or `PYTHONOPTIMIZE`: protocol validation uses assertions.
All build/check/run commands belong inside the existing scoped infrastructure;
metadata-only inspection is separate. Do not overlap compilation/checks with
recorded timing. Build at `-j1` for the measured solver setup. The actual final
configure/build specs and records are in `evidence/acacia/`; the complete source
and compiler/link argv are in the proposal build artifacts.

After configuration, derive targets and unit dependencies from **fresh** Meson
introspection, rather than the obsolete ten-target inventory. Build all 14
candidate targets plus required unit targets, and the separately configured
pristine anchor. Preserve actual compile commands and runtime library bindings.
The fixed list of candidate target names follows directly from the patch and
the arm labels in README.md.

Before any timing, repeat the final gate categories: G0/unit suite, version and
configuration-frontends checks, CLI checks, four Boolean probe build modes,
and known REAL/UNREAL forced-backward smokes for each of the 15 configurations.
`evidence/acacia/checks-summary.json` records the original 87 unit checks,
version check, 112 Boolean cases and 45 CLI checks and their exact locators.
Release `NO_VERBOSE` limits route logging; source/type/config bindings and
actual verdicts complement the forced-route requests. The tiny Boolean probe
does not exercise lazy frozen catalogs; use the dedicated Posets lazy/FC tests
for those internals. Bind the probe's actual included/generated headers,
compiler argv, dependency inventory and binary hashes in the new records.

Record new `-V`/configuration output, source/patch pins, ELF hashes, compiler
commands and shared/static library identities. Generate a **new** arms manifest
with exact 15 labels, empty flags and shipping preset; do not relabel old ELF
paths or treat the copied proposal JSON as a ready-to-run manifest on another
host. The official launch manifest here identifies the original execution only.

## Primary corpus and campaign

Materialize the exact 1524 native mapped SYNTCOMP26 identities using the pinned
Acacia list/map/exceptions and native TLSF corpus procedure. This materialized
native corpus is an external prerequisite; its manifest and verification are
referenced in the provenance.
Supply explicit new `--list`, `--tlsf-map` and `--tlsf-corpus` paths. The historical
default corpus path names the original user's read-only checkout and is not a
portable default.

First run `campaign.py` with `--dry-run`, then execute/resume the same command
without that flag. Supply the new `--repo`, `--arms` and `--output`, seed 260617
and batch size 32. Keep flags empty, 17s/8G/no-swap and serial scope ownership;
do not add CPU quota/allowed-CPU overrides. The controller handles the shared
host lock, scope cleanup, native known-status/cross-arm correctness stops,
resume and strict-wall scoring. Its existing `README.txt` documents these
contracts. Its `selfcheck.py` exercises bounded mocked scheduling/failure cases.

Run `--summary-only` only once every arm has all 1524 rows. Preserve native
answers beyond the scoring wall cap in correctness checks; the derived scoring
view changes only the scored outcome. Do not remove failed/OOM/timeout rows.
Run `make-final-table.py CAMPAIGN OUTPUT` only after the new campaign is complete;
it derives both predeclared comparators. For the historical phase-one campaign,
this postprocessor has already executed. Its reviewed comparison and completion
evidence are in `evidence/acacia/final-primary/`.

## Replay and supplemental compilation

Identical replay requires the original hash-matching captured TSVs and their
metadata; see EVIDENCE.md. Newly captured inputs constitute a new experiment.
Keep the original source-oracle and complete-frame eligibility semantics.

For a combined recreation, configure the original replay source at each exact
production pin using matched width/compiler flags, retaining its provenance.
First build all three original combined baseline targets in the clean source's
`build-replay-release` directory. Before mocked controller checks or per-backend
build generators, run the included freeze helper through a new scoped spec
with this argv:

```sh
python3 -B STUDY/work/acacia-campaign/freeze_replay.py \
  --source NEW_BASELINE_SOURCE \
  --revision 7c407128326583abbb137fd3832857ed506e20fb \
  --output STUDY/work/acacia-posets-eval/_deps/replay-baseline-7c40712
```

`NEW_BASELINE_SOURCE` must be clean at that exact revision, with the release
compatible/native/max binaries and Meson build metadata already present. The
output directory must be absent. The resulting
`STUDY/work/acacia-posets-eval/_deps/replay-baseline-7c40712/provenance.json` is
the baseline prerequisite consumed by the copied controller selfchecks and
the anchor for subsequent build generators. Keep historical files unchanged.

Next recreate `STUDY/work/acacia-campaign/pair0-baseline-spec.json` from
`evidence/replay/spec-template.json`, using new absolute source/build/trace/output
and environment paths with the same caps/rounds/method. Both the frozen baseline
provenance and this fresh template must exist before the mocked controller
helpers and paired-plan writer. These are new records, not relocated historical
bindings.

For supplemental per-backend compilation, create separate clean measurement
clones of `7c40712` and `cc058ca`; apply only the exact selector V2 overlay to
each and commit locally. Do not use the public harness revision as an unlabelled
replacement for these measurement sources. The recorded local V2 commits and
their production bases are in `per-backend-v2-measurement-sources.json`.

The existing `prepare-per-backend-build.py` accepts `--source`, `--base-revision`,
`--selector-overlay`, `--anchor-provenance` and `--output`. It derives compile/link
templates and selector specs. `run-per-backend-build.py NEW_BUILD_PLAN.json`
executes those specs serially; `--backend kdtree_backed` is the bounded pilot.
Full maps require all 24 backends × two effective widths per pin. Preserve
original combined-compatible source-oracle ELFs. The patched matrix loader
verifies singleton dispatch, all24 coverage, templates/flags, source/hash/library
bindings and native/max alias proofs, rather than trusting a map declaration.

`prepare-paired-replay.py` accepts either `--candidate-build` or both
`--baseline-map`/`--candidate-map`, plus `--trace-plan` and a fresh `--prefix`.
It uses `pair0-baseline-spec.json` as its command template: recreate that
template in the prerequisite order above, not with the old absolute paths.
Preserve the generated plan, and create a separately named
derived plan binding `run-paired-replay-stop-on-correctness.py` as controller.
Only the controller binding changes; keep inputs, checks, order and caps.
Execute the derived plan through the established host/scope protocol.
`summarize-paired-replay.py NEW_PLAN.json` derives tables without launching
measurements. The controller stops on the first rejected/malformed report;
source failures cannot be silently filtered into an apparently valid timing.

Before measurements, the supplied bounded `check-per-backend-preparation.py`,
`check-paired-map-adaptation.py`, `check-paired-correctness-stop.py` and
`check-saved-frame-audit-fixtures.py` test the task controllers. Run them through
new scoped specs. The patched Acacia pytest suite tests map/provenance/dispatch
and negative-integrity cases. These do not replace semantic checks of the real
ELFs. Never run timing concurrently with these checks or builds.

## Recreating the separate default-rank comparison

Use exact baseline `789bf99346d41a30f1b454ff58385b6f8a2961a9` and candidate
`30e66853832d98bcfaada296d892157b95a8150d`. Public `789bf993` already contains the corrected
single-backend registration; do not reapply the old phase-one selector patch.
The `default-rank/sources/tools-v2/` sources describe the explicit rank-only,
compatible/native two-ELF inventory per side, while preserving both original
Vector types and the unchanged captured-operation kernel. Original combined
actual-pin source oracles, semantic 30/2-second and clean 2-second caps, all censors and matched
flags remain required.

Copied helpers retain their actual task-layout/absolute bindings. Treat them as
exact historical source recipes, not portable launchers: use a new isolated
workspace/layout and separately reviewed derived commands, never silently rewrite
historical specs or replay the original paths. The core patch/manifests, actual
plans and recorded compiler identities describe the completed treatment.

Profiling source lives separately under `default-rank/sources/profile-draft-v2/`
and the accepted projection source. It enables statistics/allocation hooks and
keeps shipping representation/noexcept-move traits; it is never a clean timing
binary. Its CLI is ELF TRACE VECTOR operations|roi, with actual source AND exact
backend/vector/width qualification required before workload specs. The work/heap and PMU runs are complete; their instrumented clocks were discarded.
Exact PMU source and command/recipe/schedule freezes are under
`default-rank/sources/pmu-draft-v2/` and `pmu-plan-v1/`; the actually used owner
executors are separately preserved. Their original manifest references can
include unembedded private artifacts/specs. They are source recipes and evidence
bindings, not self-contained portable launchers. The completed publication panel
retains numerical phase pairs, all raw PMU values, both estimators and all censors.

A new recreation must preserve actual-pin plus exact backend/vector/width clean
qualification, original operation/ROI lifetimes, wrapper/trait selfchecks,
30-second instrumented cap, and matched source/build context. PMU uses the frozen
four-cell/three-pair CPU-2 user-only grouped treatment; do not mix it with clean
elapsed timing or silently add cells/events. Retain denied/multiplexed/missing
states rather than scaling or replacing them with zero. Original private traces,
ELFs/dependencies and their reviewed bindings are still needed for captured-input
replay. Do not silently rewrite historical absolute paths or call a local locator
a public download.

The separate two-arm solver campaign is complete, including the actual native
loss and negative solved/PAR2 comparison. Exact reconstruction patches for its
baseline and candidate Acacia trees are under `default-rank/patches/`; they bind
the actual 789bf993 and 30e66853 Posets pins. Do not substitute the phase-one
binaries, pooled rows or independent private Rust artifacts and claim identical
non-rank binary bytes. Keep the shipping preset, empty flags, 17-second strict
wall cap, 8 GiB/no swap, source/configuration/library bindings and unchanged
summary-only/immutable-input checks.

`default-rank/sources/solver-table-draft-v1/` is the unchanged used two-arm deriver
and its fixtures. It requires exactly 1,524 cases per arm, approved frozen hash,
complete usable summary, matching native/scored/sidecar identities and unchanged
strict scoring (all unsolved cost34s). It audits kernel OOM events independently
of native labels and never relabels/rescores them. Final exact table/CSV and both
reviews are embedded. The original numerical summary remains external; its
explicit no-host-details projection/provenance is separately named.

The compact default numerical archive and verification are embedded, alongside
the exact owner-used archive adapter source. It contains four native/memory TSVs
and six members; fresh extraction verified bytes/hashes and 3,048 rows per kind.
The full invariance map, raw logs, source/build trees, ELFs, caches, environment
files and captured traces remain external locators. No original WIP payload is
provided. The final compact preservation proof states its ignored/untracked-byte
limit. Final hardware context uses selected saved fields, without a fixed-clock
or full-host-detail claim.

For assembly, the unchanged durable updater and five passed synthetic fixtures
are preserved as historical source recipes under `default-rank/sources/package-refresh/`.
They require a reviewed hash-bound allowlist in an appropriate isolated task
layout, a fresh metadata backup and explicit `--apply`; do not run copied recipes
against old absolute paths or original checkouts. Package assembly only copies
already verified archive bytes; it creates/extracts no archive and launches no
build, solver, profile or measurement. Preserve all previous exact copies and
review the final inventory/checksums before publishing.
