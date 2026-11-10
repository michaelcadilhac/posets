"""Integrity checks for the offline Posets matrix; never launch a benchmark."""

import importlib.util
import copy
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import pytest

TOOLS = Path(__file__).resolve().parent
BENCHMARKING = TOOLS.parent.parent / "acacia-posets-eval/benchmarking"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(BENCHMARKING))
SPEC = importlib.util.spec_from_file_location("posets_matrix", TOOLS / "posets-matrix.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def ranked_map_fixture(tmp_path, monkeypatch):
    """Synthetic ELF observations only; source policy is tested separately below."""
    path, plan, output, observed, library, ninja = binary_map_fixture(tmp_path, monkeypatch)
    source = Path(plan["source_dir"])
    approved = tmp_path / "fixture-approval.json"
    approved.write_text("fixture authority; never measurement evidence\n")
    bind = lambda p: {"path": str(p), "sha256": M.sha(p)}
    plan.update(inventory=M.contract.INVENTORY, rank_source_role="baseline",
                rank_source_manifest=bind(approved), rank_contract=bind(Path(M.contract.__file__).resolve()))
    plan.pop("selector_overlay")
    plan["binaries"] = [row for row in plan["binaries"] if row["backend"] == M.contract.RANK]
    proof_path = Path(plan["anchor_provenance"]["path"])
    proof = json.loads(proof_path.read_text())
    anchor = proof["provenance"]
    anchor.update(posets_revision=plan["source_revision"], posets_source_dir=str(source),
                  rank_anchor=True, rank_source_role="baseline", rank_source_manifest=bind(approved))
    anchor["source_sha256"] = {name: M.sha(source / name) for name in anchor["source_sha256"]}
    commands = json.loads((source / "build/compile_commands.json").read_text())
    for mode in M.MODES:
        anchor["modes"][mode]["compiler_command"] = next(
            row for row in commands if row["output"] == f"tests/downset-matrix-{mode}.p/downset-bm.cc.o")
    proof_path.write_text(json.dumps(proof))
    plan["anchor_provenance"] = bind(proof_path)
    build_path = Path(json.loads(Path(plan["binaries"][0]["build_record"]["path"]).read_text())["build_plan"]["path"])
    build = json.loads(build_path.read_text())
    build.update({key: plan[key] for key in ("inventory", "rank_source_role", "rank_source_manifest", "rank_contract")})
    build["rows"] = [row for row in build["rows"] if row["backend"] == M.contract.RANK]
    build_path.write_text(json.dumps(build))
    for entry in plan["binaries"]:
        record_path = Path(entry["build_record"]["path"])
        record = json.loads(record_path.read_text())
        record.update(M.contract.source_fields(plan))
        record.pop("selector_overlay_sha256")
        record["build_plan"] = bind(build_path)
        record_path.write_text(json.dumps(record))
        entry["build_record"] = bind(record_path)
    path.write_text(json.dumps(plan))
    expected = (source, plan["base_revision"], plan["source_revision"], "baseline", approved)

    def source_policy(*values):
        M.require(values == expected, "Fixture source identity changed")
        return {"production_reference_revision": M.contract.PRODUCTION, "baseline_headers_cc_equivalent": True}

    monkeypatch.setattr(M.contract, "validate_source", source_policy)
    return path, plan, output, observed, library, ninja


def ranked_load(path, plan, output, enabled=True):
    return M.backend_provenance(Path(plan["source_oracle_binary"]["path"]).parent.parent,
                                output, None, path, enabled)[1]


def test_selected_map_requires_explicit_flag_and_real_singletons(tmp_path, monkeypatch):
    path, plan, output, _, _, _ = ranked_map_fixture(tmp_path, monkeypatch)
    with pytest.raises(ValueError, match="requires --rank-only"):
        ranked_load(path, plan, output, False)
    result = ranked_load(path, plan, output)
    assert all(set(rows) == {M.contract.RANK} for rows in result["backend_binaries"].values())
    assert all(row["backends"] == [M.contract.RANK] for row in result["modes"].values())
    assert all(row["backends"] == sorted(M.BACKENDS) for row in result["alias_probes"].values())
    assert result["source_oracle"]["binary"] == plan["source_oracle_binary"]["path"]


@pytest.mark.parametrize("mutation", ["missing", "extra", "duplicate", "wrong-backend", "wrong-width",
                                     "wrong-source", "singleton", "runtime-width", "compile-flag", "ISA-template"])
def test_selected_map_refreshed_hash_negatives(tmp_path, monkeypatch, mutation):
    path, plan, output, observed, _, _ = ranked_map_fixture(tmp_path, monkeypatch)
    entry = plan["binaries"][0]
    if mutation == "missing":
        plan["binaries"].pop()
    elif mutation in ("extra", "duplicate"):
        added = copy.deepcopy(entry)
        if mutation == "extra":
            added["backend"] = "comptrie_backed"
        plan["binaries"].append(added)
    elif mutation == "wrong-backend":
        entry["backend"] = "comptrie_backed"
    elif mutation == "wrong-width":
        entry["mode"] = "max"
    elif mutation == "wrong-source":
        wrong = tmp_path / "other-source"
        wrong.mkdir()
        plan["source_dir"] = str(wrong)
    elif mutation == "singleton":
        observed[entry["binary"]]["backends"] = ["comptrie_backed"]
    elif mutation == "runtime-width":
        observed[entry["binary"]]["width_selector"] = 1
    else:
        record_path = Path(entry["build_record"]["path"])
        record = json.loads(record_path.read_text())
        flag = "-O0" if mutation == "compile-flag" else "-mavx2"
        record["compiler_command"]["arguments"].append(flag)
        if mutation == "ISA-template":
            record["template_compiler_command"]["arguments"].append(flag)
            database = Path(record["compile_commands"]["path"])
            commands = json.loads(database.read_text())
            commands = [record["template_compiler_command"] if row["output"] ==
                        record["template_compiler_command"]["output"] else row for row in commands]
            database.write_text(json.dumps(commands))
        resync_build_record(plan, entry, record)
    path.write_text(json.dumps(plan))  # Outer hash and all modified child bindings are current.
    with pytest.raises(ValueError):
        ranked_load(path, plan, output)


def source_contract_fixture(tmp_path, monkeypatch):
    """Real temporary Git IDs, synthetic approved bytes; no compiler/ELF runs."""
    contract = M.contract
    repo = tmp_path / "repo"
    source = repo / "_deps/source"
    source.mkdir(parents=True)
    kernels = ("tests/downset-bm.cc", "tests/matrix-cpre.hh", "tests/matrix-check.hh", "tests/test_maker.hh")
    for name in set(kernels) | (contract.CHANGES - {"tests/rank-bucketed.cc"}):
        p = source / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("base fixture " + name + "\n")

    def git(*args):
        return subprocess.check_output(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                                        *args], cwd=source).decode().strip()

    git("init", "-q")
    git("add", ".")
    git("commit", "-qm", "fixture base")
    base = git("rev-parse", "HEAD")
    monkeypatch.setattr(contract, "REPO", repo)
    monkeypatch.setattr(contract, "BASE", base)
    monkeypatch.setattr(contract, "PRODUCTION", base)
    patch_hash = "d" * 64
    monkeypatch.setattr(contract, "APPROVED_PATCH", patch_hash)
    for name in contract.CHANGES:
        p = source / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("approved fixture " + name + "\n")
    git("add", ".")
    git("commit", "-qm", "fixture approved candidate")
    revision = git("rev-parse", "HEAD")
    manifest = tmp_path / "approved.json"
    manifest.write_text(json.dumps({"baseline_commit": base, "patch_sha256": patch_hash,
        "files": [{"path": name, "sha256": M.sha(source / name)} for name in sorted(contract.CHANGES)]}))
    monkeypatch.setattr(contract, "APPROVED_MANIFEST", M.sha(manifest))
    return source, base, revision, manifest, git


