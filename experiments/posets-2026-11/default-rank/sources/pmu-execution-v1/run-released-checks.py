"""Run only the four metadata and eight backend checks already released by root."""
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

if not __debug__:
    raise SystemExit("Assertions must be enabled")
OUT = Path(__file__).resolve().parent
PHASE = OUT.parent
WORK = PHASE.parent
PLAN = PHASE / "pmu-plan-v1"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binding(path):
    return {"path": str(path), "sha256": sha(path)}


def read(path):
    return json.loads(Path(path).read_text())


def save(path, value):
    with Path(path).open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


assert sha(PHASE / "pmu-activation-v1.json") == "4e9857778fbbfcb17dfd85865ecee9602918a8a7b2d97795920f88ff0f3aa21c"
manifest = read(PLAN / "source-manifest.json")
assert sha(PLAN / "source-manifest.json") == "2e72016b0d8358395ad0b329bb6b981695f0420889e51dc8f40c855badab8c5c"
for name, digest in manifest["files"].items():
    assert sha(PLAN / name) == digest
schedule = read(PLAN / "schedule.json")
commands = {row["name"]: row for row in read(PLAN / "argv.json")["commands"]}
closure = read(OUT / "ACTUAL-ARTIFACT-CLOSURE.json")
for path, digest in closure["checked_file_sha256"].items():
    assert sha(path) == digest
wrapper = WORK / "acacia-campaign/scoped_task.py"
guard_path = WORK / "acacia-campaign/campaign.py"
spec = importlib.util.spec_from_file_location("pmu_existing_scope_guard", guard_path)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
completed = []
with guard.HOST_LOCK.open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    for name in schedule["runtime_command_order"][:12]:
        row = commands[name]
        assert row["kind"] in {"metadata", "backend_check"} and row["mode"] == "check"
        path = Path(row["record"])
        assert not any(path.parent.glob(path.stem + ".*")) and not guard.running_scopes()
        approved_spec = read(row["spec"])
        assert approved_spec["command"] == row["argv"] and row["argv"][:4] == ["taskset", "-c", "2", "env"]
        artifact = next(value for value in closure["artifacts"] if value["role"] == row["role"] and value["width"] == row["width"])
        assert sha(artifact["files"]["ELF"]["path"]) == artifact["files"]["ELF"]["sha256"]
        argv = ["/usr/bin/python3", "-B", str(wrapper), row["spec"], row["record"]]
        call = subprocess.run(argv, capture_output=True, text=True, env=os.environ | {"PYTHONDONTWRITEBYTECODE": "1", "GIT_OPTIONAL_LOCKS": "0"})
        path.with_suffix(".wrapper.stdout").write_text(call.stdout)
        path.with_suffix(".wrapper.stderr").write_text(call.stderr)
        record = read(path)
        assert record["spec"] == approved_spec and record["spec_sha256"] == sha(row["spec"])
        assert record["helper_sha256"] == sha(wrapper) == sha(path.with_suffix(".helper.py"))
        assert sha(record["log"]) == record["log_sha256"]
        assert record["memory_max"] == "8G" and record["memory_swap_max"] == "0"
        result = record["result"]
        assert call.returncode == result["returncode"] == 0 and not result["timed_out"] and not result["resource_limited"]
        assert not any(json.loads(result["scope_memory_events"])[key] for key in ["oom", "oom_kill", "oom_group_kill"])
        raw = Path(record["log"]).read_text()
        assert result["stderr_bytes"] == 0 and len(raw) == result["stdout_bytes"]
        lines = raw.splitlines()
        banners = [dict(word.split("=", 1) for word in line.split()) for line in lines if line.startswith("rank_pmu_diagnostic=")]
        layouts = [dict(word.split("=", 1) for word in line.split()) for line in lines if line.startswith("rank_pmu_width_selector=")]
        footers = [dict(word.split("=", 1) for word in line.split()) for line in lines if line.startswith("rank_pmu_kernel_exit=")]
        assert len(banners) == len(layouts) == len(footers) == 1
        banner, layout, footer = banners[0], layouts[0], footers[0]
        region = "1" if row["phase"] == "cpre" else "0"
        assert banner["rank_pmu_diagnostic"] == banner["clocks_discarded"] == "1" and banner["cpu_affinity"] == "2"
        assert banner["mode"] == "check" and banner["selected_phase"] == row["phase"] and banner["selected_region"] == region
        assert int(banner["process_id"]) > 0 and banner["thread_id"] == banner["process_id"]
        lanes, selector = (16, 0) if row["width"] == "compatible" else (64, 1)
        assert layout == {"rank_pmu_width_selector": str(selector), "selected_lanes": str(lanes), "original_vector_flavors": "2", "rank_stats_enabled": "0", "allocation_hooks_enabled": "0"}
        assert footer == {"rank_pmu_kernel_exit": "0", "rank_pmu_roi_valid": "1", "total_region_calls": "0", "captured_regions": "0", "selected_region": region, "check_only": "1", "pmu_core_usable": "0", "pmu_all_events_available": "0"}
        metadata = None
        if row["kind"] == "metadata":
            objects = [json.loads(line) for line in lines if line.startswith("{")]
            assert len(objects) == 1
            metadata = objects[0]
            authority = read(PHASE / "profile-gate-results-v2/actual-artifact-closure.json")["source_roles"][row["role"]]
            anchor = read(authority["clean_anchor"]["path"])["modes"][row["width"]]
            for key in ["compiler", "element_bytes", "selected_lanes", "compatible_lanes", "native_lanes", "max_fixed_lanes", "block_bytes", "width_selector", "alignment", "ndebug", "stdlib_assertions", "simd_type", "hybrid_policy"]:
                assert metadata[key] == anchor[key]
            assert metadata["backends"] == ["rank_bucketed_vector_backed"]
        else:
            identities = re.findall(r'^\[--\] running tests for (.+)$', raw, flags=re.MULTILINE)
            assert identities == ["posets::downsets::rank_bucketed_vector_backed<posets::vectors::simd_vector_backed<test_value_type>>"]
            assert "[OK]" in raw
        accepted = {"command": row, "record": binding(path), "closure": binding(OUT / "ACTUAL-ARTIFACT-CLOSURE.json"),
                    "banner": banner, "layout": layout, "footer": footer, "metadata": metadata,
                    "interpretation": "check-only; zero captures means no PMU event collection, not measured zero events"}
        save(path.with_suffix(".accepted.json"), accepted)
        completed.append({"name": name, "record": binding(path), "accepted": binding(path.with_suffix(".accepted.json")), "resource": result})
        guard.atomic_json(OUT / "check-progress.json", {"completed": len(completed), "total": 12, "last": name})
        print(json.dumps({"completed": len(completed), "total": 12, "name": name, "seconds": result["seconds"]}), flush=True)
    for path, digest in closure["checked_file_sha256"].items():
        assert sha(path) == digest
    save(OUT / "CHECKS-COMPLETE.json", {"status": "all4metadata and8backend checks passed on exactCPU2;24 measurement calls remain unreleased",
                                       "closure": binding(OUT / "ACTUAL-ARTIFACT-CLOSURE.json"), "stages": completed,
                                       "no_hardware_counter_measurement": True, "source_artifact_dependency_library_invariance": True})
