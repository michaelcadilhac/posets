#!/usr/bin/env python3
"""Serial, resumable 17-second native SYNTCOMP26 backend campaign.

Uses the repository's runner and reporters; never builds or changes a checkout.
Run with --dry-run first. See README.txt for the frozen arms input format.
"""

import argparse
from collections import Counter
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import signal
import subprocess
import sys
import time
from types import SimpleNamespace
import uuid

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
TOTAL = 1524
CAP = 17
SUPPORT = ("run-syntcomp26-coverage.py", "benchlib.py", "scope_memory.py",
           "suite_paths.py", "cactus-report.py", "make-panel.py", "family_names.py")
HOST_LOCK = Path(f"/tmp/acacia-posets-benchmark-{os.getuid()}.lock")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def binding(path):
    path = Path(path).resolve(strict=True)
    return {"path": str(path), "sha256": digest(path)}


def atomic_json(path, value):
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def load_support(repo):
    sys.path.insert(0, str(repo / "benchmarking"))
    import benchlib
    coverage = benchlib.load_tool("benchmarking/run-syntcomp26-coverage.py")
    reporter = benchlib.load_tool("benchmarking/cactus-report.py")
    return benchlib, coverage, reporter


def schedule(instances, labels, size, seed):
    rng = random.Random(seed)
    names = list(instances)
    rng.shuffle(names)
    result = []
    # Every batch visits every arm; rotating a seeded order balances positions.
    order = list(labels)
    rng.shuffle(order)
    for index, start in enumerate(range(0, len(names), size)):
        offset = index % len(order)
        result.append({"index": index, "instances": names[start:start + size],
                       "arms": order[offset:] + order[:offset]})
    return result


def freeze(args, coverage):
    hazards = [key for key in os.environ if key.startswith("ACACIA_")
               and key not in {"ACACIA_CAMPAIGN_SCOPE_GUARD", "ACACIA_ALLOW_STRAY_SCOPES"}]
    require(not hazards, f"Unset inherited Acacia runtime/diagnostic variables for the primary run: {hazards}")
    payload = json.loads(args.arms.read_text())
    arms = payload if isinstance(payload, list) else payload["arms"]
    require(isinstance(arms, list) and len(arms) >= 2, "Need at least two arms")
    normalized = []
    for original in arms:
        arm = dict(original)
        label = arm.get("label", "")
        require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", label), f"Unsafe label: {label!r}")
        binary = binding(arm["binary"])
        require(os.access(binary["path"], os.X_OK), f"Not executable: {binary['path']}")
        require(not arm.get("binary_sha256") or arm["binary_sha256"] == binary["sha256"],
                f"{label}: supplied binary SHA-256 differs")
        require(arm.get("acacia_sha") and arm.get("source_pins"), f"{label}: source pins required")
        arm.update(binary=binary["path"], binary_sha256=binary["sha256"],
                   flags=arm.get("flags", ""), preset=arm.get("preset", ""))
        require(isinstance(arm["flags"], str), f"{label}: flags must be a shell-token string")
        shlex.split(arm["flags"])
        if arm.get("provenance"):
            arm["provenance"] = binding(arm["provenance"])
        normalized.append(arm)
    labels = [arm["label"] for arm in normalized]
    require(len(set(labels)) == len(labels), "Repeated arm labels")
    baseline = labels[0] if isinstance(payload, list) else payload.get("baseline", labels[0])
    require(baseline in labels, "Baseline must name an arm")
    instances = coverage.read_instance_list(args.list)
    require(len(instances) == TOTAL and len(set(instances)) == TOTAL,
            f"Need exactly {TOTAL} distinct corpus instances")
    mapping = coverage.read_tlsf_map(args.tlsf_map)
    targets = coverage.resolve_targets(instances, mapping, args.tlsf_corpus)
    corpus_digest = hashlib.sha256()
    for name in instances:
        tlsf, path = targets[name]
        corpus_digest.update(json.dumps([name, tlsf, digest(path)], separators=(",", ":")).encode() + b"\n")
    require(args.batch_size > 0, "Batch size must be positive")
    environment = {key: value for key, value in os.environ.items()
                   if key.startswith(("ACACIA_", "LD_", "MALLOC_", "GLIBC_", "OMP_", "SPOT_"))
                   and key != "ACACIA_CAMPAIGN_SCOPE_GUARD"}
    return {"schema_version": 1, "repo": str(args.repo), "baseline": baseline,
            "arms": normalized, "cap_s": CAP, "memory_max": "8G", "memory_swap_max": "0",
            "allowed_cpus": args.allowed_cpus, "cpu_quota": args.cpu_quota,
            "collect_rusage": True, "environment": environment,
            "host_lock": str(HOST_LOCK),
            "inputs": {key: binding(getattr(args, key))
                       for key in ("list", "tlsf_map", "status_exceptions")},
            "tlsf_corpus": str(args.tlsf_corpus), "corpus_sha256": corpus_digest.hexdigest(),
            "implementation": {str(args.repo / "benchmarking" / name): digest(args.repo / "benchmarking" / name)
                               for name in SUPPORT} | {str(Path(__file__).resolve()): digest(__file__)},
            "batch_size": args.batch_size, "seed": args.seed,
            "schedule": schedule(instances, labels, args.batch_size, args.seed)}