@pytest.mark.parametrize("mutation", ["header", "kernel", "extra-file", "refreshed-approval", "wrong-base", "wrong-role", "symlink"])
def test_rank_source_rejects_refreshed_wrong_sources(tmp_path, monkeypatch, mutation):
    source, base, revision, manifest, git = source_contract_fixture(tmp_path, monkeypatch)
    M.contract.validate_source(source, base, revision, "candidate", manifest)
    role = "candidate"
    if mutation == "wrong-base":
        base = "0" * 40
    elif mutation == "wrong-role":
        role = "baseline"
    else:
        name = ("tests/downset-bm.cc" if mutation == "kernel" else "unapproved.hh" if mutation == "extra-file"
                else "include/posets/downsets/rank_bucketed_vector_backed.hh")
        if mutation == "symlink":
            target = tmp_path / "unchanged-header-bytes.hh"
            target.write_bytes((source / name).read_bytes())
            (source / name).unlink()
            (source / name).symlink_to(target)
        else:
            (source / name).write_text("changed after approval\n")
        git("add", ".")
        git("commit", "-qm", "fixture changed source, current hashes")
        revision = git("rev-parse", "HEAD")
        if mutation == "refreshed-approval":
            value = json.loads(manifest.read_text())
            for item in value["files"]:
                item["sha256"] = M.sha(source / item["path"])
            manifest.write_text(json.dumps(value))
    with pytest.raises(ValueError):
        M.contract.validate_source(source, base, revision, role, manifest)


@pytest.mark.parametrize("arguments", [["-DPOSETS_RANK_STATS"], ["-DPOSETS_RANK_STATS=0"],
                                       ["-D", "POSETS_RANK_STATS=1"]])
def test_clean_selected_build_rejects_enabled_statistics(arguments):
    with pytest.raises(ValueError):
        M.contract.clean_flags(arguments)


def rank_command():
    return [sys.executable, "-B", str(M.contract.DRIVER), "--build-dir", "/fixture/build",
            "--source-dir", "/fixture/source", "--trace-plan", "/fixture/traces.json",
            "--output", "/fixture/output", "--binary-map", "/fixture/map.json", "--measure",
            "--rounds", "1", "--repetitions", "1", "--seed", "260617", "--source-timeout", "30",
            "--check-timeout", "2", "--case-timeout", "2", "--memory-max", "8G",
            "--rank-only", "--backends", M.contract.RANK]


@pytest.mark.parametrize("mutation", ["opt-in", "backends", "source-cap", "check-cap", "timing-cap",
                                     "duplicate-map", "detail"])
def test_selected_invocation_protocol_negatives(mutation):
    command = rank_command()
    M.contract.check_command(command)
    if mutation == "opt-in":
        command.remove("--rank-only")
    elif mutation == "backends":
        command.append("comptrie_backed")
    elif mutation == "duplicate-map":
        command.extend(["--binary-map", "/other/map.json"])
    elif mutation == "detail":
        command.append("--detail")
    else:
        option = {"source-cap": "--source-timeout", "check-cap": "--check-timeout",
                  "timing-cap": "--case-timeout"}[mutation]
        command[command.index(option) + 1] = "99"
    with pytest.raises(ValueError):
        M.contract.check_command(command)


@pytest.mark.parametrize("mutation", ["none", "candidate-map-opt-in", "candidate-role", "candidate-header",
                                     "plan-opt-in", "invocation-opt-in", "seed", "seed-equals",
                                     "seed-abbreviation", "source-equals", "map-equals", "output-abbreviation"])
def test_paired_rank_contract_checks_both_sides_before_invocation(tmp_path, monkeypatch, mutation):
    source, base, revision, manifest, git = source_contract_fixture(tmp_path, monkeypatch)
    baseline = source.parent / "baseline"
    shutil.copytree(source, baseline)
    subprocess.run(["git", "-C", str(baseline), "checkout", "--detach", base], check=True,
                   capture_output=True)
    contract = M.contract
    bind = lambda p: {"path": str(p), "sha256": M.sha(p)}
    variants, documents = {}, {}
    for label, role, tree, pin in [("baseline", "baseline", baseline, base),
                                   ("optimized", "candidate", source, revision)]:
        path = tmp_path / (label + "-map.json")
        metadata = {"inventory": contract.INVENTORY, "rank_source_role": role,
                    "rank_source_manifest": bind(manifest), "rank_contract": bind(Path(contract.__file__).resolve()),
                    "source_dir": str(tree), "source_revision": pin, "base_revision": base,
                    "binaries": [{"mode": mode, "backend": contract.RANK} for mode in ("compatible", "native")]}
        path.write_text(json.dumps(metadata))
        documents[label] = (path, metadata)
        variants[label] = {"rank_source_role": role, "revision": pin, "source": str(tree),
                           "build": str(tree / "fixture-build"), "binary_map": bind(path)}
    trace = tmp_path / "traces.json"
    trace.write_text("fixture trace-plan bytes; no workload\n")
    plan = {"inventory": contract.INVENTORY, "backends": [contract.RANK], "variants": variants,
            "source_revisions": {name: value["revision"] for name, value in variants.items()},
            "rank_contract": bind(Path(contract.__file__).resolve()), "driver": bind(contract.DRIVER),
            "trace_plan": bind(trace)}
    run = {"variant": "baseline", "pair": 0, "seed": 260617, "output": str(tmp_path / "output")}
    command = rank_command()
    for option, value in [("--build-dir", variants["baseline"]["build"]),
                          ("--source-dir", str(baseline)), ("--trace-plan", str(trace)),
                          ("--binary-map", variants["baseline"]["binary_map"]["path"]),
                          ("--output", run["output"])]:
        command[command.index(option) + 1] = value
    spec = {"inventory": contract.INVENTORY, "driver": plan["driver"], "command": command}
    contract.check_plan_run(plan, run, spec, variants["baseline"])
    path, candidate = documents["optimized"]
    if mutation == "candidate-map-opt-in":
        candidate.pop("inventory")
    elif mutation == "candidate-role":
        candidate["rank_source_role"] = variants["optimized"]["rank_source_role"] = "baseline"
    elif mutation == "candidate-header":
        (source / "include/posets/downsets/rank_bucketed_vector_backed.hh").write_text("unapproved\n")
        git("add", ".")
        git("commit", "-qm", "fixture altered candidate")
        pin = git("rev-parse", "HEAD")
        candidate["source_revision"] = variants["optimized"]["revision"] = pin
        plan["source_revisions"]["optimized"] = pin
    elif mutation == "plan-opt-in":
        plan.pop("inventory")
    elif mutation == "invocation-opt-in":
        command.remove("--rank-only")
    elif mutation == "seed":
        command[command.index("--seed") + 1] = "260618"
    elif mutation == "seed-equals":
        command.append("--seed=7")
    elif mutation == "seed-abbreviation":
        command.extend(["--see", "7"])
    elif mutation == "source-equals":
        command.append("--source-dir=/other/source")
    elif mutation == "map-equals":
        command.append("--binary-map=/other/map.json")
    elif mutation == "output-abbreviation":
        command.extend(["--out", "/other/output"])
    spec_path = tmp_path / "current-invocation.json"
    spec_path.write_text(json.dumps(spec))
    run["spec"] = bind(spec_path)  # Current invocation hash cannot authorize a hidden effective override.
    path.write_text(json.dumps(candidate))
    variants["optimized"]["binary_map"] = bind(path)  # Refresh outer binding, retain source authority.
    if mutation == "none":
        contract.check_plan_run(plan, run, spec, variants["baseline"])
    else:
        with pytest.raises(ValueError):
            contract.check_plan_run(plan, run, spec, variants["baseline"])


