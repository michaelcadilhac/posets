"""Execute the existing 24 PMU specs in saved order; preserve unavailable raw events."""
import datetime
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


def fields(line):
    tokens = [word.split("=", 1) for word in line.split()]
    assert all(len(token) == 2 for token in tokens)
    result = dict(tokens)
    assert len(result) == len(tokens)
    return result


release_path = PHASE / "pmu-measurement-release-v1.json"
assert sha(release_path) == "aeb7a7c02870f9dfc21bcc84374e1e1b3bd8e9cf6341b9d092cf40b25d6fc752"
release = read(release_path)
for item in release["bindings"]:
    assert sha(item["path"]) == item["sha256"]
actual = read(OUT / "PMU24-ACTUAL-BINDINGS.json")
closure = read(OUT / "ACTUAL-ARTIFACT-CLOSURE.json")
wrapper = WORK / "acacia-campaign/scoped_task.py"
guard_path = WORK / "acacia-campaign/campaign.py"
assert sha(wrapper) == "9fa7a6777374552f2b0ea95556896757f97cf5396689cdc2ef3026a795688e43"
assert sha(guard_path) == "20724d11b3b364ccc416ef24927a641fdf706f45f6ec923f10b7864cdfab0a1e"
plan_dir = PHASE / "pmu-plan-v1"
assert sha(plan_dir / "source-manifest.json") == "2e72016b0d8358395ad0b329bb6b981695f0420889e51dc8f40c855badab8c5c"
for name, digest in read(plan_dir / "source-manifest.json")["files"].items():
    assert sha(plan_dir / name) == digest
trace_bindings = []
for trace in read(plan_dir / "evidence-bindings.json")["trace_bindings"]:
    trace_bindings.extend([{"path": trace["path"], "sha256": trace["sha256"]}, trace["metadata"], trace["capture_manifest"], trace["cohort_plan"]])
spec = importlib.util.spec_from_file_location("pmu_measure_existing_guard", guard_path)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
commands = {row["name"]: row for row in read(PHASE / "pmu-plan-v1/argv.json")["commands"]}
schedule = read(PHASE / "pmu-plan-v1/schedule.json")
assert [row["name"] for row in actual["rows"]] == schedule["runtime_command_order"][12:]


def invariance():
    for item in release["bindings"]:
        assert sha(item["path"]) == item["sha256"]
    for path, digest in closure["checked_file_sha256"].items():
        assert sha(path) == digest
    for trace in trace_bindings:
        assert sha(trace["path"]) == trace["sha256"]
    return {"checked_closure_files": len(closure["checked_file_sha256"]), "release_bindings": len(release["bindings"])}


