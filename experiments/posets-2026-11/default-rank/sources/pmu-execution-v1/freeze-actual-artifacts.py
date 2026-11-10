"""Freeze actual four-PMU-artifact closure; observe ELF type and preserve duplicate deps."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

if not __debug__:
    raise SystemExit("Assertions must be enabled")
OUT = Path(__file__).resolve().parent
PHASE = OUT.parent
PLAN = PHASE / "pmu-plan-v1"
CHECKED = {}


def sha(path):
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    assert str(path) not in CHECKED or CHECKED[str(path)] == digest
    CHECKED[str(path)] = digest
    return digest


def binding(path):
    return {"path": str(path), "sha256": sha(path)}


def read(path):
    return json.loads(Path(path).read_text())


parser_path = PHASE / "profile-gate-results-v2/verify-artifact-closure-v4.py"
assert sha(parser_path) == "569bd2686d56419012d6c27caec07917f3286250a1b0602ebbfbb65b224bad66"
module_spec = importlib.util.spec_from_file_location("reviewed_make_decoder", parser_path)
parser = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(parser)
assert sha(PLAN / "source-manifest.json") == "2e72016b0d8358395ad0b329bb6b981695f0420889e51dc8f40c855badab8c5c"
manifest = read(PLAN / "source-manifest.json")
for name, digest in manifest["files"].items():
    assert sha(PLAN / name) == digest
draft_manifest_path = Path(manifest["source"]["path"])
assert sha(draft_manifest_path) == manifest["source"]["sha256"]
draft = draft_manifest_path.parent
known_private = {str(draft / name): digest for name, digest in read(draft_manifest_path)["files"].items()}
for path, digest in known_private.items():
    assert sha(path) == digest
prior = read(PHASE / "profile-gate-results-v2/actual-artifact-closure.json")
query_path = OUT / "actual-library-query-record.json"
query = read(query_path)
assert query["result"]["returncode"] == 0 and not query["result"]["timed_out"] and not query["result"]["resource_limited"]
assert sha(query["log"]) == query["log_sha256"]
libraries, current = {}, None
for line in Path(query["log"]).read_text().splitlines():
    if line.endswith(":"):
        current = line[:-1]
        assert current not in libraries
        libraries[current] = []
        continue
    assert current and "not found" not in line
    if line.strip().startswith("linux-vdso.so."):
        continue
    match = re.search(r'=>\s+(/\S+)\s+\(', line) or re.match(r'\s*(/\S+)\s+\(', line)
    assert match, line
    path = Path(match[1])
    libraries[current].append({"path": str(path), "realpath": str(path.resolve(strict=True)), "sha256": sha(path)})
recipes = read(PLAN / "build-recipes.json")["recipes"]
assert set(libraries) == {row["future_artifacts"]["ELF"]["path"] for row in recipes}
artifacts = []
for recipe in recipes:
    role, width = recipe["role"], recipe["width"]
    source = Path(recipe["source_root"])
    authority = prior["source_roles"][role]
    assert str(source) == authority["source"] and recipe["source_revision"] == authority["revision"]
    assert sha(authority["clean_anchor"]["path"]) == authority["clean_anchor"]["sha256"]
    anchor = read(authority["clean_anchor"]["path"])
    assert subprocess.check_output(["git", "--no-optional-locks", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() == recipe["source_revision"]
    assert subprocess.check_output(["git", "--no-optional-locks", "-C", str(source), "status", "--porcelain"], text=True) == ""
    for name, digest in anchor["source_sha256"].items():
        assert sha(source / name) == digest
    invocations = []
    for kind in ["compile", "link"]:
        stage = recipe[kind]
        record = read(stage["record"])
        assert record["spec"] == read(stage["spec"]) and record["spec_sha256"] == sha(stage["spec"])
        assert record["spec"]["command"] == stage["argv"] and record["spec"]["cwd"] == stage["cwd"]
        assert record["helper_sha256"] == "9fa7a6777374552f2b0ea95556896757f97cf5396689cdc2ef3026a795688e43" == sha(Path(stage["record"]).with_suffix(".helper.py"))
        assert sha(record["log"]) == record["log_sha256"]
        assert record["memory_max"] == "8G" and record["memory_swap_max"] == "0"
        result = record["result"]
        assert result["returncode"] == 0 and not result["timed_out"] and not result["resource_limited"]
        assert not any(json.loads(result["scope_memory_events"])[key] for key in ["oom", "oom_kill", "oom_group_kill"])
        invocations.append({"spec": binding(stage["spec"]), "record": binding(stage["record"]), "log": binding(record["log"]), "resources": result})
    args = recipe["compile"]["argv"]
    assert not any("POSETS_RANK_STATS" in word or "profile-draft" in word for word in args)
    target, tokens = parser.dep_rule(Path(recipe["future_artifacts"]["dependency"]["path"]).read_text())
    assert target == recipe["future_artifacts"]["object"]["path"]
    dependencies = []
    for token in tokens:
        path = Path(os.path.abspath(Path(recipe["compile"]["cwd"]) / token))
        if str(path) in known_private:
            assert path.resolve(strict=True) == path and sha(path) == known_private[str(path)]
            category = "reviewed-PMU-source"
        elif path.is_relative_to(source):
            relative = str(path.relative_to(source))
            assert path.resolve(strict=True) == path and relative in anchor["source_sha256"]
            assert sha(path) == anchor["source_sha256"][relative]
            category = "pinned-role-source"
        else:
            assert path.is_relative_to("/usr"), path
            category = "actual-system-header"
        dependencies.append({"lexical": token, "path": str(path), "realpath": str(path.resolve(strict=True)), "sha256": sha(path), "category": category})
    present = {row["path"] for row in dependencies}
    required = {str(draft / name) for name in ["rank-pmu.cc", "downset-bm-pmu.cc", "rank-pmu-hooks.hh"]}
    required.update(str(source / name) for name in ["tests/matrix-cpre.hh", "tests/matrix-check.hh", "tests/test_maker.hh", "include/posets/vectors.hh", "include/posets/downsets/rank_bucketed_vector_backed.hh"])
    assert required <= present
    files = {kind: binding(row["path"]) for kind, row in recipe["future_artifacts"].items() if isinstance(row, dict) and "path" in row}
    header = Path(files["ELF"]["path"]).read_bytes()[:64]
    assert header[:4] == b"\x7fELF"
    observed = {"class": header[4], "encoding": header[5], "type": struct.unpack_from("<H", header, 16)[0], "machine": struct.unpack_from("<H", header, 18)[0]}
    actual_libs = sorted(libraries[files["ELF"]["path"]], key=lambda row: row["path"])
    assert actual_libs == sorted(anchor["modes"][width]["libraries"], key=lambda row: row["path"])
    artifacts.append({"role": role, "width": width, "recipe": recipe, "files": files, "observed_ELF_header": observed,
                      "dependencies": dependencies, "runtime_libraries": actual_libs, "invocations": invocations})
result = {"status": "actual four-PMU-artifact closure locally verified; metadata/backend gates and measurement approval remain separate",
          "activation": binding(PHASE / "pmu-activation-v1.json"), "plan_manifest": binding(PLAN / "source-manifest.json"),
          "source_manifest": binding(draft_manifest_path), "closure_script": binding(__file__), "reused_Make_decoder": binding(parser_path),
          "artifacts": artifacts, "actual_library_query": binding(query_path),
          "compiler": binding(Path(shutil.which("c++"))), "taskset": binding(Path(shutil.which("taskset"))),
          "tool_context": {"uname": list(os.uname()), "outer_affinity": sorted(os.sched_getaffinity(0)),
                           "perf_event_paranoid": Path("/proc/sys/kernel/perf_event_paranoid").read_text().strip(),
                           "CPU2_present_in_owner_affinity": 2 in os.sched_getaffinity(0),
                           "event_permission_or_usability": "not probed; reserved for approved measurement calls"},
          "counts": {"ELFs": len(artifacts), "dependency_occurrences": sum(len(row["dependencies"]) for row in artifacts)},
          "checked_file_sha256": CHECKED}
with (OUT / "ACTUAL-ARTIFACT-CLOSURE.json").open("x") as stream:
    json.dump(result, stream, indent=2, sort_keys=True)
    stream.write("\n")
print(json.dumps({"status": result["status"], "counts": result["counts"]}))
