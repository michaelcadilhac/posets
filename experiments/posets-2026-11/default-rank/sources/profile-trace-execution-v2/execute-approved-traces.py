"""Execute the released plan in its existing order using the original scope owner."""
import datetime
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

if not __debug__:
    raise SystemExit("Assertions must be enabled")
HERE = Path(__file__).resolve().parent
PHASE = HERE.parent
WORK = PHASE.parent
PLAN_PATH = PHASE / "profile-trace-specs-v2/plan.json"
RELEASE_PATH = PHASE / "profile-trace-release-v2.json"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binding(path):
    return {"path": str(path), "sha256": sha(path)}


def read(path):
    return json.loads(Path(path).read_text())


def verify(item):
    assert binding(item["path"]) == item, item


def save(path, value):
    with Path(path).open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


assert sha(RELEASE_PATH) == "e4eddf5619482aafb72db0d5828c4f20b292f97e93a6029f2907841bcb762f97"
release = read(RELEASE_PATH)
for item in release["bindings"]:
    verify(item)
plan = read(PLAN_PATH)
assert sha(PLAN_PATH) == "137f2e93460b2437a14143bf8ea1cf1a436b2073d1a2e87230454040dec9aa64"
verify(plan["projector"])
projector = module("approved_profile_stdout", plan["projector"]["path"])
guard_path = WORK / "acacia-campaign/campaign.py"
assert sha(guard_path) == "20724d11b3b364ccc416ef24927a641fdf706f45f6ec923f10b7864cdfab0a1e"
guard = module("existing_campaign_scope_guard", guard_path)
inventory = read(plan["eligibility_inventory"]["path"])
closure = read(plan["accepted_closure"]["path"])
all_checked = dict(closure["checked_file_sha256"])
for path, digest in inventory["checked_file_sha256"].items():
    assert path not in all_checked or all_checked[path] == digest
    all_checked[path] = digest


def invariance():
    for item in release["bindings"]:
        verify(item)
    for path, digest in all_checked.items():
        assert sha(path) == digest, path
    for role, facts in closure["source_roles"].items():
        command = ["git", "--no-optional-locks", "-C", facts["source"]]
        assert subprocess.check_output(command + ["rev-parse", "HEAD"], text=True).strip() == facts["revision"]
        assert subprocess.check_output(command + ["status", "--porcelain"], text=True) == ""
    return {"checked_files": len(all_checked), "source_roles": closure["source_roles"]}


