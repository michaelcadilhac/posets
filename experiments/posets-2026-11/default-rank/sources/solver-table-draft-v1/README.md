This is a source-only two-arm derivation draft, not a result. No module was
imported and no test, helper, workload or table derivation was executed here.
The phase-one `work/acacia-campaign/make-final-table.py` is unchanged.

The only accepted inventory, in order, is:

- `baseline-default`: Posets `789bf99346d41a30f1b454ff58385b6f8a2961a9`.
- `optimized-default`: Posets `30e66853832d98bcfaada296d892157b95a8150d`.

The comparator is `baseline-default`. Both arms must retain Acacia
`86dd487001dc75a3c1c0dfa9db50a9882a532db1`, shipping
`otf_sparse_formula_parity`, empty primary flags, 17 seconds, 8 GiB and no swap.
Exactly 1,524 unique cases per arm are required: 3,048 native rows, 3,048 scoring
rows and the matching raw/scored memory sidecars. The frozen list must match
both arms. No arbitrary-arm mode or replacement scheduler is introduced.

Before future use, the owner must supply a completed campaign and its externally
approved exact `frozen.json` hash. The script requires complete progress, a usable
unchanged `campaign.py` summary, no truth or collected conflicts, exact labels,
pins, case identities and resource treatment. It verifies every cited native,
scored and memory file hash, preserves decisive native-overrun truth checks,
recomputes strict-wall solved cases, gains/losses and PAR2, and compares them
with the existing summary. All nonanswers cost 34 seconds; only native decisive
answers with measured wall time <=17 seconds count as solved. Native rows are
never edited; the existing scoring view may differ only by the documented
decisive-overrun-to-TIMEOUT result field.

The original CPU validation is retained. Scope peak and process RSS are also
recomputed with the exact hash-verified existing `scope_memory.py` helper and
the same provenance/coverage rules used by `campaign.py`. The helper is loaded
only during future derivation, with bytecode writes disabled. Incomplete CPU,
scope or RSS coverage retains its denominator and suppresses the corresponding
full-cohort total/max/median; process RSS remains distinct from cgroup peak.
No new memory estimator or collector is introduced.

The independent kernel-event audit joins the raw and scoring memory sidecars
on the existing complete `MEMORY_KEYS`: solver/case/cap, run index, binary hash,
scope unit, measured time, exit code, scope peak and process RSS. Scoring must
preserve the entire memory sidecar exactly by case. Nonempty event records need
the existing cgroup provenance and nonnegative uint64 JSON integer counters.
Empty event records need an explicit missing reason. Malformed data or a wrong
join aborts publication rather than becoming zero events.

The audit reports observed/missing event coverage, OOM/oom_kill invocation-row
counts, sums of the observed kernel counters, native MEMOUT/TIMEOUT counts and
their intersections. An invocation with oom_kill=3 counts as one observed OOM-kill
row and three counter increments. TIMEOUT plus an OOM kill stays TIMEOUT and costs
the same PAR2 penalty. Zero observed events with missing coverage is not complete
evidence of zero OOM. Positive events and missing observations retain case and
invocation identities; no events are used to relabel or rescore outcomes.

After explicit owner/lane release, create fresh existing scoped-task specs using
the established Acacia cwd, 8 GiB/no swap. The synthetic test command is:

```text
python3 -B /home/gperez/Documents/Codex/2026-11-08/look-into-the-posets-sibling-repo/work/default-optimization/solver-table-draft-v1/test_default_table.py
```

These source-only fixtures cover zero events with complete coverage, TIMEOUT
with multiple OOM-kill increments, missing event evidence, every invocation join
key, malformed counter types/ranges/provenance, missing/extra/duplicate cases and
tampered TSV bytes. They touch only temporary synthetic files. They have not run.

After campaign completion, summary-only verification, final bindings and source
review, the derivation command is:

```text
python3 -B ABS_DRAFT/make-default-table.py ABS_COMPLETED_CAMPAIGN ABS_FRESH_OUTPUT --frozen-sha256 APPROVED_FROZEN_SHA256
```

The placeholders are intentionally unresolved: this draft contains no future
manifest/binary hash or claimed campaign result. The output directory must not
exist. Only after every gate passes does the script create the JSON/CSV comparison
and `oom-event-observations.json`; `COMPLETION.json` is written last, binding the
three outputs. Input hashes are checked again before publication. Preserve failed
attempts and do not overwrite or silently resume a partial output directory.

The final table and OOM audit still need independent saved-row review. The script
reuses the campaign's established external-truth adjudication and verifies native
cross-arm consistency; it does not rerun the solver or independently parse the
corpus truth. This one campaign is an observed comparison, not fresh held-out
evidence or a significance/general-speedup claim. Raw data access remains subject
to the existing compact-package retention limits.
