# Experiment guide

For current library use, builds, and contracts, read the [main README](../README.md).
The directory [posets-2026-11](posets-2026-11/README.md) is the frozen PR40 evidence
package. Its README, source copies, tables, and manifests keep their original
bytes. This guide sits outside that package so navigation can change without
changing the evidence.

## Find a result

| Question | Saved record |
|---|---|
| What is in the package? | [Package README](posets-2026-11/README.md), [inventory](posets-2026-11/inventory.json), and [copy origins](posets-2026-11/copy-origins.json) |
| How can I reconstruct a run? | [RECREATE.md](posets-2026-11/RECREATE.md) lists the layout and external inputs. [EVIDENCE.md](posets-2026-11/EVIDENCE.md) explains access and measurement limits. |
| Where is the 15-arm solver campaign? | [Phase-one outcomes](posets-2026-11/evidence/acacia/final-primary/) |
| Where is the default-rank replay? | [Clean replay](posets-2026-11/default-rank/evidence/clean-replay/) and [interpretation](posets-2026-11/default-rank/evidence/interpretation/) |
| Where are allocation and PMU observations? | [Work and cache evidence](posets-2026-11/default-rank/evidence/work-and-cache/) |
| Where is the two-arm solver comparison? | [Default-rank reports](posets-2026-11/default-rank/reports/) and [outcomes](posets-2026-11/default-rank/evidence/acacia/final-primary/) |
| What happened in later KD work? | [Rejected round-two experiments](posets-2026-11/kd-round2/README.txt) and [deadline follow-ups](posets-2026-11/deadline-reruns/README.md) |

Each table belongs to its named campaign and revision. Phase-one, default-rank,
round-two, and deadline results have different treatments and denominators.
Keep those identities when quoting a row. Old CI counts apply to their recorded
commit, not the current checkout. A source draft can still say “not executed”
when a separate, later evidence directory records completion.
The default-rank native `raw/` tables are members of
`default-native-evidence.tar.gz`. Its four-entry `SHA256SUMS` applies after
extraction, not to loose files in the package directory.

## Read the historical commands

The archived commands describe their original task layout. They are not current
Posets maintenance tools. Some roots are computed from a script's old location;
others use private absolute paths or require external helpers, corpora, plans,
and exact source/ELF hashes. Merely substituting today's checkout does not
recreate the treatment. Use fresh output paths for an independent reconstruction.
The [recreation guide](posets-2026-11/RECREATE.md) explains the prerequisites.

The following errata cover every archived README. Paths in the first column are
relative to `posets-2026-11`. They record a static check of commands, arguments,
file names, and target identities against the saved parsers and current Meson
files. They do not claim that archived workloads have been rerun.