def verify_files(manifest, arm=None):
    paths = dict(manifest["implementation"])
    paths.update({item["path"]: item["sha256"] for item in manifest["inputs"].values()})
    for current in ([arm] if arm else manifest["arms"]):
        paths[current["binary"]] = current["binary_sha256"]
        if current.get("provenance"):
            paths[current["provenance"]["path"]] = current["provenance"]["sha256"]
    for path, expected in paths.items():
        require(digest(path) == expected, f"Frozen input changed: {path}")


def raw_path(output, arm):
    return output / "raw" / f"{arm['label']}.tsv"


def read_rows(coverage, path, arm, instances, manifest):
    rows = coverage.load_output(path) if path.exists() else []
    mapping = coverage.read_tlsf_map(Path(manifest["inputs"]["tlsf_map"]["path"]))
    seen = set()
    expected = {"solver_label": arm["label"], "cap_s": str(CAP),
                "acacia_sha": arm["acacia_sha"], "binary_sha256": arm["binary_sha256"],
                "flags": arm["flags"], "preset": arm["preset"], "memory_max": "8G",
                "memory_swap_max": "0", "collect_rusage": "true",
                "allowed_cpus": manifest["allowed_cpus"] or "", "cpu_quota": manifest["cpu_quota"] or ""}
    for row in rows:
        name = row["instance"]
        require(name in instances and name not in seen, f"{path}: extra or duplicate instance {name}")
        require(row["tlsf_file"] == mapping[name], f"{path}: TLSF target differs at {name}")
        require(all(row[key] == value for key, value in expected.items()), f"{path}: treatment differs at {name}")
        require(math.isfinite(float(row["seconds"])) and float(row["seconds"]) >= 0,
                f"{path}: invalid measured time at {name}")
        if row["result"] in coverage.DECISIVE_RESULTS:
            require(row["exit_code"] == str(coverage.TOOL_EXIT_CODES["acacia"][row["result"]]),
                    f"{path}: decisive result contradicts exit at {name}")
        seen.add(name)
    return rows


def command(manifest, arm, batch_list, output):
    cmd = [sys.executable, "-u", str(Path(manifest["repo"]) / "benchmarking/run-syntcomp26-coverage.py"),
           "--bin", arm["binary"], "--solver-label", arm["label"], "--list", str(batch_list),
           "--tlsf-map", manifest["inputs"]["tlsf_map"]["path"], "--tlsf-corpus", manifest["tlsf_corpus"],
           "--status-exceptions", manifest["inputs"]["status_exceptions"]["path"],
           "--caps", str(CAP), "--memory-max", "8G", "--memory-swap-max", "0",
           "--output", str(raw_path(output, arm)), "--resume", "--conflict-policy", "stop",
           "--acacia-sha", arm["acacia_sha"], "--preset", arm["preset"], "--collect-rusage",
           "--flags=" + arm["flags"]]
    for key in ("allowed_cpus", "cpu_quota"):
        if manifest[key]:
            cmd += ["--" + key.replace("_", "-"), manifest[key]]
    return cmd


def running_scopes():
    result = subprocess.run(["systemctl", "--user", "list-units", "--type=scope", "--all",
                             "--plain", "--no-legend", "acacia-*"], capture_output=True, text=True,
                            timeout=5)
    require(result.returncode == 0, f"Cannot verify scope isolation: {result.stderr.strip()}")
    found = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if (len(fields) >= 4 and fields[0].startswith("acacia-") and fields[0].endswith(".scope")
                and fields[2] in {"active", "activating", "deactivating", "reloading", "refreshing"}):
            found.append(fields[0])
    return found


