# Default-rank work/heap companion V2 — documentation correction, not executed

This directory is separate instrumentation for baseline
`789bf99346d41a30f1b454ff58385b6f8a2961a9` and candidate
`30e66853832d98bcfaada296d892157b95a8150d`. It changes no production, clean
build, captured input or frozen helper. No source here has been imported,
compiled, collected, tested, linted or profiled. Root must approve the exact
sources and release a separate lane after the current benchmark owner.

The actual operation body remains `matrix_cpre::run` from each pinned source.
`matrix-cpre.hh` and `matrix-check.hh` are referenced unchanged, not replaced.
Both original matrix vector flavors are instantiated: `vector_backed<int8_t>`
and `simd_vector_backed<int8_t>`. The thin diagnostic adapter has no data fields;
compile-time checks require identical value type, sizeof, alignment and both
nothrow-move traits. This keeps the candidate on the shipping accepted-insert
path. A failed assertion rejects this profiling treatment.

The allocation header is an exact copy of Lemire's verified allocation-evidence
snapshot. The counted-set and existing START/STOP ROI patterns are reused;
their exact source bindings are in `input-bindings.json`. No PMU, cache simulator,
new architecture, threshold, SIMD sum or compiler annotation is introduced.

## Two independent passes

`rank-profile.cc TRACE VECTOR operations` wraps the backend's constructor,
contains, apply, union and intersection calls. Each call snapshots existing
`rank_stats`, explicit Vector work, rank hooks and the reused requested-heap
tracker. It runs the original kernel in verification mode. It does not emit
operation clocks.

`rank-profile.cc TRACE VECTOR roi` uses the raw backend and existing kernel
START/STOP hooks, with one round and `detail=false`. It observes the existing
build, whole-CPre, contains and fresh-rebuild windows (three for contains traces,
four for CPre). The kernel's clocks are computed as before and discarded. They
are never printed, compared or used as clean performance/PMU evidence.

These are different processes/passes. A per-operation reset cannot erase an
outer CPre peak. Every snapshot closes before generator-capacity inspection and
serialization. Counters and current-intersection state use fixed storage;
logging uses C stdio only after closure and flushes each row. No record vector,
size-dependent telemetry allocation or extra dominance/oracle preflight is added
to an observed window.

| Window | Ownership at its exit; meaning of heap delta |
| --- | --- |
| `constructor_only_caller_alive` | Backend constructor returned, caller's raw V container is still alive. Incremental live delta is **not** backend retained bytes. |
| `apply` | New result exists; original frontier and retained actioner scratch remain alive. |
| `union_rhs_still_alive` | Consumed RHS is still alive. Its later destruction is outside this individual call. |
| `intersection_rhs_still_alive` | RHS still exists; replacement delta is new-minus-old heap, not absolute new retention. |
| `build_normalized` | The unchanged `matrix_check::build` has destroyed its raw coordinate/V staging containers. Its escaped backend is the newly retained requested-C++-heap delta. Parsed logical inputs remain unchanged across entry/exit. |
| `cpre_after_rhs_destruction_net` | Includes bottom accumulator construction and all mapped/combined/RHS temporary destruction. Final frontier remains alive. Delta is net new-minus-old retained heap, including any other persistent scratch changes; no absolute final-frontier estimate is invented. |
| `rebuild_normalized_first_alive` | A second fresh constructor, not in-place rebuilding. The first frontier is still alive; caller staging is destroyed before this exit. |

Contains argument lifetimes differ by pass. The `operations` window starts
inside `measured_set::contains`, after argument materialization, and closes before
an argument temporary is destroyed. Thus its final-CPre contains snapshot excludes
the initial-state query temporary's creation/destruction. The raw `roi` final-CPre
contains window surrounds the original timed lambda and includes both. A captured
contains query is already built before either pass's contains window. Keep mode
on every row; these two contains allocation scopes are not interchangeable.

Whole kernel cleanup is reported separately after the returned metric map and
frontier die. This is a diagnostic live-heap delta, not an automatic leak or
backend-size verdict. Rows with `has_result=0` have unavailable result size and
generator capacity: their printed zero placeholders do not mean an empty result,
zero retained bytes or measured private rank capacity.

All heap numbers are requested C++ `new`/`new[]` bytes. They exclude tracker
headers/alignment, allocator overhead, direct malloc, stacks, mappings and RSS.
The cgroup peak is a separate whole-process measurement. The preliminary parsed
trace used for metadata remains alive during replay, so whole-process peaks
include that additional input copy. Exposed generator capacity is reported;
private rank capacity is not guessed to match it. Peaks from separate calls are
not additive.

## Counter meanings

The separate shadow-header patches add only diagnostic include/calls and locals:

* `rank_of` entry/completion charges one call and `v.size()` logical active
  coordinates. No lane/padding or `operator[]` hook is used. Actioner reads never
  count as rank scans. Planned coordinates include a started call; completed
  coordinates sum only fully completed calls. Partial reads of a throwing rank
  call are not counted. These shipping-vector traces are not exception probes.
