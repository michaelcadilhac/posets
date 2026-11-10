#!/usr/bin/env python3
"""Five adjacent paired repeats using unchanged Acacia native coverage CLI."""

import argparse
from collections import Counter
from contextlib import ExitStack
import csv
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import signal
from statistics import median
import subprocess
import sys
import time

if not __debug__:
    raise SystemExit("Assertions must be enabled")

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CAMPAIGN = ROOT / "work/acacia-campaign/campaign.py"
CAMPAIGN_SHA = "20724d11b3b364ccc416ef24927a641fdf706f45f6ec923f10b7864cdfab0a1e"
CAP = 17
REPETITIONS = 5


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def binding(path):
    path = Path(path).resolve(strict=True)
    return {"path": str(path), "sha256": digest(path)}


def verify(item):
    require(digest(item["path"]) == item["sha256"], f"Changed input: {item['path']}")


def write_new(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def schedule(cells, seed):
    ordered = list(cells)
    random.Random(seed).shuffle(ordered)
    return [{"id": f"{cell['cell_id']}-r{rep + 1}", "cell_id": cell["cell_id"],
             "repetition": rep + 1,
             "order": ["before", "after"] if (index + rep) % 2 == 0 else ["after", "before"]}
            for rep in range(REPETITIONS) for index, cell in enumerate(ordered)]


def prepare(inventory_path, destination):
    inventory = json.loads(inventory_path.read_text())
    require(inventory["counts"]["selected_pair_case_cells"] == 45, "Expected fixed 45-cell cohort")
    first = json.loads(Path(inventory["sources"]["original-15"]["frozen"]["path"]).read_text())
    files = [binding(inventory_path), binding(HERE / "ROOT-PLAN.json"), binding(Path(__file__))]
    require(digest(CAMPAIGN) == CAMPAIGN_SHA, "Changed original campaign.py")
    for path, sha in first["implementation"].items():
        item = {"path": path, "sha256": sha}
        verify(item)
        files.append(item)
    for source in inventory["sources"].values():
        verify(source["frozen"])
        files.append(source["frozen"])
    arms = {}
    for sha, entry in inventory["arms_by_binary_sha256"].items():
        arm = entry["arm"]
        require(arm["flags"] == "" and arm["preset"] == "otf_sparse_formula_parity", "Primary treatment changed")
        require(digest(arm["binary"]) == sha, "Frozen ELF changed")
        arms[sha] = arm
        files.append({"path": arm["binary"], "sha256": sha})
        if arm.get("provenance"):
            verify(arm["provenance"])
            files.append(arm["provenance"])
            proof = json.loads(Path(arm["provenance"]["path"]).read_text())
            if proof.get("postbuild_proof"):
                verify(proof["postbuild_proof"])
                files.append(proof["postbuild_proof"])
                proof = json.loads(Path(proof["postbuild_proof"]["path"]).read_text())
            libraries = proof.get("runtime_libraries_unique", proof.get("runtime_libraries", []))
            require(len(libraries) == 8, "Expected actual frozen eight-library closure")
            for library in libraries:
                verify(library)
                files.append(library)
    files.extend(first["inputs"].values())
    with Path(first["inputs"]["tlsf_map"]["path"]).open() as stream:
        mapping = {r["instance"]: r["tlsf"] for r in csv.DictReader(stream, delimiter="\t")}
    for name in inventory["counts"]["instances"]:
        files.append(binding(Path(first["tlsf_corpus"]) / mapping[name]))
    plan = {"status": "prepared-not-released", "schema_version": 1,
            "inventory": binding(inventory_path), "root_policy": binding(HERE / "ROOT-PLAN.json"),
            "implementation": binding(Path(__file__)), "interpreter": binding(sys.executable),
            "repo": first["repo"], "inputs": first["inputs"], "tlsf_corpus": first["tlsf_corpus"],
            "host_lock": first["host_lock"], "environment": first["environment"],
            "cap_s": CAP, "memory_max": "8G", "memory_swap_max": "0", "allowed_cpus": None,
            "cpu_quota": None, "collect_rusage": True, "seed": 260617,
            "arms": arms, "pairs": inventory["pairs"],
            "cells": [{k: c[k] for k in ("cell_id", "pair_id", "instance", "selection_reasons")} for c in inventory["cells"]],
            "schedule": schedule(inventory["cells"], 260617),
            "bindings": list({(x["path"], x["sha256"]): x for x in files}.values()),
            "attribution": "Historical backend-versus-ranked comparisons cannot alone trigger optimization rollback",
            "diagnostics_34_s": "excluded; requires a separate symmetric frozen plan if later authorized"}
    validate(plan)
    write_new(destination, plan)


def validate(plan):
    verify(plan["inventory"])
    inventory = json.loads(Path(plan["inventory"]["path"]).read_text())
    require(plan["pairs"] == inventory["pairs"], "Pair identities/cohorts changed")
    require(plan["cells"] == [{k: c[k] for k in ("cell_id", "pair_id", "instance", "selection_reasons")} for c in inventory["cells"]], "Selected cohort changed")
    require(plan["arms"] == {sha: entry["arm"] for sha, entry in inventory["arms_by_binary_sha256"].items()}, "Frozen arm identities changed")
    require(plan["cap_s"] == CAP and plan["memory_max"] == "8G" and plan["memory_swap_max"] == "0", "Wrong limits")
    require(plan["allowed_cpus"] is None and plan["cpu_quota"] is None and plan["collect_rusage"] is True, "Wrong resource treatment")
    require(len(plan["arms"]) == 18 and len(plan["cells"]) == 45, "Wrong arm/cell universe")
    cells = {c["cell_id"]: c for c in plan["cells"]}
    pairs = {p["pair_id"]: p for p in plan["pairs"]}
    require(len(cells) == 45 and len(pairs) == len(plan["pairs"]), "Duplicate cell/pair")
    require(len({(c["pair_id"], c["instance"]) for c in cells.values()}) == 45, "Duplicate pair-case")
    expected = schedule(plan["cells"], plan["seed"])
    require(plan["schedule"] == expected, "Schedule changed")
    require(len(expected) == 225 and len({x["id"] for x in expected}) == 225, "Wrong repetition universe")
    for c in cells.values():
        p = pairs[c["pair_id"]]
        require(c["instance"] in p["selected_instances"], "Cell outside pair cohort")
        for role in ("before", "after"):
            sha = p[f"{role}_binary_sha256"]
            arm = plan["arms"][sha]
            require(arm["binary_sha256"] == sha and arm["flags"] == "" and arm["preset"] == "otf_sparse_formula_parity", "Arm treatment changed")


def environment(plan):
    hazards = [k for k in os.environ if k.startswith("ACACIA_") and k not in {"ACACIA_CAMPAIGN_SCOPE_GUARD", "ACACIA_ALLOW_STRAY_SCOPES"}]
    require(not hazards, f"Inherited runtime/diagnostic variables: {hazards}")
    actual = {k: v for k, v in os.environ.items() if k.startswith(("ACACIA_", "LD_", "MALLOC_", "GLIBC_", "OMP_", "SPOT_")) and k != "ACACIA_CAMPAIGN_SCOPE_GUARD"}
    require(actual == plan["environment"], "Runtime environment differs from original frozen campaign")
    return dict(os.environ, ACACIA_CAMPAIGN_SCOPE_GUARD=os.environ.get("ACACIA_CAMPAIGN_SCOPE_GUARD") or f"deadline-repeats:{os.getpid()}", PYTHONDONTWRITEBYTECODE="1")


def campaign_module():
    require(digest(CAMPAIGN) == CAMPAIGN_SHA, "Changed campaign helper")
    spec = importlib.util.spec_from_file_location("original_campaign", CAMPAIGN)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def score(row):
    solved = row["result"] in {"REALIZABLE", "UNREALIZABLE"} and float(row["seconds"]) <= CAP
    return {"solved": solved, "par2": float(row["seconds"]) if solved else 2 * CAP,
            "wall_cap_overrun": row["result"] in {"REALIZABLE", "UNREALIZABLE"} and float(row["seconds"]) > CAP}


def cell_summary(before, after):
    require(len(before) == len(after) == REPETITIONS, "Incomplete paired cell")
    a, b = [score(r) for r in before], [score(r) for r in after]
    all_solved = all(x["solved"] for x in a + b)
    ratios = ([float(y["seconds"]) / float(x["seconds"]) for x, y in zip(before, after)]
              if all_solved and all(float(x["seconds"]) > 0 for x in before) else None)
    solve_loss = sum(x["solved"] for x in a) >= 4 and sum(x["solved"] for x in b) <= 1
    timing_loss = ratios is not None and median(ratios) >= 1.10 and sum(r > 1 for r in ratios) >= 4
    return {"before_solved": sum(x["solved"] for x in a), "after_solved": sum(x["solved"] for x in b),
            "paired_ratios_after_over_before": ratios, "median_paired_ratio": median(ratios) if ratios else None,
            "before_par2_sum": sum(x["par2"] for x in a), "after_par2_sum": sum(x["par2"] for x in b),
            "serious_solve_loss": solve_loss, "serious_timing_loss": timing_loss,
            "censored": not all_solved, "attribution": "repeat signal only; same-backend matched controls required for rollback"}


def resource_coverage(rows):
    resources = {}
    for field in ("cpu_seconds", "max_process_rss_bytes", "scope_memory_peak_bytes"):
        values = []
        for row in rows:
            raw = row.get(field, "")
            if raw == "":
                continue
            try:
                value = float(raw) if field == "cpu_seconds" else int(raw)
            except (ValueError, TypeError, OverflowError):
                raise ValueError(f"Invalid resource {field}: {raw!r}") from None
            require(value >= 0 and (field != "cpu_seconds" or math.isfinite(value)), f"Invalid resource {field}: {raw!r}")
            values.append(value)
        resources[field] = {"valid": len(values), "total": len(rows),
                            "aggregate": (sum(values) if field == "cpu_seconds" else max(values)) if len(values) == len(rows) and values else None}
    events = [json.loads(r["scope_memory_events"]) for r in rows if r.get("scope_memory_events", "")]
    for event in events:
        require(type(event.get("oom_kill")) is int and event["oom_kill"] >= 0, "Invalid OOM counter")
    resources["oom_events"] = {"valid": len(events), "total": len(rows),
                               "oom_kill_observed_invocations": sum(e["oom_kill"] > 0 for e in events),
                               "oom_kill_counter_sum": sum(e["oom_kill"] for e in events)}
    return resources


def interrupted(signum, _frame):
    raise SystemExit(128 + signum)


def execute(plan, plan_path, output, summary_only=False):
    validate(plan)
    for item in plan["bindings"] + [plan["interpreter"]]:
        verify(item)
    require(binding(sys.executable) == plan["interpreter"], "Interpreter changed")
    child_env = environment(plan)
    campaign = campaign_module()
    benchlib, coverage, _reporter = campaign.load_support(Path(plan["repo"]))
    output.mkdir(parents=True, exist_ok=True)
    with Path(plan["host_lock"]).open("a") as host, (output / "driver.lock").open("a") as local, ExitStack() as resources:
        try:
            fcntl.flock(host, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(local, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("PAUSED: benchmark host/output lock is held", flush=True)
            return 75
        frozen = {"plan": binding(plan_path), "invocation": sys.argv, "environment": plan["environment"]}
        if (output / "frozen.json").exists():
            require(json.loads((output / "frozen.json").read_text())["plan"] == frozen["plan"], "Resume plan changed")
        else:
            require(not summary_only and not (output / "legs").exists(), "Unbound output")
            write_new(output / "frozen.json", frozen)
            campaign.atomic_json(output / "host.json", {"uname": list(os.uname()), "affinity": sorted(os.sched_getaffinity(0)), "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
        cells = {c["cell_id"]: c for c in plan["cells"]}
        pairs = {p["pair_id"]: p for p in plan["pairs"]}
        observed = {c: {"before": [], "after": []} for c in cells}
        truths = {}
        completed = 0
        sampler = resources.enter_context(benchlib.HostSampler(output / "thermal.tsv", interval=30)) if not summary_only else None
        for visit in plan["schedule"]:
            cell = cells[visit["cell_id"]]
            pair = pairs[cell["pair_id"]]
            for role in visit["order"]:
                arm = plan["arms"][pair[f"{role}_binary_sha256"]]
                leg = output / "legs" / visit["id"] / role
                record = leg / "record.json"
                cmd = campaign.command(plan, arm, leg / "case.list", leg)
                if record.exists():
                    saved = json.loads(record.read_text())
                    require(saved["plan_sha256"] == frozen["plan"]["sha256"] and saved["command"] == cmd, "Changed completed leg identity")
                    for item in saved["files"]:
                        verify(item)
                else:
                    require(not summary_only, f"Incomplete repeat: {visit['id']} {role}")
                    require(not leg.exists(), f"Interrupted/unbound leg preserved; adjudicate before resuming: {leg}")
                    scopes = campaign.running_scopes()
                    if scopes:
                        campaign.atomic_json(output / "progress.json", {"status": "paused_contention", "completed": completed, "scopes": scopes})
                        print(f"PAUSED: active Acacia scopes {scopes}", flush=True)
                        return 75
                    verify({"path": arm["binary"], "sha256": arm["binary_sha256"]})
                    leg.mkdir(parents=True)
                    (leg / "raw").mkdir()
                    (leg / "case.list").write_text(cell["instance"] + "\n")
                    write_new(leg / "started.json", {"plan_sha256": frozen["plan"]["sha256"], "visit": visit, "role": role, "command": cmd})
                    campaign.atomic_json(output / "progress.json", {"status": "running", "completed": completed, "total": 450, "visit": visit["id"], "role": role})
                    sampler.set_leg(f"{visit['id']}:{role}", campaign.raw_path(leg, arm))
                    started = time.time()
                    with (leg / "stdout.log").open("x") as stream:
                        child = subprocess.Popen(cmd, cwd=plan["repo"], env=child_env, stdout=stream, stderr=subprocess.STDOUT)
                        try:
                            code = child.wait()
                        except BaseException:
                            child.terminate()
                            child.wait(timeout=20)
                            raise
                        stream.flush()
                        os.fsync(stream.fileno())
                    saved = {"plan_sha256": frozen["plan"]["sha256"], "visit": visit, "role": role, "command": cmd,
                             "exit_code": code, "started_unix": started, "elapsed_seconds": time.time() - started,
                             "files": [binding(p) for p in sorted(leg.rglob("*")) if p.is_file()]}
                    write_new(record, saved)
                raw = campaign.raw_path(leg, arm)
                require(saved["exit_code"] == 0, f"Native CLI failed; evidence preserved: {record}")
                rows = campaign.read_rows(coverage, raw, arm, {cell["instance"]}, plan)
                require(len(rows) == 1, f"Expected exactly one row: {raw}")
                row = rows[0]
                resource_coverage([row])
                require(Path(str(raw) + ".memory.tsv").is_file(), f"Missing resource sidecar: {raw}")
                require(row["result"] in {"REALIZABLE", "UNREALIZABLE", "TIMEOUT", "MEMOUT", "UNKNOWN"}, f"Solver failure: {row}")
                conflicts = raw.with_name(f"{arm['label']}-conflicts.tsv")
                require(not conflicts.exists() or not coverage.load_conflicts(conflicts), f"Known truth conflict: {conflicts}")
                if row["result"] in coverage.DECISIVE_RESULTS:
                    previous = truths.setdefault(row["instance"], row["result"])
                    require(previous == row["result"], f"Opposite native verdict including over-cap result: {record}")
                observed[cell["cell_id"]][role].append({"repetition": visit["repetition"], "row": row, "record": binding(record)})
                completed += 1
            if not summary_only:
                campaign.atomic_json(output / "progress.json", {"status": "running", "completed": completed, "total": 450, "last_complete_pair": visit["id"]})
                print(f"completed={completed}/450 last_pair={visit['id']}", flush=True)
        results = []
        for cell in plan["cells"]:
            data = observed[cell["cell_id"]]
            for role in data:
                data[role].sort(key=lambda x: x["repetition"])
            results.append(dict(cell, **cell_summary([x["row"] for x in data["before"]], [x["row"] for x in data["after"]]), observations=data))
        all_rows = [x["row"] for data in observed.values() for role in data.values() for x in role]
        resources = resource_coverage(all_rows)
        for item in plan["bindings"] + [plan["interpreter"]]:
            verify(item)
        campaign.atomic_json(output / "summary.json", {"status": "complete", "plan": frozen["plan"], "rows": completed,
                             "native_outcomes": dict(Counter(r["result"] for r in all_rows)), "resource_coverage": resources,
                             "strict_wall_rule": "native decisive and seconds <= 17; all unsolved PAR2=34", "cells": results})
        campaign.atomic_json(output / "progress.json", {"status": "complete", "completed": completed, "total": 450})
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", type=Path, help="inventory.json; no solver calls")
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summary-only", action="store_true")
    args = parser.parse_args()
    if args.prepare:
        prepare(args.prepare.resolve(), args.plan.resolve())
        return 0
    plan = json.loads(args.plan.read_text())
    validate(plan)
    if args.dry_run:
        for item in plan["bindings"] + [plan["interpreter"]]:
            verify(item)
        environment(plan)
        print("DRY RUN: 45 pair-case cells, five repetitions, 450 native calls; no target execution")
        return 0
    require(args.output is not None, "--output required")
    try:
        return execute(plan, args.plan.resolve(), args.output.resolve(), args.summary_only)
    except BaseException as error:
        if args.output.exists():
            write_new(args.output / f"STOP-{time.time_ns()}.json", {"error": str(error), "type": type(error).__name__})
        raise


if __name__ == "__main__":
    for handled in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(handled, interrupted)
    raise SystemExit(main())