def cross_arm_conflicts(observed):
    verdicts = {}
    for label, rows in observed.items():
        for row in rows:
            if row["result"] in {"REALIZABLE", "UNREALIZABLE"}:
                verdicts.setdefault(row["instance"], {})[label] = row["result"]
    return [{"instance": name, "verdicts": answers} for name, answers in sorted(verdicts.items())
            if len(set(answers.values())) > 1]


def scoring_views(manifest, output, by_label, coverage):
    directory = output / "strict-wall-scoring"
    directory.mkdir(exist_ok=True)
    sources = {}
    for arm in manifest["arms"]:
        label = arm["label"]
        raw = raw_path(output, arm)
        rows = [dict(row) for row in by_label[label]]
        overruns = []
        for row in rows:
            if row["result"] in coverage.DECISIVE_RESULTS and float(row["seconds"]) > CAP:
                overruns.append({"instance": row["instance"], "native_result": row["result"],
                                 "native_exit_code": row["exit_code"], "native_seconds": row["seconds"],
                                 "scored_result": "TIMEOUT", "category": "wall-cap-overrun"})
                row["result"] = "TIMEOUT"
        scored = directory / f"{label}.tsv"
        coverage.atomic_write_tsv(scored, coverage.OUTPUT_COLUMNS, rows)
        # The transform preserves every memory join key; only result changes.
        coverage.write_memory_sidecar(scored, rows)
        native_memory = Path(str(raw) + ".memory.tsv")
        sources[label] = {"raw": binding(raw), "raw_memory": binding(native_memory) if native_memory.exists() else None,
                          "scored": binding(scored), "scored_memory": binding(Path(str(scored) + ".memory.tsv")),
                          "wall_cap_overruns": overruns}
    policy = {"cap_s": CAP, "solved_wall_rule": "native decisive verdict and measured seconds <= 17",
              "transform": "Only native decisive rows with seconds > 17 become TIMEOUT in the scoring view; all other fields remain unchanged. This is a scoring category, not evidence that the native timer fired.",
              "memory_binding": "All native memory join keys and observations are unchanged; the scoring memory sidecar is copied from loaded native observations.",
              "sources": sources}
    atomic_json(directory / "provenance.json", policy)
    return policy


