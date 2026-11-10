"""Read-only closing comparison against the approved, measured input bindings."""
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
from datetime import datetime, timezone

if not __debug__:
    raise SystemExit("Assertions must be enabled")

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = ROOT / "work"
CAMPAIGN = HERE.parent / "final-primary-default-rank-v3"
PREP = HERE.parent / "acacia-preparation-v3"
PROPOSAL = HERE.parent / "acacia-final-proposed-v3"
verified = {}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def check(path, expected):
    path = str(Path(path).resolve(strict=True))
    assert sha(path) == expected, path
    assert path not in verified or verified[path] == expected
    verified[path] = expected


def read(path):
    return json.loads(Path(path).read_text())


def bindings(value):
    if isinstance(value, dict):
        if isinstance(value.get("path"), str) and isinstance(value.get("sha256"), str):
            check(value["path"], value["sha256"])
        for child in value.values():
            bindings(child)
    elif isinstance(value, list):
        for child in value:
            bindings(child)


def git(source, *args):
    return subprocess.check_output(["git", "--no-optional-locks", "-C", str(source), *args])


for name in ["summary.json", "summary.txt", "progress.json", "frozen.json"]:
    assert (CAMPAIGN / name).read_bytes() == (HERE / ("automatic-" + name)).read_bytes(), name
check(CAMPAIGN / "frozen.json", "548ee0e77b9518a9f96a5f187b8f761966399509607c4fc393fa94a58e1cc5bb")
assert read(HERE / "summary-only-record.json")["exit_code"] == 0
execution = read(HERE / "APPROVAL-AND-EXECUTION.json")
bindings(execution)
checkpoint = read(PROPOSAL / "FINAL-REVIEW-CHECKPOINT.json")
bindings(checkpoint)
bindings(read(PROPOSAL / "ROOT-ACTUAL-FROZEN-ACCEPTANCE.json"))
manifest, summary, progress = [read(CAMPAIGN / name) for name in ["frozen.json", "summary.json", "progress.json"]]
assert progress["status"] == "complete"
assert progress["completed"] == {"baseline-default": 1524, "optimized-default": 1524}
assert summary["usable"] and not summary["conflicts"]
assert manifest["cap_s"] == 17 and manifest["memory_max"] == "8G" and manifest["memory_swap_max"] == "0"
assert manifest["batch_size"] == 32 and manifest["seed"] == 260617
assert manifest["allowed_cpus"] is None and manifest["cpu_quota"] is None
assert [arm["label"] for arm in manifest["arms"]] == ["baseline-default", "optimized-default"]
for path, digest in manifest["implementation"].items():
    check(path, digest)
bindings(manifest["inputs"])
source_facts = read(PREP / "materialized-sources.json")
role_checks = {}
for role, facts in source_facts["roles"].items():
    source = Path(facts["source"])
    counts = {}
    for key, base in [("actual_acacia_tracked_sha256", source),
                      ("actual_posets_tracked_sha256", source / "subprojects/posets")]:
        for path, digest in facts[key].items():
            check(base / path, digest)
        counts[key] = len(facts[key])
    for path, digest in facts["yyjson_copy"]["files"].items():
        check(Path(facts["yyjson_copy"]["copy"]) / path, digest)
    counts["yyjson_files"] = len(facts["yyjson_copy"]["files"])
    for project in ["acacia", "posets", "tlsf", "oxidd"]:
        old = facts[project]
        assert git(old["path"], "rev-parse", "HEAD").decode().strip() == old["revision"]
        assert git(old["path"], "status", "--porcelain").decode() == old["status"]
    assert git(source, "diff", "--binary", "--full-index", "HEAD") == (PREP / f"{role}-actual-acacia.patch").read_bytes()
    label = "baseline-default" if role == "baseline" else "optimized-default"
    provenance = read(PROPOSAL / f"{label}-provenance.json")
    bindings(provenance)
    proof_path = Path(provenance["postbuild_proof"]["path"])
    proof = read(proof_path)
    bindings(proof)
    binary = next(item["path"] for item in proof["artifacts"] if item["path"].endswith("/src/acacia-bonsai"))
    ldd = subprocess.run(["ldd", binary], capture_output=True, text=True, check=True)
    assert not ldd.stderr and "not found" not in ldd.stdout
    libraries = []
    for line in ldd.stdout.splitlines():
        match = re.search(r'=>\s+(/\S+)\s+\(', line) or re.match(r'\s*(/\S+)\s+\(', line)
        if match:
            path = str(Path(match[1]).resolve(strict=True))
            libraries.append({"path": path, "sha256": sha(path)})
    assert sorted(libraries, key=lambda item: item["path"]) == sorted(proof["runtime_libraries"], key=lambda item: item["path"])
    assert summary["series"][label]["cpu"]["observed_rows"] == 1524
    assert summary["series"][label]["peak_memory"]["scope"]["numerator"] == 1524
    assert summary["series"][label]["peak_memory"]["process_rss"]["numerator"] == 1524
    role_checks[role] = {"source_files": counts, "git_identities_and_status_unchanged": True,
                         "actual_full_acacia_diff_unchanged": True, "runtime_libraries": libraries,
                         "postbuild_proof": {"path": str(proof_path), "sha256": sha(proof_path)}}