@pytest.mark.parametrize("extra", [
    ["--seed=7"], ["--see", "7"], ["--seed", "7"],
    ["--source-dir=/other/source"], ["--source-d", "/other/source"],
    ["--binary-map=/other/map.json"], ["--binary-m", "/other/map.json"],
    ["--build-dir=/other/build"], ["--build-d", "/other/build"],
    ["--trace-plan=/other/traces.json"], ["--trace-p", "/other/traces.json"],
    ["--output=/other/output"], ["--out", "/other/output"],
    ["--case-timeout=99"], ["--rounds=2"], ["--repetitions=2"],
    ["--memory-max=64G"], ["--backends=comptrie_backed"],
    ["--rank-on"], ["--meas"], ["--measure"], ["--"],
])
def test_selected_command_rejects_effective_argparse_overrides(extra):
    command = rank_command()
    assert M.contract.check_command(command)["--seed"] == "260617"
    with pytest.raises(ValueError):
        M.contract.check_command(command + extra)


@pytest.mark.parametrize("seed", [260618, 260617.0, True, None, "260617"])
def test_actual_report_seed_must_match_declared_integer(tmp_path, seed):
    run = {"seed": 260617}
    report = {"treatment": {"seed": 260617}}
    M.contract.check_report_seed(report, run)
    report["treatment"]["seed"] = seed
    path = tmp_path / "report.json"
    path.write_text(json.dumps(report))
    with pytest.raises(ValueError, match="report seed"):
        M.contract.check_report_seed(json.loads(path.read_text()), run)
    report["treatment"].pop("seed")
    with pytest.raises(ValueError, match="report seed"):
        M.contract.check_report_seed(report, run)


def paired_writer_fixture(tmp_path, monkeypatch):
    """Real source-authority checks; synthetic saved writer metadata, no ELF/build runs."""
    loader = importlib.util.spec_from_file_location("rank_paired_writer_fixture", TOOLS / "prepare-paired-replay.py")
    writer = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(writer)
    source, base, revision, manifest, _ = source_contract_fixture(tmp_path, monkeypatch)
    baseline = source.parent / "baseline"
    shutil.copytree(source, baseline)
    subprocess.run(["git", "-C", str(baseline), "checkout", "--detach", base], check=True,
                   capture_output=True)
    monkeypatch.setattr(writer, "REPO", M.contract.REPO)
    monkeypatch.setattr(writer, "load_tool", lambda _: M)  # Reuse actual normalization, not a mocked result.
    bind = lambda p: {"path": str(p), "sha256": M.sha(p)}
    library = tmp_path / "fixture-runtime.so"
    library.write_text("synthetic saved runtime context; never loaded\n")
    maps, documents, anchors, records = {}, {}, {}, {}
    for label, role, tree, pin in [("baseline", "baseline", baseline, base),
                                   ("optimized", "candidate", source, revision)]:
        build = source.parent / (label + "-writer-build")
        (build / "tests").mkdir(parents=True)
        modes = {}
        records[label] = {}
        binaries = []
        for mode in M.MODES:
            binary = build / "tests" / ("downset-matrix-" + mode)
            binary.write_bytes(b"synthetic writer binding; not an ELF")
            output = "tests/downset-matrix-" + mode + ".p/downset-bm.cc.o"
            compiler = {"directory": str(build), "file": str(tree / "tests/downset-bm.cc"),
                        "output": output, "arguments": ["c++", "-O3", "-flto", "-std=c++20",
                        "-DPOSETS_SIMD_WIDTH=POSETS_SIMD_WIDTH_" + mode.upper(),
                        "-I" + str(tree / "include"), "-c", str(tree / "tests/downset-bm.cc"), "-o", output]}
            modes[mode] = {"compiler": "synthetic fixture compiler", "simd_type": "fixture-vector",
                "selected_lanes": 16 if mode == "compatible" else 64,
                "alignment": 16 if mode == "compatible" else 64, "element_bytes": 1,
                "block_bytes": 16 if mode == "compatible" else 64, "compatible_lanes": 16,
                "native_lanes": 64, "max_fixed_lanes": 64, "hybrid_policy": "fixture-policy",
                "backends": sorted(M.BACKENDS), "compiler_command": compiler,
                "libraries": [bind(library)], "binary": str(binary), "binary_sha256": M.sha(binary)}
            if mode == "max":
                continue
            linker = {"directory": str(build), "output": "tests/downset-matrix-" + mode,
                      "arguments": ["c++", "-O3", "-flto", "-o", "tests/downset-matrix-" + mode, output]}
            record_path = tmp_path / (label + "-" + mode + "-writer-record.json")
            record_path.write_text(json.dumps({"template_compiler_command": compiler,
                                              "template_linker_command": linker}))
            records[label][mode] = record_path
            binaries.append({"mode": mode, "backend": M.contract.RANK, "build_record": bind(record_path)})
        anchor_path = tmp_path / (label + "-writer-anchor.json")
        anchor = {"posets_source_dir": str(tree), "posets_revision": pin, "rank_anchor": True,
                  "rank_source_role": role, "rank_source_manifest": bind(manifest), "modes": modes}
        anchor_path.write_text(json.dumps(anchor))
        path = tmp_path / (label + "-writer-map.json")
        metadata = {"schema_version": 1, "kind": "posets-per-backend-binaries",
                    "inventory": M.contract.INVENTORY, "rank_source_role": role,
                    "rank_source_manifest": bind(manifest), "rank_contract": bind(Path(M.contract.__file__).resolve()),
                    "base_revision": base, "source_revision": pin, "source_dir": str(tree),
                    "anchor_provenance": bind(anchor_path), "binaries": binaries,
                    "source_oracle_binary": bind(Path(modes["compatible"]["binary"]))}
        path.write_text(json.dumps(metadata))
        maps[label], documents[label], anchors[label] = path, metadata, anchor_path
    return writer, maps, documents, anchors, records


@pytest.mark.parametrize("mutation", ["none", "link-template", "runtime-libraries"])
def test_paired_writer_compares_actual_selected_contexts(tmp_path, monkeypatch, mutation):
    writer, maps, documents, anchors, records = paired_writer_fixture(tmp_path, monkeypatch)
    result = writer.mapped_variants(maps["baseline"], maps["optimized"], True)
    assert set(result) == {"baseline", "optimized"}
    assert result["optimized"]["rank_source_role"] == "candidate"
    if mutation == "none":
        return
    candidate = documents["optimized"]
    previous_map_binding = writer.binding(maps["optimized"])
    if mutation == "link-template":
        record_path = records["optimized"]["compatible"]
        record = json.loads(record_path.read_text())
        record["template_linker_command"]["arguments"].append("-Wl,--as-needed")
        record_path.write_text(json.dumps(record))
        candidate["binaries"][0]["build_record"] = writer.binding(record_path)
    else:
        library = tmp_path / "different-fixture-runtime.so"
        library.write_text("different synthetic runtime library context\n")
        anchor_path = anchors["optimized"]
        anchor = json.loads(anchor_path.read_text())
        for mode in M.MODES:
            anchor["modes"][mode]["libraries"] = [writer.binding(library)]
        anchor_path.write_text(json.dumps(anchor))
        candidate["anchor_provenance"] = writer.binding(anchor_path)
    maps["optimized"].write_text(json.dumps(candidate))
    assert writer.binding(maps["optimized"]) != previous_map_binding
    with pytest.raises(AssertionError, match="contexts differ"):
        writer.mapped_variants(maps["baseline"], maps["optimized"], True)


def test_selected_builder_inventory_keeps_full48_default():
    loader = importlib.util.spec_from_file_location("rank_build_fixture", TOOLS / "run-per-backend-build.py")
    builder = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(builder)
    selected = {"inventory": M.contract.INVENTORY, "backend_order": [M.contract.RANK],
                "rows": [{"mode": mode, "backend": M.contract.RANK, "binary": mode + ".elf",
                          "build_record": mode + ".json", "compiler_command": {"output": mode + ".o"}}
                         for mode in ("compatible", "native")]}
    with pytest.raises(ValueError):
        builder.check_inventory(selected)
    builder.check_inventory(selected, True)
    complete = {"backend_order": sorted(M.BACKENDS),
                "rows": [{"mode": mode, "backend": backend, "binary": mode + backend + ".elf",
                          "build_record": mode + backend + ".json",
                          "compiler_command": {"output": mode + backend + ".o"}}
                         for mode in ("compatible", "native") for backend in sorted(M.BACKENDS)]}
    builder.check_inventory(complete)
    complete["rows"].pop()
    with pytest.raises(ValueError):
        builder.check_inventory(complete)
    missing = copy.deepcopy(selected)
    missing["rows"].pop()
    with pytest.raises(ValueError):
        builder.check_inventory(missing, True)