def summarize(manifest, output, coverage, reporter):
    instances = coverage.read_instance_list(Path(manifest["inputs"]["list"]["path"]))
    arms = manifest["arms"]
    by_label = {arm["label"]: read_rows(coverage, raw_path(output, arm), arm, set(instances), manifest)
                for arm in arms}
    counts = {label: len(rows) for label, rows in by_label.items()}
    require(all(count == TOTAL for count in counts.values()), f"Summary requires exact full coverage: {counts}")
    scoring = scoring_views(manifest, output, by_label, coverage)
    # Use the repository reporter for scoring, expected-status checks and joins.
    args = SimpleNamespace(cap=CAP, list=Path(manifest["inputs"]["list"]["path"]),
                           tlsf_map=Path(manifest["inputs"]["tlsf_map"]["path"]),
                           tlsf_corpus=Path(manifest["tlsf_corpus"]),
                           status_exceptions=Path(manifest["inputs"]["status_exceptions"]["path"]),
                           series=[f"{arm['label']}={scoring['sources'][arm['label']]['scored']['path']}" for arm in arms],
                           internal=[], new=manifest["baseline"], legacy_audit=[], untimed_conversion=[],
                           manifest=[], noise_floor=None, noise_floor_source="")
    report, _views = reporter.threeway_join(args)
    require(report["corpus_sha256"] == manifest["corpus_sha256"], "Corpus changed during the campaign")
    # Truth checks always inspect native verdicts, including wall-cap overruns.
    expected, origins = reporter.threeway_expected_verdicts(instances, args.tlsf_map, args.tlsf_corpus,
                                                           args.status_exceptions)
    truth_conflicts = []
    for label, rows in by_label.items():
        for row in rows:
            if row["result"] in coverage.DECISIVE_RESULTS and expected.get(row["instance"]) not in (None, row["result"]):
                truth_conflicts.append({"instance": row["instance"], "series": label, "verdict": row["result"],
                                        "against": f"expected ({origins[row['instance']]})",
                                        "other_verdict": expected[row["instance"]]})
    for conflict in cross_arm_conflicts(by_label):
        truth_conflicts.append({"instance": conflict["instance"], "series": ",".join(conflict["verdicts"]),
                                "verdict": conflict["verdicts"], "against": "each other", "other_verdict": ""})
    report["conflicts"] = truth_conflicts
    report["strict_wall_scoring"] = scoring
    for arm in arms:
        label = arm["label"]
        rows = by_label[label]
        entry = report["series"][label]
        conflicts_path = raw_path(output, arm).with_name(f"{label}-conflicts.tsv")
        entry["collected_conflicts"] = coverage.load_conflicts(conflicts_path) if conflicts_path.exists() else []
        entry["truth_conflict_instances"] = sorted({conflict["instance"] for conflict in truth_conflicts
                                                    if label in conflict["series"].split(",")} |
                                                   {conflict["instance"] for conflict in entry["collected_conflicts"]})
        entry["source_pins"] = arm["source_pins"]
        entry["frozen_arm"] = arm
        entry["native_outcomes"] = dict(Counter(row["result"] for row in rows))
        entry["wall_cap_overruns"] = scoring["sources"][label]["wall_cap_overruns"]
        entry["native_raw"] = scoring["sources"][label]["raw"]
        cpu = [float(row["cpu_seconds"]) for row in rows if row["cpu_seconds"]]
        require(all(math.isfinite(value) and value >= 0 for value in cpu), f"{label}: invalid CPU measurement")
        entry["cpu"] = {"observed_rows": len(cpu), "expected_rows": TOTAL,
                        "total_seconds": sum(cpu) if len(cpu) == TOTAL else None}
        for key in ("scope", "process_rss"):
            memory = entry["peak_memory"][key]
            if memory["numerator"] != TOTAL:
                memory["max_bytes"] = None
                memory["median_bytes"] = None
        scope = entry["peak_memory"]["scope"]
        entry["peak_memory"]["max_mib"] = (round(scope["max_bytes"] / 2**20, 3)
                                                   if scope["max_bytes"] is not None else "missing")
        entry["peak_memory"]["median_mib"] = (round(scope["median_bytes"] / 2**20, 3)
                                                      if scope["median_bytes"] is not None else "missing")
    report["candidate_vs_baseline"] = {
        label: {"gains": pair["losses"], "losses": pair["gains"],
                "par2_delta_s": -pair["par2_delta_s"]} for label, pair in report["paired"].items()}
    report["frozen_manifest_sha256"] = digest(output / "frozen.json")
    report["cpu_host"] = json.loads((output / "host.json").read_text())
    report["usable"] = not report["conflicts"] and not any(
        entry["collected_conflicts"] for entry in report["series"].values())
    atomic_json(output / "summary.json", report)
    lines = [f"17-second SYNTCOMP26: {TOTAL} instances per arm.", "",
             "| Arm | Solved | PAR2 total s | CPU s | Peak MiB | Conflicts | Wall overruns | Native failure kinds |",
             "|---|---:|---:|---:|---:|---:|---:|---|"]
    for label, entry in report["series"].items():
        failures = ", ".join(f"{key}={value}" for key, value in entry["native_outcomes"].items()
                             if key not in coverage.DECISIVE_RESULTS) or "none"
        cpu = entry["cpu"]["total_seconds"]
        lines.append(f"| {label} | {entry['solved']} | {entry['par2_total_s']:.3f} | "
                     f"{f'{cpu:.3f}' if cpu is not None else 'missing'} | {entry['peak_memory']['max_mib']} | "
                     f"{len(entry['truth_conflict_instances'])} | {len(entry['wall_cap_overruns'])} | {failures} |")
    lines += ["", f"Candidate gains/losses against {manifest['baseline']}; PAR2 delta is candidate minus baseline.", ""]
    for label, pair in report["candidate_vs_baseline"].items():
        lines.append(f"- {label}: {len(pair['gains'])} gains, {len(pair['losses'])} losses; PAR2 {pair['par2_delta_s']:+.3f} s.")
    lines += ["", "All nonanswers are charged 34 seconds in PAR2. No noise floor was measured.",
              "Strict-wall scoring requires measured seconds <= 17. Successful native overruns are a separate wall-cap-overrun category; the scoring view calls them TIMEOUT without claiming the native timer fired. Native verdicts/times remain unchanged and participate in truth-conflict checks.",
              "Peak/median memory and total CPU are reported only with all 1524 valid observations.",
              "CPU host details, source/binary hashes, commands, memory gaps, conflicts and per-instance pairs are in summary.json and frozen.json."]
    if not report["usable"]:
        lines += ["", "NOT FINAL: verdict conflicts require adjudication before these scores can be used."]
    (output / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)
    return 0 if report["usable"] else 3


