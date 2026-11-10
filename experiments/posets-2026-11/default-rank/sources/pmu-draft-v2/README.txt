Conditional default-rank PMU companion, source draft only

Nothing here has been imported, compiled or run. The four cells remain conditional
on actual work/heap interpretation and root approval of the final sources/specs.
This directory schedules no work and adds no production or clean-source change.

rank-pmu.cc wraps the original selected-rank full CLI. downset-bm-pmu.cc is the
exact original driver with only the two hunks in cli-observation.patch: PMU hook
include and original-main rename. Both VECTOR_TYPES, original registry/dispatch,
and matrix_cpre::run stay intact. rank-pmu-hooks.hh copies event_state/pmu_group
verbatim from verified phase-one phase-pmu-profile.cc, then adds fixed ROI state,
check/measure lifecycle, exactCPU2 enforcement and explicit validity markers.
No measured Vector/set wrapper or shadow rank header is included.

POSETS_RANK_PMU_MODE=check|measure and POSETS_RANK_PMU_PHASE=build|cpre are required.
The full original argv is forwarded unchanged. Normal example argument tails:
  --trace TRACE --check=replay --params=rounds=1,detail=0 --seed=260617 rank_bucketed_vector_backed simd_vector_backed
  --trace TRACE --params=rounds=1,detail=0 --seed=260617 rank_bucketed_vector_backed simd_vector_backed
The first uses modecheck; the second uses modemeasure. Both must be tasksetCPU2
inside the existing8GiB/no-swap scope. These examples are not executed commands.
Only collector CPre128/native and RR552 build128/native are declared (plan.json).

Compile from the saved original selected-rank command/link templates for each
role/width. Redirect only source/object/dependency/output destinations and add
this private source include path. Keep both original V instantiations and all
effective O3/LTO/native/global/link flags. POSETS_RANK_STATS must be UNDEFINED;
defining it to0 is rejected because its current implementation uses #ifdef.
No allocation/stats hooks, new annotations or global compiler-flag changes.

Existing hooks select call0 build or call1 whole CPre with rounds1/detailfalse.
Require4 total START/STOP pairs and1 selected region. Build includes original
coordinate/V staging+constructor: raw staging is destroyed before build STOP,
while the returned set remains alive and its destruction is excluded.
Whole CPre includes bottom/mapped/combined destruction, but excludes final
frontier destruction. Parser, source/result oracle and group read/printing remain
outside. The library clocks still print through the original CLI; those values
are instrumented diagnostic output, discarded for all clean timing claims.

Exit0 means successful kernel+ROI, not usable counters. Require the printed
rank_pmu_roi_valid and pmu_core_usable plus exact bound width/phase/CPU and raw
event/group records. Cache-event absence/denial and multiplexing remain explicit.
Never substitute zeros, scale counts, or retry expanded event/workload matrices.
CPU2 is mandatory; no automatic fallback. Three paired repetitions are limited
observations, natural hardware cache state differs from cold simulations, and
fewer allocation bytes alone do not demonstrate fewer cache misses.

Anticipated builds/checks, scheduling and output acceptance are in plan.json and
STATIC-CHECKS.json. No future ELF/dependency/runtime evidence is fabricated here.
Reused phase-one controls and original clean recipe hashes are in source-manifest.
The eight new backend checks must follow new789/30e source qualification, rather
than rely on the old phase-one oracle. Preserve all failed/partial/resource rows.

V2 changes only the anticipated metadata-evidence wording. The original
--metadata reports backend registry and width; both vector instantiations are
supported by the bound CLI source. The original_vector_flavors=2 runtime marker
is a literal and is not an independent vector-inventory probe. All C++ source
and measurement-plan bytes remain identical to V1. V1 remains preserved.
