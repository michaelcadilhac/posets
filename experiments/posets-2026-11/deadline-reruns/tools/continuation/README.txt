This is a source-only continuation proposal, not a target release.

The conditional 30-second run stopped after 205 primary observations. Its last
row is the original e167 arm's native ERROR / exit 3 on ltl2dba_R_pb_12_pe_.ltl.
That row, its 20.121184411924332 seconds, exit code, native label and resource
observations remain unchanged. The separate diagnostic reproduced three Spot
acceptance-set-limit exceptions, the outside-proved-DPA-Buchi status message,
and final ERROR, without timeout or OOM. The original primary stderr text was
not retained; the explicit adjudication rests on that bound original record
and independent diagnostic, not on a claim that primary stderr was recovered.

The amendment carries all 205 original rows and runs only original visits
205 through 311, in order. The first is the lazy-FC counterpart on the same R12
case. There are 102 uninterrupted prefix pairs, one pair split across the
STOP / diagnostic / adjudication gap, and 53 uninterrupted suffix pairs. No
paired timing ratio is inferred for that ERROR pair. The final conditional
panel still has 156 cases per arm; this is not a new full 1,524-case campaign.
The 17-second source observations and scores are untouched.

capture_native.py imports the exact original native CLI and calls main(argv).
It temporarily replaces only that module's imported run_systemd_scope alias.
The delegate receives the same positional and keyword arguments and default
full-string capture mode. The returned RunResult object is returned unchanged.
Only after the delegate has completed its measured/resource interval are its
full strings and metadata written. No route, phase, worker, deadline, capture
consumer/filter or solver flag is added. The native CLI still owns the scope,
normalization, raw row and memory sidecar. The controller joins the capture to
those native artifacts, including the CLI's six-decimal CPU formatting.

Recognition is deliberately narrow: complete stderr must equal the preserved
1,080-byte diagnostic signature, stdout must be empty, native outcome must be
ERROR / exit 3, and timeout, resource reason, resource_limited and OOM fields
must be clear. Identity, cap, TLSF, ELF, source, flags, preset and sidecar/resource
guards are still required. Similar messages with extra text, a different count
or ordering, another limit, incomplete capture or a mixed resource failure STOP.
The classification is SPOT_ACCEPTANCE_LIMIT_32, separate from TIMEOUT/MEMOUT.
The raw result remains ERROR and costs 60 seconds under conditional PAR2; its
early exit never becomes a cheap solved score. UNKNOWN remains accepted unsolved.
All other ERROR/CRASH outcomes stop after recording the completed attempt and
full capture, before launching another visit. No V2 whitelist is changed.

All new lists, captures, records, progress and composite summary live in a new
namespace. Original helpers, plans, records, traces, ELFs and Spot bytes remain
read-only. Completed new records can be reused without another call. Any
started/partial leg or capture is refused; there is no automatic retry.
Cancellation terminates and waits for the current native CLI, then stops.
Existing cgroup ownership, HostSampler and nonblocking host/output locks remain.

Entrypoints, after source review and separate root releases:

1. Run offline-spec.json through the unchanged scoped_task.py under the host
   lock. It exercises only synthetic data and mocked process/scope work. It
   checks eight fixtures: exact/error rejection and identity, unchanged delegate
   and same object, complete capture/delegation/metadata, 205 + 107 partition/projection, full 107-call mock plus zero-call
   completed resume, partial/cancellation cleanup, unknown/wrong-identity stop,
   and known future primary errors remaining ERROR with cost 60. The actual
   source qualification in source_state is not mocked when preparing real work.
2. Run continue30.py --prepare ABS_NEW_DIR through that same scoped owner. The
   reserved directory is work/deadline-reruns-v1/
   final-kd-timeout-followup30-continuation-v1. It validates the original source,
   every carried record and the exact mapping before writing lists/amendment.
   It emits no solver commands. There is no --dry-run flag.
3. Review that actual amendment and its original-error adjudication. A new
   explicit root release must bind amendment {path, sha256}, set
   accepted_error_category to SPOT_ACCEPTANCE_LIMIT_32 and
   max_new_solver_calls to 107. A template with a pending hash is not authority.
4. The existing owner pattern invokes continue30.py --run ABS/amendment.json
   --release ABS_ROOT_RELEASE.json directly. The controller takes the host lock;
   each unchanged native call takes its own 30-second, 8G, no-swap scope. Do not
   wrap this whole owner inside an extra setup scope that it would see as busy.
   It cannot run until the concrete amendment/release and runtime lane are approved.

External runtime templates are benchmark-owned at continuation-runtime-templates-v1.
Only the offline spec is provided here. No amendment or runnable TRACE/solver
workload has been materialized by this source preparation. Approximate remaining
cap budget is 107 x 30 = 3,210 seconds, plus CLI/scope/capture/check overhead.
No source qualification, imports, syntax checks, tests or target calls have run.
The exact signature is conservative; broader error adjudication needs a new review.