completed = []
started = datetime.datetime.now(datetime.timezone.utc).isoformat()
wall_start = time.monotonic()
environment = os.environ | {"PYTHONDONTWRITEBYTECODE": "1", "GIT_OPTIONAL_LOCKS": "0"}
current = None
with guard.HOST_LOCK.open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    save(HERE / "execution-start.json", {"release": binding(RELEASE_PATH), "plan": binding(PLAN_PATH),
                                         "loop": binding(__file__), "guard": binding(guard_path),
                                         "started_utc": started, "initial_invariance": invariance()})
    try:
        for index, row in enumerate(plan["rows"]):
            if not row["eligible"]:
                assert not any(key in row for key in ["record", "spec", "wrapper_argv"])
                continue
            current = {"plan_row": index, "spec": row["spec"], "record": row["record"]}
            record_path = Path(row["record"])
            assert not any(record_path.parent.glob(record_path.stem + ".*")), "Preserve existing attempt sidecars"
            assert not guard.running_scopes(), "Scope contention: stop and preserve, never kill foreign scopes"
            for item in [row["spec"], row["trace"], plan["wrapper"], plan["environment"], plan["projector"]]:
                verify(item)
            artifact = [value for value in plan["profile_artifacts"] if value["role"] == row["role"] and value["mode"] == row["mode"]]
            assert len(artifact) == 1 and artifact[0]["ELF"] == row["instrumented_ELF"]
            assert sha(artifact[0]["ELF"]["path"]) == artifact[0]["ELF"]["sha256"]
            for library in artifact[0]["runtime_libraries"]:
                assert str(Path(library["path"]).resolve(strict=True)) == library["realpath"]
                assert sha(library["path"]) == library["sha256"]
            record_path.parent.mkdir(parents=True, exist_ok=True)
            save(record_path.with_suffix(".started.json"), {**current, "argv": row["wrapper_argv"],
                                                           "utc": datetime.datetime.now(datetime.timezone.utc).isoformat()})
            guard.atomic_json(HERE / "progress.json", {"status": "running", "completed": len(completed), "total": 192, "current": current})
            call = subprocess.run(row["wrapper_argv"], env=environment, capture_output=True, text=True)
            record_path.with_suffix(".wrapper.stdout").write_text(call.stdout)
            record_path.with_suffix(".wrapper.stderr").write_text(call.stderr)
            record = read(record_path)
            spec = read(row["spec"]["path"])
            assert record["spec"] == spec and record["spec_sha256"] == row["spec"]["sha256"]
            assert record["helper_sha256"] == plan["wrapper"]["sha256"] == sha(record_path.with_suffix(".helper.py"))
            assert record["benchlib_sha256"] == "0e18c08c753786c07974d80cf47a1c46d95b0c0fea9616e4468eee3de28c239c"
            assert sha(record["log"]) == record["log_sha256"]
            actual_environment = record["environment_overrides"]
            scope_guard = actual_environment["ACACIA_CAMPAIGN_SCOPE_GUARD"]
            assert scope_guard.startswith("scoped-task:")
            assert actual_environment == read(spec["environment_file"]) | {"ACACIA_CAMPAIGN_SCOPE_GUARD": scope_guard, "PYTHONDONTWRITEBYTECODE": "1"}
            assert record["memory_max"] == "8G" and record["memory_swap_max"] == "0"
            result = record["result"]
            assert call.returncode == result["returncode"] == 0 and not result["timed_out"] and not result["resource_limited"]
            assert not any(json.loads(result["scope_memory_events"])[key] for key in ["oom", "oom_kill", "oom_group_kill"])
            assert result["memory_peak_bytes"] is not None and result["cpu_seconds"] is not None
            # The unchanged helper merges streams. Split only when its byte counters prove stderr is empty.
            raw = Path(record["log"]).read_text()
            assert result["stderr_bytes"] == 0 and len(raw) == result["stdout_bytes"], "Cannot prove stdout from combined log; preserve and stop"
            accepted = projector.accept_stdout(raw, row["expected_stdout"])
            record_path.with_suffix(".stdout").write_text(raw)
            record_path.with_suffix(".stderr").write_text("")
            parsed_path = record_path.with_suffix(".accepted.json")
            save(parsed_path, {"plan_row": index, "record": binding(record_path), "spec": row["spec"],
                               "projector": plan["projector"], "expected_stdout": row["expected_stdout"], "accepted": accepted,
                               "stream_proof": "original helper stderr_bytes=0; combined log exactly matches stdout character count"})
            outcome = {**current, "record_binding": binding(record_path), "accepted": binding(parsed_path),
                       "seconds": result["seconds"], "cpu_seconds": result["cpu_seconds"], "scope_memory_peak_bytes": result["memory_peak_bytes"]}
            completed.append(outcome)
            with (HERE / "completed.jsonl").open("a") as stream:
                stream.write(json.dumps(outcome) + "\n")
            if len(completed) % 24 == 0 or len(completed) == 1:
                print(json.dumps({"completed": len(completed), "total": 192, "seconds_elapsed": time.monotonic() - wall_start,
                                  "last_plan_row": index}), flush=True)
        assert len(completed) == 192
        final = invariance()
        save(HERE / "COMPLETION.json", {"status": "all192 instrumented calls accepted;12 qualification-censored rows retained without calls",
                                        "started_utc": started, "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                        "release": binding(RELEASE_PATH), "plan": binding(PLAN_PATH), "loop": binding(__file__),
                                        "counts": plan["counts"], "completed": completed, "final_invariance": final,
                                        "clean_statuses_unchanged": True, "clocks_discarded": True,
                                        "wall_seconds": time.monotonic() - wall_start,
                                        "resource_coverage": {"valid": 192, "total": 192,
                                                              "max_scope_memory_peak_bytes": max(row["scope_memory_peak_bytes"] for row in completed),
                                                              "CPU_seconds_sum": sum(row["cpu_seconds"] for row in completed)},
                                        "PMU_or_Acacia_runtime_run": False})
        guard.atomic_json(HERE / "progress.json", {"status": "complete", "completed": 192, "total": 192})
        print("ALL192-TRACE-CALLS-ACCEPTED", flush=True)
    except Exception as error:
        save(HERE / "FAILURE.json", {"status": "stop and preserve", "current": current, "completed": completed,
                                     "error": repr(error), "release": binding(RELEASE_PATH), "plan": binding(PLAN_PATH)})
        guard.atomic_json(HERE / "progress.json", {"status": "stop-and-preserve", "completed": len(completed), "current": current, "error": repr(error)})
        raise