| README | How to read its commands and examples |
|---|---|
| [Package README](posets-2026-11/README.md) | The patch paths exist. The 14 candidate labels and pristine anchor describe the pinned Acacia treatment. `otf_sparse_formula_parity` is an Acacia preset identity, not a Posets CLI option. Current build commands are in the main README. |
| [Campaign scripts](posets-2026-11/scripts/acacia-campaign/README.txt) | The arms JSON is a schema example; use the build records for source identity. `work/acacia-campaign/selfcheck.py` assumes the reconstructed task root. `campaign.py` accepts `--arms`, `--output`, `--dry-run`, `--summary-only`, and `--help`. Supply explicit `--repo`, `--list`, `--tlsf-map`, and `--tlsf-corpus` paths; its defaults point outside the package. Summary-only needs a complete cohort. |
| [Deadline reruns](posets-2026-11/deadline-reruns/README.md) | There is no runnable shell example. Keep the full 17-second campaign separate from the 30-second, 156-case follow-up. |
| [Continuation draft](posets-2026-11/deadline-reruns/tools/continuation/README.txt) | `offline-spec.json` is present. The wrapper is `../scoped_task.py` relative to this README. `continue30.py` accepts either `--prepare ABS_NEW_DIR` or `--run ABS_AMENDMENT --release ABS_ROOT_RELEASE`; it has no `--dry-run`. These are templates, not literal paths. Completed outcomes are in [conditional30/summary.json](posets-2026-11/deadline-reruns/conditional30/summary.json). |
| [PMU draft](posets-2026-11/default-rank/sources/pmu-draft-v2/README.txt) | `POSETS_RANK_PMU_MODE` takes `check` or `measure`; `POSETS_RANK_PMU_PHASE` takes `build` or `cpre`. Use shell assignments such as `POSETS_RANK_PMU_MODE=check POSETS_RANK_PMU_PHASE=build taskset -c 2 ...` only with the pinned PMU executable and matching environment. The `--trace`, `--check=replay`, `--params`, and `--seed` text is an argument tail, not a standalone command. `--metadata` reports registry and width information. Completed data is in work-and-cache above. |
| [PMU plan](posets-2026-11/default-rank/sources/pmu-plan-v1/README.txt) | `scoped_task.py` takes two positional paths: the specification and output record. Its saved absolute path belongs to the old task. A relocated invocation has the form `python3 -B RELOCATED_HELPER NEW_SPEC NEW_RECORD`. The four recipes, eight build specifications, and 24 measurement calls are fixed plan counts. |
| [Profile draft](posets-2026-11/default-rank/sources/profile-draft-v2/README.md) | Compile `rank-profile.cc` first. Its executable takes `TRACE VECTOR MODE`, where vector is `vector_backed` or `simd_vector_backed`, and mode is `operations` or `roi`. The compiler recipe is a text skeleton: supply actual matched flags, shadow-header order, width, and link flags. The separate `rank-profile-check.cc`, `PROFILE_EXPECT_CANDIDATE`, and Valgrind allocator-hook option exist. |
| [Profile projection](posets-2026-11/default-rank/sources/profile-trace-projection-v2/README.md) | `PROFILE_ELF ABSOLUTE_TRACE VECTOR MODE` is a schema; pipes denote alternatives. `project_profiles.py` takes one output path. The unittest discovery recipe names the existing `test_project_profiles.py`; replace `THIS_DIRECTORY` with its package directory. Source and ELF guards still require the accepted historical inputs. |
| [Solver-table draft](posets-2026-11/default-rank/sources/solver-table-draft-v1/README.md) | The test file is packaged beside this README; the original absolute test path is historical. `make-default-table.py` takes campaign and fresh-output paths plus required `--frozen-sha256`. Its external support files and completed campaign are still prerequisites. Later results are linked above. |
| [Rank replay tools](posets-2026-11/default-rank/sources/tools-v2/README.md) | Freeze, build preparation, runner, matrix, and paired preparation options match their parsers. `--rank-only` selects the pinned two-entry inventory; it does not mean “all48 cells”. The matrix recipe also needs `--build-dir` and `--output`. `run-paired-replay-stop-on-correctness.py PLAN` takes one plan; `summarize-paired-replay.py PLAN [--output-prefix PREFIX]` takes that plan and an optional prefix. Spell the pytest runner `python3 -B -m pytest --noconftest -p no:cacheprovider FILE`; it needs pytest and the original support/driver references. |
| [KD round two](posets-2026-11/kd-round2/README.txt) | Run the shown `sha256sum -c SHA256SUMS` from `kd-round2`, since its manifest paths are relative to that directory. Rejected patches are preserved evidence, not active implementations. |

## Context for old implementation comparisons

Current headers include common fixes that older comparisons may predate. The
legacy KD query once accepted `(2,3)` against maxima
`(0,4),(1,3),(2,2),(3,1),(4,0)`. Crossing an already credited axis could count it
twice. Non-strict crossings require `old < query <= new`; strict crossings require
`old <= query < new`. Comparisons with `71918d6e113f33b9986968d9ae9c8ae5ea49ff27`
must account for this common fix.

The KD allocation changed to a complete-heap envelope. Compressed tries dropped
unused suffix colors. Sharing forests use generation stamps and retain historical
nodes; child-buffer growth, hash shifts, and union temporary lifetimes also had
fixes. The blocked trie and lazy KD are separate backends. These notes explain
source differences; they make no new timing claim. See the main README for their
current contracts and memory-accounting limits.