old = read(WORK / "acacia-campaign/final-primary-execution-20261109/final-invariance-audit.json")
for path, digest in old["all_verified_file_sha256"].items():
    check(path, digest)
controller = runpy.run_path(str(WORK / "acacia-campaign/campaign.py"))
rows = [json.loads(line) for line in (CAMPAIGN / "invocations.jsonl").read_text().splitlines()]
expected = [(batch["index"], label) for batch in manifest["schedule"] for label in batch["arms"]]
assert [(row["batch"], row["arm"]) for row in rows] == expected
arms = {arm["label"]: arm for arm in manifest["arms"]}
for row in rows:
    assert row["exit_code"] == 0
    assert row["completed"] == min((row["batch"] + 1) * 32, 1524)
    check(row["log"], row["log_sha256"])
    batch_list = CAMPAIGN / "lists" / f"{row['batch']:04d}.list"
    batch = manifest["schedule"][row["batch"]]
    assert batch_list.read_text() == "".join(name + "\n" for name in batch["instances"])
    assert row["command"] == controller["command"](manifest, arms[row["arm"]], batch_list, CAMPAIGN)
for name in ["summary.json", "summary.txt", "progress.json", "frozen.json", "invocations.jsonl", "host.json"]:
    check(CAMPAIGN / name, sha(CAMPAIGN / name))
bindings(summary["strict_wall_scoring"]["sources"])
host = read(CAMPAIGN / "host.json")
start = datetime.fromisoformat(host["started_utc"].replace("Z", "+00:00")).timestamp()
completion = (CAMPAIGN / "progress.json").stat().st_mtime
result = {"status": "complete; measured source/artifact/helper/library/manifest bindings unchanged",
          "audited_utc": datetime.now(timezone.utc).isoformat(), "rows": 3048, "rows_per_arm": 1524,
          "roles": role_checks, "verified_checkpoint_bindings": len(checkpoint["bindings"]),
          "phase_one_prior_verified_files_rechecked": len(old["all_verified_file_sha256"]),
          "native_invocations": len(rows), "schedule_and_exact_commands_verified": True,
          "summary_only_identical_to_automatic": True,
          "cpu_valid_rows": 3048, "scope_memory_valid_rows": 3048, "process_rss_valid_rows": 3048,
          "native_truth_conflicts": summary["conflicts"],
          "wall_seconds_launch_to_complete_progress": completion - start,
          "source_attribution_limit": execution["source_attribution_limit"],
          "original_checkouts_closing_snapshot": "root owns separate original-WIP closing check",
          "all_verified_file_sha256": dict(sorted(verified.items()))}
with (HERE / "FINAL-INVARIANCE.json").open("x") as stream:
    json.dump(result, stream, indent=2, sort_keys=True)
    stream.write("\n")
print(json.dumps({key: value for key, value in result.items() if key not in {"all_verified_file_sha256", "roles"}}, sort_keys=True))