def test_unchanged_checks_the_declared_frozen_source(tmp_path):
    source = tmp_path / "frozen-posets"
    source.mkdir()
    header = source / "fixture.hh"
    header.write_text("frozen source\n")
    metadata = {"modes": {}, "posets_source_dir": str(source),
                "source_sha256": {"fixture.hh": M.sha(header)},
                "driver_sha256": M.sha(Path(M.__file__)), "support_sha256": {}}
    M.unchanged(metadata)
    header.write_text("changed source\n")
    with pytest.raises(ValueError, match="A Posets source changed"):
        M.unchanged(metadata)


def capture(tmp_path):
    snapshot = tmp_path / "snapshots"
    directory = snapshot / "aut-1-0"
    directory.mkdir(parents=True)
    (directory / "meta.tsv").write_text(
        "schema_version\tstates\tbool_threshold\tinit_state\tworker\tinstance\n"
        "3\t2\t0\t0\tfixture\tfixture\n")
    (directory / "automaton.hoa").write_text("HOA fixture: provenance only\n")
    (directory / "cpre-1.skipped").write_text("actions=5000 cap=4096\n")
    (directory / "contains-1.tsv.partial").write_text("# incomplete")
    trace = directory / "contains-0.tsv"
    trace.write_text("# schema_version=1 k=2 loop=1 maxima=1 expected=1 bin=0 ordinal=1 hits=1 origin=picker\n"
                     "[query]\n0\t-1\n[generators]\n0\t0\n[end]\n")
    manifest = tmp_path / "capture.json"
    def file(name, content):
        path = tmp_path / name
        path.write_text(content)
        return {"path": str(path), "sha256": M.sha(path)}

    binary = file("binary", "trusted fixture; never executed\n")
    tlsf = file("input.tlsf", "fixture\n")
    proof = file("provenance.json", json.dumps({"binary_sha256": binary["sha256"], "source_revision": "a" * 40,
                                               "meson_options": [{"name": "acacia_enable_diagnostics", "value": True}]}))
    library = file("libfixture.so", "fixture library\n")
    implementation = {str(tmp_path / name): file(name, name)["sha256"]
                      for name in ("posets-capture.py", "posets-matrix.py", "benchlib.py", "scope_memory.py")}
    stdout, stderr = file("stdout", "UNKNOWN\n"), file("stderr", "")
    document = {"schema_version": 1, "kind": "posets-operation-capture", "instance": "fixture",
                "input": tlsf, "binary": binary, "frozen_provenance": proof,
                "implementation_sha256": implementation,
                "implementation_source_paths": {Path(path).name: path for path in implementation},
                "runtime_libraries": [{**library, "realpath": library["path"]}],
                "snapshot_directory": str(snapshot),
                "command": [binary["path"], "--arms", "real:small:backward", "--spot-fast", "off",
                            "--r-prepass", "off", "--equivariance", "off", "-T", tlsf["path"]],
                "environment": {"ACACIA_DIAG": "1", "ACACIA_DIAG_INSTANCE": "fixture", "ACACIA_DIAG_PROGRESS_EVERY": "0",
                                "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(snapshot), "ACACIA_ANTICHAIN_SNAPSHOT_CPRE": "1",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS": "1", "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS": "1",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX": "8", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS": "4096",
                                "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX": "16"},
                "result": {"returncode": 2, "seconds": 0, "timed_out": False, "resource_limited": False},
                "classification": "UNKNOWN",
                "treatment": {"timeout": 17, "memory_max": "8G", "memory_swap_max": "0", "serial": True},
                "logs_sha256": {"stdout_sha256": stdout["sha256"], "stderr_sha256": stderr["sha256"]}}
    manifest.write_text(json.dumps(document))
    refresh_manifest(manifest)
    plan = tmp_path / "plan.json"
    entries = [{"instance": "fixture", "family": "fixture", "split": "train",
                "path": str(trace), "capture_manifest": str(manifest)}]
    plan.write_text(json.dumps({"schema_version": 1, "traces": entries}))
    return plan, trace, entries


def refresh_manifest(manifest):
    document = json.loads(manifest.read_text())
    snapshot = Path(document["snapshot_directory"])
    document["snapshot_files_sha256"] = {str(path.relative_to(snapshot)): M.sha(path)
                                           for path in snapshot.rglob("*") if path.is_file()}
    manifest.write_text(json.dumps(document))


def test_all_boolean_is_not_zero_dimension_and_failed_samples_are_bound(tmp_path):
    plan, trace, _ = capture(tmp_path)
    rows, sources = M.load_traces(plan)
    assert rows[0]["features"] == {"dimension": 2, "numeric_prefix": 0, "boolean_tail": 2,
                                   "maxima": 1, "bottom_fraction": 0, "unique_numeric_prefixes": 1,
                                   "unique_boolean_suffixes": 1, "recorded_answer": 1}
    directory = sources["directories"][str(trace.parent)]
    assert directory["complete_operation_counts"] is None
    assert {Path(path).suffix for path in directory["files_sha256"]} >= {".partial", ".skipped"}


@pytest.mark.parametrize("mutation", ["duplicate", "family_split", "instance_split", "partial", "symlink"])
def test_invalid_trace_plan_rejected(tmp_path, mutation):
    plan, trace, entries = capture(tmp_path)
    if mutation == "duplicate":
        entries.append(dict(entries[0]))
    elif mutation in ("family_split", "instance_split"):
        other = trace.with_name("contains-2.tsv")
        other.write_bytes(trace.read_bytes())
        entries.append({**entries[0], "path": str(other), "split": "heldout"})
        if mutation == "instance_split":
            entries[-1]["family"] = "another-family"
    elif mutation == "partial":
        entries[0]["path"] += ".partial"
        Path(entries[0]["path"]).write_bytes(trace.read_bytes())
    else:
        copy = trace.with_suffix(".copy")
        trace.rename(copy)
        trace.symlink_to(copy)
    refresh_manifest(Path(entries[0]["capture_manifest"]))
    plan.write_text(json.dumps({"schema_version": 1, "traces": entries}))
    expected = {"duplicate": "Repeated trace", "family_split": "family crosses", "instance_split": "instance crosses",
                "partial": "finalized operation", "symlink": "nonregular"}[mutation]
    with pytest.raises(ValueError, match=expected):
        M.load_traces(plan)


@pytest.mark.parametrize("row", ["1\t4\t2\t2\t8\n", "1\t4\t2\t1\t8\n", "1\t4\t2\t2\t8"])
def test_operation_counts_require_complete_consistent_table(tmp_path, row):
    plan, trace, _ = capture(tmp_path)
    (trace.parent / "operation-counts.tsv").write_text("complete\tcontains\thits\tmisses\tcpre_calls\n" + row)
    refresh_manifest(tmp_path / "capture.json")
    if row == "1\t4\t2\t2\t8\n":
        _, source = M.load_traces(plan)
        assert source["directories"][str(trace.parent)]["complete_operation_counts"]["contains"] == 4
    else:
        with pytest.raises(ValueError):
            M.load_traces(plan)


