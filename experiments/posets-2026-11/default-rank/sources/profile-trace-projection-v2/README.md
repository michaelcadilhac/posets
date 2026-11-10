# Narrow profile projection V2 — schema correction, source only

`project_profiles.py` has not been imported or run. It projects only the exact
approved eligibility inventory80b5b223…17c7b8, accepted closure V4d097ccb5…e3a25,
and Nb eligibility approvald308dfd8…d2a9ba. No qualification algorithm, scheduler,
new measurement, library/PIE assumption or dependency-cardinality rule is added.

The writer rechecks their hashes and the accepted checked-file/source/ELF/trace/
dependency/library identities before creating output. Actual source HEADs must
remain clean789/30e. It selects the four already accepted profile ELFs; it does
not rebuild or probe them. Repeated dependency records are allowed. ELF type is
accepted through its existing bound identity, without inferring PIE.

All204 pass rows remain in the projected plan, with their original eligibility,
reason and clean measurement statuses. Exactly192 eligible rows receive specs;
the12 qualification-censored rows have no spec, record or runnable argv. Four
source/exact-cell qualification observations are required for each eligible row.
No clean measurement censor or metric is recomputed or upgraded.

The actual C++ program requires `argc==4`:

```text
PROFILE_ELF ABSOLUTE_TRACE vector_backed|simd_vector_backed operations|roi
```

The generated specs use that form, the unchanged original scoped_task wrapper,
isolated Acacia cwd, bound shipping environment and30s cap. Its existing wrapper
enforces8GiB/no swap. This is instrumented counters/heap evidence; C++ clocks are
discarded and remain separate from clean timing/censorship.

After root reviews this exact source, the existing owner may run its projection
through an approved8G/no-swap scope with a new absolute direct-child output path,
for example `work/default-optimization/profile-trace-specs-v2`. No such directory
or runnable workload spec has been created by this preparation. Output must be
fresh; every JSON write is exclusive. A failed partial projection is retained,
never overwritten. Generating specs does not authorize their execution.

The projected plan binds this writer, the exact inventory/approval/closure,
wrapper/environment, actual profile ELF/library identities and each trace.
The execution owner must verify those bindings before each existing-wrapper
invocation and after the batch; refuse existing record/log/helper sidecars,
hold the shared host lane lock and keep runs serial. No executor is added here.

For a completed scope, require actual rc0, no timeout/resource limit/OOM, correct
argv/cwd/environment/helper and preserved stdout/stderr/resource records first.
Then call `accept_stdout(stdout, row['expected_stdout'])`. It requires exact
tuple metadata (including actual selector0/1 and16/64 context lanes), shipping
move-trait record, all successful phases/counter fields, a final kernel pass,
and the exact original3-region contains or4-region CPre ROI sequence. Operations
mode accepts only its existing per-call phase names. Scalar's context lane field
is the program's selected SIMD context, not a claim of scalar SIMD instructions.

Returned phase descriptors with `has_result=0` have `size` and
`generator_capacity` set to null, not zero retention. The first-failed-query null
is also preserved as null. Other work/heap values keep their original scope and
meaning; no absolute retained-backend estimate or instrumented clock ratio is
invented. Any malformed/failed/partial output is retained and stops for review.

`test_project_profiles.py` contains small synthetic stdout positive/negative
checks only. It does not generate specs, invoke ELFs or represent actual profile
results. After separate release it can run under the existing scoped wrapper as
`python3 -B -m unittest discover -s THIS_DIRECTORY -p test_project_profiles.py`.
No import, syntax compile, test, projection, probe or profile was run in this task.

## V2 correction

The preserved V1 acceptor required only a subset of emitted fields and left most
numeric strings unchecked. This version requires exact key sets for all11
metadata,4 layout,42 phase and3 cleanup fields emitted by the frozen C++ source.
Missing, duplicate and unexpected fields fail closed.

Decimal tokens use the producer's canonical ASCII spelling: no fractions,
exponents, plus signs, leading zeros, Unicode digits, NaN or infinity. Counts,
active coordinates, descriptors and other size_t/uint64 fields use the accepted
64-bit unsigned range. Both live-delta fields use signed64 bounds. Object sizeof
and alignment are positive unsigned values. Success/instrumentation/trait flags,
vector/pass/selector/kernel tokens and intersection reasons use explicit allowed
values. The first-failed query is literal null or an unsigned64 index.

Validation adds no counter-value relationships, entry/exit equality, inferred
retention or new operation ordering/count rule. Nonzero positive/negative cleanup
and phase deltas remain valid. Even nonzero has_result0 placeholders become null,
while has_result1 descriptors remain available. All projection/input/argv/fresh
output behavior is unchanged; the exact old-to-new source diff is preserved.

The independent synthetic positive fixture now lists all42 phase fields. Sources
cover removal of every emitted key, malformed/negative/overflow unsigned fields,
signed64 boundaries, null-or-index, textual/flag/duplicate/extra failures and the
unchanged allowed-operation policy. These tests have not been imported or run.
The parser-only correction needs no C++ rebuild or artifact/eligibility change.
