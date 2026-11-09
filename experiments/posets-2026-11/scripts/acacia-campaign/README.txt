This directory wraps the isolated Acacia checkout's existing native coverage
runner. It never builds or changes a checkout. The original repository/corpus
are read only. Existing corpus-verification and lifecycle evidence are separate.

Frozen arms input (absolute binary/provenance paths):
{
  "baseline": "shipping-e167",
  "arms": [
    {"label": "shipping-e167", "binary": "/absolute/frozen/binary",
     "binary_sha256": "optional expected SHA256, checked if supplied",
     "acacia_sha": "Acacia source commit",
     "source_pins": {"acacia": "commit", "posets": "commit"},
     "flags": "", "preset": "otf_sparse_formula_parity",
     "provenance": "/absolute/build-provenance.json"},
    {"label": "candidate-rank", "binary": "/absolute/other/binary",
     "acacia_sha": "Acacia source commit",
     "source_pins": {"acacia": "commit", "posets": "commit"},
     "flags": "", "preset": "otf_sparse_formula_parity"}
  ]
}

All primary arms should use empty flags and the shipping compiled preset.
The preset field records identity; it does not add a solver runtime option.
No forced backward/search diagnostic flags belong in this primary comparison.

From the task directory:
  python3 work/acacia-campaign/selfcheck.py
  python3 work/acacia-campaign/campaign.py --arms /absolute/arms.json \
    --output /absolute/new-campaign-directory --dry-run

Remove --dry-run to execute. Run the identical command to resume. --summary-only
rebuilds final reports without any solver execution, requiring 1524 exact rows
for every frozen arm. The default isolated checkout, all.list, native TLSF map,
exceptions and original corpus paths can be specified explicitly in --help.
No allowed CPUs or CPU quota is applied unless explicitly supplied.

The fixed seed shuffles the same corpus into 32-instance batches, rotates a
seeded arm order to balance early/late positions, and runs one native runner
at a time. Each arm has one native durable raw TSV and its retained memory,
conflict and summary sidecars. Resume reads each arm once initially and once
after a batch, never scans the growing raw file for every stdout row.
Known-status conflicts stop the native runner at its first disagreement;
cross-arm opposing answers stop the driver after each batch, even with unknown
STATUS. stopped_conflict progress and cross-arm-conflicts.json preserve evidence.

progress.json gives per-arm counts, current batch/arm, command and live log.
logs/*.log retain native runner stdout/stderr (per-instance result lines, not
discarded solver text). invocations.jsonl binds completed attempts to commands,
log digests and exit codes. thermal.tsv uses the repository's host sampler.
frozen.json binds source pins, binaries, optional build provenance, runner
support scripts, relevant environment, list/map/exceptions, corpus digest,
caps/resources and schedule. host.json records CPU model/topology/affinity.

Every batch first refuses any active acacia-* scope and returns exit75 with a
visible paused_contention progress state. Resume after the other work ends.
No unrelated scopes are stopped. The child inherits ACACIA_CAMPAIGN_SCOPE_GUARD
so the repository's campaign-wide final sweep is disabled; run_systemd_scope
still owns/cleans each invocation. ACACIA_ALLOW_STRAY_SCOPES is never overridden
and cannot bypass this driver's check. A shared per-user host lock at
/tmp/acacia-posets-benchmark-UID.lock also excludes copies using different
output directories; driver.lock separately protects each output directory.
Inherited Acacia runtime/diagnostic variables are refused except the campaign
guard and stray-scope variable, so stale deadlines/capture settings cannot
silently alter primary timing. LD_/allocator/OpenMP environment is hash-bound.
Avoid builds/tests/other load while timing; a batch-entry scope check cannot
prevent an unrelated process from starting later.

summary.json and summary.txt use the repository reporter for exact uniform-cap
validation, separate failure types, verdict conflicts and PAR2. All nonanswers
cost34 seconds. Native successful verdicts with measured wall seconds >17 are
preserved verbatim and remain part of truth-conflict checks. An explicit
strict-wall-scoring/ TSV projects only their result to TIMEOUT for the existing
reporter (all other fields and memory join keys are unchanged). Its provenance
JSON binds native/scored/memory hashes, raw verdict/exit/time and the <=17 rule.
The report lists wall-cap-overruns separately from native TIMEOUT outcomes;
this scoring category does not claim the native timer fired. There is no
solved-time clamping and no rewriting of native raw evidence.
Peak/median memory and total CPU are withheld unless all1524
observations are valid. Cross-arm opposing answers are conflicts even when
STATUS is unknown; preserved conflicts make scores unusable (exit3), while
ordinary solver ERROR/CRASH rows remain explicit nonanswers. Gains/losses and
PAR2 deltas are candidate minus baseline, with no assumed noise floor.

Limit: source pin declarations are bound to file bytes but their authority is
the build provenance supplied by the builder; no build is repeated here.