@pytest.mark.parametrize("mutation", ["input", "binary", "runtime", "command", "bool_exit", "classification", "nan_seconds", "extra_snapshot", "snapshot_bytes"])
def test_capture_identity_must_bind_invocation_and_all_bytes(tmp_path, mutation):
    plan, trace, _ = capture(tmp_path)
    manifest = tmp_path / "capture.json"
    document = json.loads(manifest.read_text())
    if mutation in ("input", "binary"):
        Path(document[mutation]["path"]).write_text("changed bytes")
    elif mutation == "runtime":
        Path(document["runtime_libraries"][0]["path"]).write_text("changed library")
    elif mutation == "command":
        document["command"][2] = "other-arm"
        manifest.write_text(json.dumps(document))
    elif mutation == "bool_exit":
        document["result"]["returncode"] = False
        manifest.write_text(json.dumps(document))
    elif mutation in ("classification", "nan_seconds"):
        if mutation == "classification":
            document["classification"] = "TIMEOUT"
        else:
            document["result"]["seconds"] = float("nan")
        manifest.write_text(json.dumps(document))
    elif mutation == "extra_snapshot":
        (trace.parent / "new-file").write_text("unbound")
    else:
        trace.write_text("changed operation")
    with pytest.raises(ValueError):
        M.load_traces(plan)


def test_exact_output_identity_and_metrics():
    line = "[--] running tests for posets::downsets::vector_backed<posets::vectors::simd_vector_backed<test_value_type>>\n"
    assert M.output_identity(line, "vector_backed", "simd_vector_backed")
    assert not M.output_identity(line + line.replace("::vector_backed<", "::vector_backed_bin<"), "vector_backed", "simd_vector_backed")
    assert not M.output_identity(line, "vector_backed_bin", "simd_vector_backed")
    assert M.metrics("  - cpre: 0.001\n") == {"cpre": 0.001}
    for malformed in ("", "  - cpre: nan\n", "  - cpre: -1\n", "  - cpre: 1\n  - cpre: 2\n"):
        with pytest.raises(ValueError):
            M.metrics(malformed)


@pytest.mark.parametrize("mutation", ["wrong_binary", "missing_revision", "disabled", "integer_true", "wrong_option"])
def test_frozen_provenance_binds_actual_binary_and_diagnostic_option(tmp_path, mutation):
    plan, _, _ = capture(tmp_path)
    proof = tmp_path / "provenance.json"
    document = json.loads(proof.read_text())
    if mutation == "wrong_binary":
        document["binary_sha256"] = "b" * 64
    elif mutation == "missing_revision":
        document["source_revision"] = ""
    elif mutation in ("disabled", "integer_true"):
        document["meson_options"][0]["value"] = False if mutation == "disabled" else 1
    else:
        document["meson_options"][0]["name"] = "acacia_diagnostics"
    proof.write_text(json.dumps(document))
    manifest = tmp_path / "capture.json"
    declared = json.loads(manifest.read_text())
    declared["frozen_provenance"]["sha256"] = M.sha(proof)
    manifest.write_text(json.dumps(declared))
    with pytest.raises(ValueError, match="Frozen"):
        M.load_traces(plan)


def test_planned_order_is_saved_and_source_timeout_never_qualifies(tmp_path, monkeypatch):
    from argparse import Namespace
    from benchlib import RunResult

    monkeypatch.setattr(M, "campaign_scope_guard", lambda _: nullcontext())
    mode = {"binary": "/trusted/fixture", "backends": ["vector_backed", "vector_backed_bin"]}
    metadata = {"modes": {"compatible": mode, "native": mode},
                "aliases": {"compatible": "compatible", "native": "native"}}
    args = Namespace(case_timeout=1, check_timeout=4, source_timeout=7, memory_max="8G", backends=mode["backends"], measure=True,
                     repetitions=3, rounds=1, detail=False, seed=0)
    trace = {"id": 0, "path": "/trace", "features": {"dimension": 2, "maxima": 1, "boolean_tail": 2}}
    measured = []

    def fake_run(command, cap, *_args, **_kwargs):
        if "--check=trace-source" in command:
            assert cap == 7
            return RunResult("[TRACE SOURCE OK]\n", "", 0, 0, False)
        backend, vector = command[-2:]
        output = f"[--] running tests for posets::downsets::{backend}<posets::vectors::{vector}>\n"
        if "--check=replay" not in command:
            assert cap == 1
            assert (tmp_path / "measurement-order.json").is_file()
            measured.append(command)
            output += "  - rounds: 1\n  - dimension: 2\n  - boolean_tail: 2\n  - before_maxima: 1\n  - cpre: 0.001\n"
        else:
            assert cap == 4
        return RunResult(output, "", 0, 0, False)

    monkeypatch.setattr(M, "run_systemd_scope", fake_run)
    rows = M.replay(args, metadata, [trace], tmp_path)
    assert len([row for row in rows if row["phase"] == "check"]) == 6  # scalar once, SIMD twice per backend
    assert len(measured) == 12  # SIMD treatments only, three repetitions
    plan = json.loads((tmp_path / "measurement-order.json").read_text())
    assert {row["vector"] for row in plan} == {"simd_vector_backed"}
    assert [(row["mode"], row["backend"]) for row in plan[:4]] != [(row["mode"], row["backend"]) for row in plan[4:8]]
    assert all(row["logs_sha256"] for row in rows if "command" in row)
    monkeypatch.setattr(M, "run_systemd_scope", lambda *_args, **_kwargs: RunResult("", "", 124, 1, True))
    measured.clear()
    rows = M.replay(args, metadata, [trace], tmp_path)
    assert rows[0]["status"] == "timeout"
    assert all(row["status"] == "source_unverified" for row in rows[1:])
    assert not measured and not json.loads((tmp_path / "measurement-order.json").read_text())


def test_common_timing_order_survives_different_qualification(tmp_path, monkeypatch):
    from argparse import Namespace
    from benchlib import RunResult

    monkeypatch.setattr(M, "campaign_scope_guard", lambda _: nullcontext())
    backends = ["kdtree_backed", "sharingtree_backed", "vector_backed"]
    mode = {"binary": "/fixture", "backends": backends}
    metadata = {"modes": {"compatible": mode, "native": mode},
                "aliases": {"compatible": "compatible", "native": "native"}}
    args = Namespace(case_timeout=1, check_timeout=1, source_timeout=1, memory_max="8G",
                     backends=backends, measure=True, repetitions=3, rounds=1, detail=False, seed=17)
    traces = [{"id": number, "path": f"/trace-{number}",
               "features": {"dimension": 2, "maxima": 1, "boolean_tail": 2}} for number in range(2)]

    def run(directory, rejected):
        directory.mkdir()

        def fake(command, *_args, **_kwargs):
            if "--check=trace-source" in command:
                return RunResult("[TRACE SOURCE OK]\n", "", 0, 0, False)
            trace = int(command[command.index("--trace") + 1].rsplit("-", 1)[1])
            backend, vector = command[-2:]
            if "--check=replay" in command and (trace, backend) in rejected:
                return RunResult("", "retained timeout\n", 124, 1, True)
            output = f"[--] running tests for posets::downsets::{backend}<posets::vectors::{vector}>\n"
            if "--check=replay" not in command:
                output += "  - rounds: 1\n  - dimension: 2\n  - boolean_tail: 2\n  - before_maxima: 1\n  - cpre: 0.001\n"
            return RunResult(output, "", 0, 0, False)

        monkeypatch.setattr(M, "run_systemd_scope", fake)
        rows = M.replay(args, metadata, traces, directory)
        order = json.loads((directory / "measurement-order.json").read_text())
        return rows, [(row["repetition"], row["trace"], row["mode"], row["backend"], row["vector"]) for row in order]

    rejected = {(0, "kdtree_backed"), (1, "sharingtree_backed")}
    rows, filtered = run(tmp_path / "censored", rejected)
    _, complete = run(tmp_path / "complete", set())
    assert filtered == [row for row in complete if (row[1], row[3]) not in rejected]
    assert any(row["phase"] == "check" and row["status"] == "timeout" for row in rows)