def execute(args, manifest, benchlib, coverage, reporter):
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    with HOST_LOCK.open("a") as host_lock, (output / "driver.lock").open("a") as lock:
        try:
            fcntl.flock(host_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print(f"PAUSED: another benchmark driver owns {HOST_LOCK}. Resume after it ends.", flush=True)
            return 75
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        frozen = output / "frozen.json"
        if frozen.exists():
            require(json.loads(frozen.read_text()) == manifest, "Resume configuration differs from frozen.json")
        else:
            require(not args.summary_only, "No frozen campaign to summarize")
            require(not any(output.glob("raw/*.tsv")), "Unbound raw observations already exist")
            atomic_json(frozen, manifest)
            cpu = subprocess.run(["lscpu", "--json"], capture_output=True, text=True, check=True)
            atomic_json(output / "host.json", {"lscpu": json.loads(cpu.stdout), "uname": list(os.uname()),
                                              "affinity": sorted(os.sched_getaffinity(0)),
                                              "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
        verify_files(manifest)
        if args.summary_only:
            return summarize(manifest, output, coverage, reporter)
        for name in ("raw", "lists", "logs"):
            (output / name).mkdir(exist_ok=True)
        instances = set(coverage.read_instance_list(args.list))
        arms = {arm["label"]: arm for arm in manifest["arms"]}
        observed = {label: read_rows(coverage, raw_path(output, arm), arm, instances, manifest)
                    for label, arm in arms.items()}
        done = {label: {row["instance"] for row in rows} for label, rows in observed.items()}
        state = {"status": "running", "expected_per_arm": TOTAL, "completed": {label: len(names) for label, names in done.items()}}
        child_env = dict(os.environ, ACACIA_CAMPAIGN_SCOPE_GUARD=os.environ.get("ACACIA_CAMPAIGN_SCOPE_GUARD")
                         or f"serial-posets-campaign:{os.getpid()}", PYTHONDONTWRITEBYTECODE="1")
        def halt_on_conflicts():
            cross = cross_arm_conflicts(observed)
            known = {}
            for label, arm in arms.items():
                path = raw_path(output, arm).with_name(f"{label}-conflicts.tsv")
                if path.exists() and (conflicts := coverage.load_conflicts(path)):
                    known[label] = conflicts
            if not cross and not known:
                return False
            atomic_json(output / "cross-arm-conflicts.json", cross)
            state.update(status="stopped_conflict", cross_arm_conflicts=len(cross),
                         known_conflicts={label: len(rows) for label, rows in known.items()})
            atomic_json(output / "progress.json", state)
            print(f"STOPPED: {len(cross)} cross-arm conflicts; known-status conflicts "
                  f"{state['known_conflicts']}. Adjudicate preserved evidence before resuming.", flush=True)
            return True
        if halt_on_conflicts():
            return 3
        # No global sweep: the native runner owns and cleans only each solver scope.
        with benchlib.HostSampler(output / "thermal.tsv", interval=30) as sampler:
            for batch in manifest["schedule"]:
                batch_list = output / "lists" / f"{batch['index']:04d}.list"
                content = "".join(name + "\n" for name in batch["instances"])
                if batch_list.exists():
                    require(batch_list.read_text() == content, f"Batch list changed: {batch_list}")
                else:
                    batch_list.write_text(content)
                for label in batch["arms"]:
                    if set(batch["instances"]) <= done[label]:
                        continue
                    scopes = running_scopes()
                    if scopes:
                        state.update(status="paused_contention", scopes=scopes, batch=batch["index"], arm=label)
                        atomic_json(output / "progress.json", state)
                        print(f"PAUSED: active Acacia scopes: {', '.join(scopes)}. Resume after contention clears.", flush=True)
                        return 75
                    arm = arms[label]
                    verify_files(manifest, arm)
                    cmd = command(manifest, arm, batch_list, output)
                    attempt = f"{batch['index']:04d}-{label}-{uuid.uuid4().hex[:8]}"
                    log = output / "logs" / f"{attempt}.log"
                    state.update(status="running", batch=batch["index"], arm=label, log=str(log), command=cmd)
                    atomic_json(output / "progress.json", state)
                    sampler.set_leg(f"{batch['index']:04d}:{label}", raw_path(output, arm))
                    print(f"batch={batch['index']} arm={label} completed={state['completed'][label]}/{TOTAL} log={log}", flush=True)
                    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
                    with log.open("x") as stream:
                        child = subprocess.Popen(cmd, cwd=manifest["repo"], env=child_env, stdout=subprocess.PIPE,
                                                 stderr=subprocess.STDOUT, text=True, bufsize=1)
                        try:
                            for line in child.stdout:
                                stream.write(line)
                                stream.flush()
                                if line.startswith("cap=17s instance="):
                                    match = re.search(r" instance=(.*?) result=", line)
                                    if match and match[1] not in done[label]:
                                        done[label].add(match[1])
                                        state["completed"][label] = len(done[label])
                                        atomic_json(output / "progress.json", state)
                            code = child.wait()
                            os.fsync(stream.fileno())
                        except BaseException:
                            # TERM lets the native scope owner clean its own invocation.
                            child.terminate()
                            child.wait(timeout=20)
                            raise
                    observed[label] = read_rows(coverage, raw_path(output, arm), arm, instances, manifest)
                    done[label] = {row["instance"] for row in observed[label]}
                    state["completed"][label] = len(done[label])
                    record = {"started_utc": started, "batch": batch["index"], "arm": label,
                              "command": cmd, "log": str(log), "exit_code": code,
                              "completed": len(done[label]), "log_sha256": digest(log)}
                    with (output / "invocations.jsonl").open("a") as stream:
                        stream.write(json.dumps(record, sort_keys=True) + "\n")
                        stream.flush()
                        os.fsync(stream.fileno())
                    if halt_on_conflicts():
                        return 3
                    require(code == 0, f"Native runner failed ({code}); see {log}")
                    require(set(batch["instances"]) <= done[label], f"Incomplete batch; see {log}")
                    atomic_json(output / "progress.json", state)
        code = summarize(manifest, output, coverage, reporter)
        state.update(status="complete" if code == 0 else "complete_conflicts", arm=None, command=None)
        atomic_json(output / "progress.json", state)
        return code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=HERE.parent / "acacia-posets-eval")
    parser.add_argument("--arms", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--list", type=Path)
    parser.add_argument("--tlsf-map", type=Path)
    parser.add_argument("--tlsf-corpus", type=Path, default=Path("/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus"))
    parser.add_argument("--status-exceptions", type=Path)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--seed", type=int, default=260617)
    parser.add_argument("--allowed-cpus")
    parser.add_argument("--cpu-quota")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summary-only", action="store_true")
    args = parser.parse_args(argv)
    args.repo = args.repo.resolve(strict=True)
    args.arms = args.arms.resolve(strict=True)
    args.output = args.output.resolve()
    args.list = (args.list or args.repo / "tests/suites/benchmarks/syntcomp26/all.list").resolve(strict=True)
    args.tlsf_map = (args.tlsf_map or args.repo / "tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv").resolve(strict=True)
    args.status_exceptions = (args.status_exceptions or args.repo / "benchmarking/syntcomp26-status-exceptions.tsv").resolve(strict=True)
    args.tlsf_corpus = args.tlsf_corpus.resolve(strict=True)
    benchlib, coverage, reporter = load_support(args.repo)
    manifest = freeze(args, coverage)
    if args.dry_run:
        print(json.dumps({"arms": manifest["arms"], "batches": len(manifest["schedule"]),
                          "instances_per_arm": TOTAL, "schedule_head": manifest["schedule"][:2],
                          "first_command": command(manifest, manifest["arms"][0], args.output / "lists/0000.list", args.output),
                          "guard": "Refuse active scopes before every batch; no global cleanup; no solver executed."}, indent=2))
        return 0
    return execute(args, manifest, benchlib, coverage, reporter)


if __name__ == "__main__":
    def interrupted(signum, frame):
        raise SystemExit(128 + signum)

    for handled in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(handled, interrupted)
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"campaign error: {error}", file=sys.stderr)
        raise SystemExit(2)