completed = []
current = None
with guard.HOST_LOCK.open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    initial = invariance()
    save(OUT / "MEASUREMENT-START.json", {"release": binding(release_path), "script": binding(__file__), "initial_invariance": initial})
    try:
        for index, row in enumerate(actual["rows"]):
            current = row
            declaration = commands[row["name"]]
            path = Path(row["record"])
            assert not any(path.parent.glob(path.stem + ".*")) and not guard.running_scopes()
            assert sha(row["spec"]["path"]) == row["spec"]["sha256"]
            approved = read(row["spec"]["path"])
            assert approved["command"] == row["command"] == declaration["argv"]
            assert row["command"][:4] == ["taskset", "-c", "2", "env"]
            assert sha(row["actual_ELF"]["path"]) == row["actual_ELF"]["sha256"]
            for trace in trace_bindings:
                assert sha(trace["path"]) == trace["sha256"]
            for library in row["runtime_libraries"]:
                assert str(Path(library["path"]).resolve(strict=True)) == library["realpath"] and sha(library["path"]) == library["sha256"]
            argv = ["/usr/bin/python3", "-B", str(wrapper), row["spec"]["path"], row["record"]]
            result = subprocess.run(argv, capture_output=True, text=True, env=os.environ | {"PYTHONDONTWRITEBYTECODE": "1", "GIT_OPTIONAL_LOCKS": "0"})
            path.with_suffix(".wrapper.stdout").write_text(result.stdout)
            path.with_suffix(".wrapper.stderr").write_text(result.stderr)
            record = read(path)
            assert record["spec"] == approved and record["spec_sha256"] == row["spec"]["sha256"]
            assert record["helper_sha256"] == sha(wrapper) == sha(path.with_suffix(".helper.py"))
            assert record["memory_max"] == "8G" and record["memory_swap_max"] == "0"
            assert sha(record["log"]) == record["log_sha256"]
            process = record["result"]
            assert result.returncode == process["returncode"] == 0 and not process["timed_out"] and not process["resource_limited"]
            assert not any(json.loads(process["scope_memory_events"])[key] for key in ["oom", "oom_kill", "oom_group_kill"])
            raw = Path(record["log"]).read_text()
            assert process["stderr_bytes"] == 0 and len(raw) == process["stdout_bytes"]
            lines = raw.splitlines()
            def one(prefix):
                found = [fields(line) for line in lines if line.startswith(prefix)]
                assert len(found) == 1
                return found[0]
            banner = one("rank_pmu_diagnostic=")
            layout = one("rank_pmu_width_selector=")
            footer = one("rank_pmu_kernel_exit=")
            group = one("pmu_group_status=")
            region = "1" if declaration["phase"] == "cpre" else "0"
            lanes, selector = (16, 0) if declaration["width"] == "compatible" else (64, 1)
            assert banner["rank_pmu_diagnostic"] == banner["clocks_discarded"] == "1" and banner["cpu_affinity"] == "2"
            assert banner["mode"] == "measure" and banner["selected_phase"] == declaration["phase"] and banner["selected_region"] == region
            assert layout == {"rank_pmu_width_selector": str(selector), "selected_lanes": str(lanes), "original_vector_flavors": "2", "rank_stats_enabled": "0", "allocation_hooks_enabled": "0"}
            assert footer["rank_pmu_kernel_exit"] == "0" and footer["rank_pmu_roi_valid"] == "1"
            assert footer["total_region_calls"] == "4" and footer["captured_regions"] == "1" and footer["selected_region"] == region and footer["check_only"] == "0"
            identities = re.findall(r'^\[--\] running tests for (.+)$', raw, flags=re.MULTILINE)
            assert identities == ["posets::downsets::rank_bucketed_vector_backed<posets::vectors::simd_vector_backed<test_value_type>>"]
            raw_ids = [fields(line) for line in lines if line.startswith("group_raw_id=")]
            assert len({item["group_raw_id"] for item in raw_ids}) == len(raw_ids)
            events = [fields(line) for line in lines if line.startswith("event=")]
            assert [item["event"] for item in events] == ["cycles", "instructions", "l1d_read_miss", "llc_read_miss"]
            for event in events:
                assert event["raw_available"] in {"0", "1"}
                if event["raw_available"] == "1":
                    matches = [item for item in raw_ids if item["group_raw_id"] == event["id"]]
                    assert len(matches) == 1 and matches[0]["group_raw_count"] == event["raw_count"]
                    event["count"] = int(event["raw_count"])
                else:
                    assert "raw_count" not in event
                    event["count"] = None
            enabled = int(group["time_enabled"]) if group["time_available"] == "1" else None
            running = int(group["time_running"]) if group["time_available"] == "1" else None
            core = group["pmu_group_status"] == "ok" and enabled is not None and enabled == running and enabled > 0 and all(item["raw_available"] == "1" for item in events[:2])
            assert footer["pmu_core_usable"] == str(int(core))
            usable_all = core and all(item["raw_available"] == "1" for item in events)
            assert footer["pmu_all_events_available"] == str(int(usable_all))
            accepted = {"name": row["name"], "record": binding(path), "actual_binding": row,
                        "banner": banner, "layout": layout, "footer": footer, "group": group, "raw_group_read": raw_ids,
                        "events": events, "time_enabled": enabled, "time_running": running,
                        "core_usable": core, "all_events_available": usable_all,
                        "clocks_discarded": True, "scaled_or_zero_filled": False}
            save(path.with_suffix(".accepted.json"), accepted)
            path.with_suffix(".stdout").write_text(raw)
            path.with_suffix(".stderr").write_text("")
            completed.append({"name": row["name"], "record": binding(path), "accepted": binding(path.with_suffix(".accepted.json")),
                              "group_status": group["pmu_group_status"], "core_usable": core, "resource": process})
            guard.atomic_json(OUT / "measurement-progress.json", {"completed": len(completed), "total": 24, "last": row["name"], "group_status": group["pmu_group_status"]})
            print(json.dumps({"completed": len(completed), "total": 24, "name": row["name"], "group_status": group["pmu_group_status"], "core_usable": core}), flush=True)
        final = invariance()
        save(OUT / "MEASUREMENTS-COMPLETE.json", {"status": "all24 original PMU calls completed; event usability remains separate", "release": binding(release_path),
                                                  "script": binding(__file__), "completed": completed, "final_invariance": final,
                                                  "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "clocks_discarded": True})
    except Exception as error:
        save(OUT / "MEASUREMENT-FAILURE.json", {"status": "stop and preserve", "current": current, "completed": completed, "error": repr(error)})
        raise