@pytest.mark.parametrize("mutation", ["none", "ndebug", "stdlib", "optimization", "lto", "target"])
def test_measurements_require_comparable_release_builds(mutation):
    flags = ["c++", "-O3", "-flto=auto", "-march=native", "-std=c++20", "-DNDEBUG"]
    modes = {name: {"ndebug": True, "stdlib_assertions": False, "compiler": "fixture",
                    "compiler_command": {"arguments": list(flags)}} for name in ("compatible", "native", "max")}
    native = modes["native"]
    if mutation == "ndebug":
        native["ndebug"] = False
    elif mutation == "stdlib":
        native["stdlib_assertions"] = True
    elif mutation == "optimization":
        native["compiler_command"]["arguments"].append("-O2")
    elif mutation == "lto":
        native["compiler_command"]["arguments"].remove("-flto=auto")
    elif mutation == "target":
        native["compiler_command"]["arguments"].append("-march=x86-64")
    if mutation == "none":
        M.measurement_build({"modes": modes})
    else:
        with pytest.raises(ValueError):
            M.measurement_build({"modes": modes})


def binary_map_fixture(tmp_path, monkeypatch):
    """Real source pins, fake ELF/Ninja observations; no compiler or harness runs."""
    source, anchor_source = tmp_path / "source", tmp_path / "anchor"
    (source / "tests").mkdir(parents=True)
    (source / ".gitignore").write_text("build/\n")
    marker = "  register_maker<false> ((vector_types*) nullptr, (set_types*) nullptr);\n"
    original = "using set_types = template_type_list<\n  fixture\n  >;\nint main () {\n" + marker + "}\n"
    (source / "tests/downset-bm.cc").write_text(original)
    (source / "tests/meson.build").write_text("# Keep the 14-backend matrix\n")

    def git(*args):
        return subprocess.check_output(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", *args], cwd=source)

    git("init", "-q")
    git("add", ".")
    git("commit", "-qm", "base")
    base = git("rev-parse", "HEAD").decode().strip()
    shutil.copytree(source, anchor_source)
    replacement = ("#if defined(POSETS_MATRIX_ONLY) && defined(POSETS_MATRIX_BACKEND)\n"
                   "  register_maker<false, POSETS_MATRIX_BACKEND> ((vector_types*) nullptr);\n#else\n" + marker + "#endif\n")
    selected = original.replace(marker, replacement)
    (source / "tests/downset-bm.cc").write_text(selected)
    git("add", ".")
    git("commit", "-qm", "selector")
    revision = git("rev-parse", "HEAD").decode().strip()
    overlay = tmp_path / "selector.patch"
    overlay.write_bytes(git("diff", "--binary", "--full-index", base, revision))
    library = tmp_path / "libfixture.so"
    library.write_text("library\n")

    def libs(_binary):
        return [{"path": str(library), "realpath": str(library), "sha256": M.sha(library)}]

    def binding(path):
        return {"path": str(path), "sha256": M.sha(path)}

    metadata, ninja = {}, {}

    def compile_command(directory, mode):
        obj = f"tests/downset-matrix-{mode}.p/downset-bm.cc.o"
        args = ["c++", "-Itests/downset-matrix-" + mode + ".p", "-I../include", "-O3", "-flto=1", "-march=native",
                "-std=c++20", "-DNDEBUG", "-DPOSETS_MATRIX_ONLY", "-DPOSETS_SIMD_WIDTH=" + str(M.MODES.index(mode)),
                "-MD", "-MQ", obj, "-MF", obj + ".d", "-o", obj, "-c", "../tests/downset-bm.cc"]
        return {"arguments": args, "directory": str(directory), "file": "../tests/downset-bm.cc", "output": obj}

    def link_command(directory, mode):
        return {"arguments": ["c++", "-o", "tests/downset-matrix-" + mode,
                               f"tests/downset-matrix-{mode}.p/downset-bm.cc.o", "-O3", "-flto=1", "-march=native"],
                "directory": str(directory), "output": "tests/downset-matrix-" + mode}

    def meta(mode, backends):
        lanes = 16 if mode == "compatible" else 64
        return {"compiler": "fixture", "element_bytes": 1, "selected_lanes": lanes, "compatible_lanes": 16,
                "native_lanes": 64, "max_fixed_lanes": 64, "block_bytes": lanes, "width_selector": M.MODES.index(mode),
                "alignment": lanes, "ndebug": True, "stdlib_assertions": False, "simd_type": str(lanes),
                "hybrid_policy": "fixture", "backends": backends}

    def instructions(mode):
        return "90 c3" if mode == "compatible" else "91 c3"

    def elf(path, mode, backends):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\x7fELF" + str(path).encode())
        metadata[str(path)] = meta(mode, backends)
        return {**metadata[str(path)], "binary": str(path), "binary_sha256": M.sha(path), "libraries": libs(path),
                "comparison_instructions_sha256": hashlib.sha256(bytes.fromhex(instructions(mode))).hexdigest()}

    for tree in (source, anchor_source):
        build = tree / "build"
        build.mkdir()
        (build / "build.ninja").write_text("fixture manifest\n")
        commands = [compile_command(build, mode) for mode in M.MODES]
        (build / "compile_commands.json").write_text(json.dumps(commands))
        for mode in M.MODES:
            ninja[(str(build), mode)] = "\n".join(shlex.join(command["arguments"])
                                                  for command in (compile_command(build, mode), link_command(build, mode))) + "\n"
    anchor = {"posets_revision": base, "posets_dirty": "", "posets_source_dir": str(anchor_source),
              "source_sha256": {name: M.sha(anchor_source / name) for name in ("tests/downset-bm.cc", "tests/meson.build")},
              "modes": {mode: {**elf(anchor_source / "build/tests" / ("downset-matrix-" + mode), mode, sorted(M.BACKENDS)),
                                "compiler_command": compile_command(anchor_source / "build", mode)} for mode in M.MODES}}
    proof = tmp_path / "anchor.json"
    proof.write_text(json.dumps({"provenance": anchor}))
    plan = {"schema_version": 1, "kind": "posets-per-backend-binaries", "base_revision": base,
            "source_revision": revision, "source_dir": str(source), "selector_overlay": binding(overlay),
            "anchor_provenance": binding(proof),
            "source_oracle_binary": binding(anchor_source / "build/tests/downset-matrix-compatible"),
            "aliases": {"max": "native"}, "binaries": []}
    builder = tmp_path / "builder.py"
    builder.write_text("fixture builder\n")
    build_plan_path = tmp_path / "build-plan.json"
    build_rows = []
    for mode in ("compatible", "native"):
        for backend in sorted(M.BACKENDS):
            binary = source / "build/per-backend" / (mode + "-" + backend)
            observed = elf(binary, mode, [backend])
            template = compile_command(source / "build", mode)
            command = json.loads(json.dumps(template))
            obj = str(binary) + ".o"
            for flag, value in (("-MQ", obj), ("-MF", obj + ".d"), ("-o", obj)):
                command["arguments"][command["arguments"].index(flag) + 1] = value
            command["arguments"].append("-DPOSETS_MATRIX_BACKEND=" + M.BACKEND_SELECTORS[backend])
            command["output"] = obj
            template_linker = link_command(source / "build", mode)
            linker = json.loads(json.dumps(template_linker))
            linker["arguments"][2] = str(binary)
            linker["arguments"][3] = obj
            linker["output"] = str(binary)
            record = {"schema_version": 1, "kind": "posets-selector-build", "mode": mode, "backend": backend,
                      "base_revision": base, "source_revision": revision, "source_dir": str(source),
                      "selector_overlay_sha256": M.sha(overlay), "binary_sha256": observed["binary_sha256"],
                      "compile_commands": binding(source / "build/compile_commands.json"),
                      "template_compiler_command": template, "compiler_command": command,
                      "template_linker_command": template_linker, "linker_command": linker}
            stages, invocations = [], []
            for stage, argv in (("compile", command), ("link", linker)):
                stem = tmp_path / (mode + "-" + backend + "-" + stage)
                spec = {"command": argv["arguments"], "cwd": argv["directory"], "timeout": 600}
                spec_path = stem.with_suffix(".spec.json")
                spec_path.write_text(json.dumps(spec))
                invocation_path = stem.with_suffix(".json")
                log = stem.with_suffix(".log")
                log.write_text("fixture successful build\n")
                helper = invocation_path.with_suffix(".helper.py")
                helper.write_text("fixture scope helper\n")
                invocation_path.write_text(json.dumps({"spec": spec, "spec_sha256": M.sha(spec_path),
                    "result": {"returncode": 0, "timed_out": False, "resource_limited": False},
                    "memory_max": "8G", "memory_swap_max": "0", "log": str(log), "log_sha256": M.sha(log),
                    "helper_sha256": M.sha(helper), "benchlib_sha256": M.sha(M.ROOT / "benchmarking/benchlib.py")}))
                stages.append({"stage": stage, "spec": binding(spec_path)})
                invocations.append({"stage": stage, **binding(invocation_path)})
            record["invocations"] = invocations
            record["builder"] = binding(builder)
            record_path = tmp_path / (mode + "-" + backend + ".json")
            record_path.write_text(json.dumps(record))
            plan["binaries"].append({"mode": mode, "backend": backend, "binary": str(binary),
                                     "binary_sha256": M.sha(binary), "build_record": binding(record_path)})
            build_rows.append({key: record[key] for key in ("mode", "backend", "compiler_command", "linker_command",
                                                           "template_compiler_command", "template_linker_command")} | {"stages": stages})
    build_plan_path.write_text(json.dumps({"schema_version": 1, "kind": "posets-selector-build-plan", "rows": build_rows,
                                          "base_revision": base, "source_revision": revision, "source_dir": str(source)}))
    for entry in plan["binaries"]:
        record_path = Path(entry["build_record"]["path"])
        record = json.loads(record_path.read_text())
        record["build_plan"] = binding(build_plan_path)
        record_path.write_text(json.dumps(record))
        entry["build_record"]["sha256"] = M.sha(record_path)
    path = tmp_path / "map.json"
    path.write_text(json.dumps(plan))
    real_check_output = subprocess.check_output

    def fake_check_output(command, **kwargs):
        if command[0] == "ninja":
            return ninja[(command[2], command[-1].removeprefix("tests/downset-matrix-"))]
        if command[0] == "objdump":
            mode = "compatible" if metadata[command[-1]]["width_selector"] == 0 else "native"
            return "0 <posets_matrix_compare>:\n  0: " + instructions(mode) + "  ret\n"
        if len(command) == 2 and command[1] == "--metadata":
            return json.dumps(metadata[command[0]])
        return real_check_output(command, **kwargs)

    monkeypatch.setattr(M.subprocess, "check_output", fake_check_output)
    monkeypatch.setattr(M, "libraries", libs)
    output = tmp_path / "output"
    output.mkdir()
    return path, plan, output, metadata, library, ninja


def load_binary_map(path, plan, output):
    return M.backend_provenance(Path(plan["source_oracle_binary"]["path"]).parent.parent, output, None, path)[1]


def resync_build_record(plan, entry, record):
    """Rebind all internal artifacts so negative flag tests reach the anchor guard."""
    build_plan_path = Path(record["build_plan"]["path"])
    build_plan = json.loads(build_plan_path.read_text())
    row, = [row for row in build_plan["rows"] if (row["mode"], row["backend"]) == (record["mode"], record["backend"])]
    for name in ("compiler_command", "linker_command", "template_compiler_command", "template_linker_command"):
        row[name] = record[name]
    for stage, invocation, name in zip(row["stages"], record["invocations"], ("compiler_command", "linker_command")):
        spec_path = Path(stage["spec"]["path"])
        spec = json.loads(spec_path.read_text())
        spec["command"] = record[name]["arguments"]
        spec_path.write_text(json.dumps(spec))
        stage["spec"]["sha256"] = M.sha(spec_path)
        invocation_path = Path(invocation["path"])
        observed = json.loads(invocation_path.read_text())
        observed["spec"], observed["spec_sha256"] = spec, M.sha(spec_path)
        invocation_path.write_text(json.dumps(observed))
        invocation["sha256"] = M.sha(invocation_path)
    build_plan_path.write_text(json.dumps(build_plan))
    for other_entry in plan["binaries"]:
        path = Path(other_entry["build_record"]["path"])
        other = record if other_entry is entry else json.loads(path.read_text())
        other["build_plan"]["sha256"] = M.sha(build_plan_path)
        other["compile_commands"]["sha256"] = M.sha(Path(other["compile_commands"]["path"]))
        path.write_text(json.dumps(other))
        other_entry["build_record"]["sha256"] = M.sha(path)


def test_per_backend_map_binds_complete_inventory_and_alias(tmp_path, monkeypatch):
    path, plan, output, _, _, _ = binary_map_fixture(tmp_path, monkeypatch)
    metadata = load_binary_map(path, plan, output)
    assert set(metadata["backend_binaries"]) == {"compatible", "native"}
    assert all(set(rows) == M.BACKENDS for rows in metadata["backend_binaries"].values())
    assert metadata["aliases"]["max"] == "native"
    assert M.binary_entry(metadata, "max", "kdtree_backed") is M.binary_entry(metadata, "native", "kdtree_backed")
    assert metadata["source_oracle"]["binary"] == plan["source_oracle_binary"]["path"]
    assert "binary" not in metadata["modes"]["max"]
    M.measurement_build(metadata)


def test_selector_source_rejects_the_legacy_singleton_alias_location(tmp_path, monkeypatch):
    _, plan, _, _, _, _ = binary_map_fixture(tmp_path, monkeypatch)
    source = Path(plan["source_dir"])
    original = subprocess.check_output(["git", "show", plan["base_revision"] + ":tests/downset-bm.cc"], cwd=source, text=True)
    start = original.index("using set_types = template_type_list<\n")
    end = original.index("  >;\n", start) + len("  >;\n")
    rejected = (original[:start] + "#if defined(POSETS_MATRIX_ONLY) && defined(POSETS_MATRIX_BACKEND)\n"
                "using set_types = template_type_list<POSETS_MATRIX_BACKEND>;\n#else\n" + original[start:end] + "#endif\n" + original[end:])
    (source / "tests/downset-bm.cc").write_text(rejected)
    subprocess.run(["git", "add", "tests/downset-bm.cc"], cwd=source, check=True)
    subprocess.run(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "rejected alias selector"], cwd=source, check=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    with pytest.raises(ValueError, match="Selector harness overlay differs"):
        M.selector_source(source, plan["base_revision"], revision, Path(plan["selector_overlay"]["path"]))


@pytest.mark.parametrize("mutation", ["duplicate", "missing", "singleton", "source_pin", "base_pin", "binary", "library", "oracle", "alias", "alias_probe", "abi", "selector", "optimization", "template_define", "link_flags", "invocation", "scope_budget"])
def test_per_backend_map_rejects_integrity_failures(tmp_path, monkeypatch, mutation):
    path, plan, output, observed, library, _ = binary_map_fixture(tmp_path, monkeypatch)
    entry = plan["binaries"][0]
    record_path = Path(entry["build_record"]["path"])
    record = json.loads(record_path.read_text())
    if mutation == "duplicate":
        plan["binaries"].append(dict(entry))
    elif mutation == "missing":
        plan["binaries"].pop()
    elif mutation == "singleton":
        observed[entry["binary"]]["backends"] = ["vector_backed", "kdtree_backed"]
    elif mutation == "source_pin":
        plan["source_revision"] = plan["base_revision"]
    elif mutation == "base_pin":
        plan["base_revision"] = plan["source_revision"]
    elif mutation == "binary":
        Path(entry["binary"]).write_bytes(b"\x7fELFtampered")
    elif mutation == "library":
        library.write_text("changed library")
    elif mutation == "oracle":
        plan["source_oracle_binary"] = {"path": entry["binary"], "sha256": entry["binary_sha256"]}
    elif mutation == "alias":
        plan["aliases"]["max"] = "compatible"
    elif mutation == "alias_probe":
        proof = Path(plan["anchor_provenance"]["path"])
        document = json.loads(proof.read_text())
        maximum = document["provenance"]["modes"]["max"]
        maximum["alignment"] *= 2
        observed[maximum["binary"]]["alignment"] = maximum["alignment"]
        proof.write_text(json.dumps(document))
        plan["anchor_provenance"]["sha256"] = M.sha(proof)
    elif mutation == "abi":
        observed[entry["binary"]]["alignment"] *= 2
    elif mutation == "selector":
        record["compiler_command"]["arguments"][-1] = "-DPOSETS_MATRIX_BACKEND=wrong"
    elif mutation == "optimization":
        record["compiler_command"]["arguments"].append("-O2")
    elif mutation == "template_define":
        record["compiler_command"]["arguments"].append("-D__AVX512F__=1")
        record["template_compiler_command"]["arguments"].append("-D__AVX512F__=1")
        db_path = Path(record["compile_commands"]["path"])
        commands = json.loads(db_path.read_text())
        commands[0] = record["template_compiler_command"]
        db_path.write_text(json.dumps(commands))
        # Update every internal reference; the frozen anchor must still reject it.
        for row in plan["binaries"]:
            p = Path(row["build_record"]["path"])
            other = record if p == record_path else json.loads(p.read_text())
            other["compile_commands"]["sha256"] = M.sha(db_path)
            p.write_text(json.dumps(other))
            row["build_record"]["sha256"] = M.sha(p)
    elif mutation == "link_flags":
        record["linker_command"]["arguments"].append("-Ofast")
    else:
        invocation = record["invocations"][0]
        p = Path(invocation["path"])
        observed_scope = json.loads(p.read_text())
        if mutation == "invocation":
            observed_scope["spec"]["command"].append("-O2")
        else:
            observed_scope["memory_max"] = "9G"
        p.write_text(json.dumps(observed_scope))
        invocation["sha256"] = M.sha(p)
    if mutation in {"selector", "optimization", "template_define", "link_flags"}:
        resync_build_record(plan, entry, record)
    elif mutation in {"invocation", "scope_budget"}:
        record_path.write_text(json.dumps(record))
        entry["build_record"]["sha256"] = M.sha(record_path)
    path.write_text(json.dumps(plan))
    with pytest.raises(ValueError):
        load_binary_map(path, plan, output)


def test_per_backend_dispatch_keeps_source_oracle_scalar_scope_and_shuffle(tmp_path, monkeypatch):
    from argparse import Namespace
    from benchlib import RunResult

    path, plan, output, _, _, _ = binary_map_fixture(tmp_path, monkeypatch)
    mapped = load_binary_map(path, plan, output)
    combined = {"modes": {mode: {"binary": "/combined/" + mode, "backends": sorted(M.BACKENDS)} for mode in M.MODES},
                "aliases": mapped["aliases"]}
    args = Namespace(case_timeout=1, check_timeout=2, source_timeout=3, memory_max="8G",
                     backends=sorted(M.BACKENDS), measure=True, repetitions=2, rounds=1, detail=False, seed=13)
    trace = {"id": 0, "path": "/trace", "features": {"dimension": 2, "maxima": 1, "boolean_tail": 2}}
    monkeypatch.setattr(M, "campaign_scope_guard", lambda _: nullcontext())
    reference = M
    if name := os.environ.get("ACACIA_POSETS_PRE_MAP_DRIVER"):
        snapshot = Path(name)
        assert M.sha(snapshot) == M.SCHEDULER_REFERENCE_SHA256
        spec = importlib.util.spec_from_file_location("posets_matrix_pre_map", snapshot)
        reference = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reference)
        monkeypatch.setattr(reference, "campaign_scope_guard", lambda _: nullcontext())

    def run(metadata, name, mismatch=False, module=M, censored=False):
        directory = tmp_path / name
        directory.mkdir()
        seen = []

        def fake(command, cap, *_args, **_kwargs):
            seen.append(command)
            if "--check=trace-source" in command:
                assert cap == 3
                expected = metadata.get("source_oracle", metadata["modes"]["compatible"])["binary"]
                assert command[0] == expected
                return RunResult("[TRACE SOURCE OK]\n", "", 0, 0, False)
            backend, vector = command[-2:]
            assert command[0] in {M.binary_entry(metadata, mode, backend)["binary"] for mode in ("compatible", "native")}
            if censored and backend == "kdtree_backed" and "--check=replay" in command:
                return RunResult("", "retained timeout\n", 124, 1, True)
            emitted = "wrong_backend" if mismatch else backend
            text = f"[--] running tests for posets::downsets::{emitted}<posets::vectors::{vector}>\n"
            if "--check=replay" not in command:
                text += "  - rounds: 1\n  - dimension: 2\n  - boolean_tail: 2\n  - before_maxima: 1\n  - cpre: 0.001\n"
            return RunResult(text, "", 0, 0, False)

        monkeypatch.setattr(M, "run_systemd_scope", fake)
        monkeypatch.setattr(module, "run_systemd_scope", fake)
        rows = module.replay(args, metadata, [trace], directory)
        scalar = [row for row in rows if row.get("vector") == "vector_backed"]
        assert len(scalar) == 24 and {row["mode"] for row in scalar} == {"compatible"}
        measurements = 92 if censored else 96
        assert len([row for row in rows if row["phase"] == "measure"]) == measurements
        assert len(seen) == 1 + 72 + measurements
        return json.loads((directory / "measurement-order.json").read_text())

    assert run(mapped, "mapped") == run(combined, "combined", module=reference)
    assert run(mapped, "mapped-censored", censored=True) == run(combined, "combined-censored", module=reference, censored=True)
    with pytest.raises(ValueError, match="exactly the requested backend"):
        run(mapped, "wrong-dispatch", mismatch=True)