* Intersection hooks surround existing actual query sites. They add no query,
  change no operand or mutation order, and never scan an independent oracle.
  The first failed query is a zero-based position in the left's current
  iteration order. `null` means no failed query was observed. Self/empty-right
  shortcuts have explicit no-query reasons; no queried position is fabricated.
  The per-call frame records entry/query/completion and no-op/change counts.
  The whole-CPre kernel currently has exactly one intersection per round.
* Vector `copy`, meet and partial-order counters charge attempts. Adapter move
  construction/assignment charge completion after the base move. Construction
  from a returned base value is separately labeled `base_adoptions_completed`,
  rather than conflated with backend relocation. Destructors are not counted.
* Existing `rank_stats` fields retain their original meanings, including legacy
  zero fields. Batch counters, notably planned pair meets, need not equal
  completed work if an exception interrupts a call. No production field is added.

Failed operations emit `success=0` snapshots where unwinding reaches the scope.
The old baseline's allocating `noexcept` constructor may terminate before a
snapshot; preserve that process failure/missing row, never turn it into zeros.
A final `kernel_check=pass` is required in addition to successful process/scope
status. The unchanged kernel still checks actual result sets/membership against
the captured/scalar expectations. A completed phase alone is not eligibility.

## Inputs and later execution gates

`trace-plans/` preserves exact copies of the existing standard12 and larger5
eligible TRAIN plans. These are already seen observations, not fresh held-out
validation. All17 declared identities remain in the attempt inventory. Before
any runnable profile spec is generated, its generator must enforce a join of
actual-pin clean source-oracle qualification AND the exact backend/vector/width
clean qualification, with trace/capture hashes and source/library identities.
This source draft has no spec generator and does not assert that gate was run.
Unqualified or censored identities remain explicit unprofiled failure/censor rows,
including source-unverified CPre. Existence or a historical status alone must not
set `source_verified=true`. A30s instrumented success cannot upgrade a censored
clean30/2/2 cell; paired profile comparisons require both sides independently
qualified and successfully completed.

Use compatible scalar+SIMD and native SIMD on both sides, retaining the actual
native=max proof from the new clean builds. The instrumented plan proposes a
separate30s process cap for root approval, 8GiB/no swap, serial builds/runs and
the existing host lock/scoped_task wrapper. This does not change the clean
30/2/2 caps or reuse instrumented outcomes as clean timings.

After review/release, derive the actual profile compiler/link flags from the
new matched clean templates. Compile one translation unit per ELF:

```text
CXX EXACT_MATCHED_FLAGS WIDTH_DEFINE
  -I PROFILE/overlays/ROLE/include -I PROFILE
  -I PINNED_SOURCE/include -I PINNED_SOURCE/lib -I PINNED_SOURCE/tests
  -MMD -MF NEW_OUTPUT.d PROFILE/rank-profile.cc -o NEW_OUTPUT EXACT_LINK_FLAGS
```

This is a command skeleton, not an executed build record. Shadow include order
is mandatory. Both roles use the same instrumentation source, flags and width;
only their bound source/overlay paths differ. Preserve actual compiler identity,
full argv/cwd, dependencies with hashes, link/runtime libraries, binary hash and
scope/spec/helper/stdout/stderr/resource records. Do not register these ELFs in
the clean binary maps. Leave clean `POSETS_RANK_STATS` undefined; this diagnostic
translation unit deliberately enables it and the allocation hook.

First compile/run `rank-profile-check.cc` separately with the same includes and
widths. Define `PROFILE_EXPECT_CANDIDATE` only for the candidate selfcheck ELF.
It checks both flavors, equal intersection's positive baseline allocation/copy
sentinel versus candidate no work, an actual late failed-query index, logical
answers, rank-call/active-coordinate counts and normalized destructor cleanup.
Run its portable build under the existing positive-intercept Memcheck setup
(`--soname-synonyms=somalloc=nouserintercepts`), with all failures retained.
Never compile/link the check and main source together.

Then require actual kernel qualification and exact ROI sequence checks on the
declared traces before gathering work/heap rows. Preserve timeouts/resource
limits/oracle rejections, partial output and missing values. No controller or
postprocessor here silently schedules, filters or summarizes a new experiment.
Root must freeze actual artifact bindings and run specs before execution.

Derived equal/covering/first-middle-last controls are deferred unless actual
counters need clarification. Any later controls must use only a few recorded
frontiers, an independent oracle and an explicit derived/seen label.

## V2 lineage

Only documentation/metadata changed from the preserved `../profile-draft/` V1.
All C++/counter/overlay/allocation/selfcheck/trace-plan bytes are identical. Paths
in this inventory resolve against the actual V2 directory; original evidence
locators remain labeled as originals. `v1-to-v2.patch` and `STATIC-NOTE.json`
record the correction and exact byte-equality proof. No execution is claimed.