def test_per_backend_synthetic_dispatch_uses_selected_elf(tmp_path, monkeypatch):
    path, plan, output, _, _, _ = binary_map_fixture(tmp_path, monkeypatch)
    metadata = load_binary_map(path, plan, output)
    commands = []

    def fake(command, **_kwargs):
        if command[:3] == ["git", "check-ignore", "-q"]:
            return subprocess.CompletedProcess(command, 0)
        backend, vector = command[-2:]
        assert command[0] in {M.binary_entry(metadata, mode, backend)["binary"] for mode in ("compatible", "native")}
        commands.append(command)
        text = f"[--] running tests for posets::downsets::{backend}<posets::vectors::{vector}>\n"
        return subprocess.CompletedProcess(command, 0, text, "")

    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(M.subprocess, "run", fake)
    monkeypatch.setattr(M, "no_core", lambda: None)
    monkeypatch.setattr(M, "provenance", lambda *_args: metadata)
    monkeypatch.setattr(M, "unchanged", lambda *_args: None)
    monkeypatch.setattr(sys, "argv", ["posets-matrix.py", "--build-dir", str(Path(plan["source_oracle_binary"]["path"]).parent.parent),
                                     "--binary-map", str(path), "--output", str(tmp_path / "screen")])
    M.main()
    assert len(commands) == 24 * 2 * 2 * len(M.CASES)
    assert json.loads((tmp_path / "screen/report.json").read_text())["scope"].startswith("Supplemental per-backend")


@pytest.mark.parametrize("changed", ["binary", "map", "record", "library"])
def test_per_backend_inputs_are_rechecked_after_replay(tmp_path, monkeypatch, changed):
    path, plan, output, _, library, _ = binary_map_fixture(tmp_path, monkeypatch)
    metadata = load_binary_map(path, plan, output)
    metadata.update({"source_sha256": {}, "driver_sha256": M.sha(Path(M.__file__)), "support_sha256": {}})
    M.unchanged(metadata)
    targets = {"binary": Path(plan["binaries"][0]["binary"]), "map": path,
               "record": Path(plan["binaries"][0]["build_record"]["path"]), "library": library}
    targets[changed].write_bytes(b"changed after replay")
    with pytest.raises(ValueError):
        M.unchanged(metadata)
