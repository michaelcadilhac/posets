#!/usr/bin/env python3
"""Screen/replay the existing Posets harness; preserve every rejected cell."""

import argparse
from collections import Counter
import csv
from dataclasses import asdict
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import random
import shlex
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rank_contract as contract
ROOT = contract.REPO
sys.path.insert(0, str(ROOT / 'benchmarking'))
from benchlib import RunResult, campaign_scope_guard, classify_run, run_systemd_scope
SCHEDULER_REFERENCE_SHA256 = "b7583da68de894784074fd83fc001875a52839dd778e99ce4a94e2fb07d4e3cf"
MODES = ("compatible", "native", "max")
CASES = ("vectors", "nonempty", "empty", "zero")
BACKENDS = {"bboxtree_backed", "comptrie_backed", "kdtree_backed", "vector_or_kdtree_backed",
            "vector_backed", "rank_bucketed_vector_backed", "filtered_vector_backed", "vector_backed_bin",
            "vector_backed_one_dim_split_intersection_only", "sharingtree_backed", "simple_sharingtree_backed",
            "sharingtrie_backed", "skiplist_backed", "cst_backed",
            "block_sharingtrie_backed", "block_sharingtrie_b1_scalar", "block_sharingtrie_b4_scalar",
            "block_sharingtrie_b4_simd", "block_sharingtrie_b8_scalar", "block_sharingtrie_b16_scalar",
            "block_sharingtrie_b16_simd", "block_sharingtrie_b8_simd_nocache",
            "lazy_kdtree_backed", "lazy_kdtree_uncascaded"}
BACKEND_SELECTORS = {name: "posets::downsets::" + name for name in BACKENDS} | {
    "block_sharingtrie_backed": "block_b8_simd",
    "block_sharingtrie_b1_scalar": "block_b1_scalar",
    "block_sharingtrie_b4_scalar": "block_b4_scalar",
    "block_sharingtrie_b4_simd": "block_b4_simd",
    "block_sharingtrie_b8_scalar": "block_b8_scalar",
    "block_sharingtrie_b16_scalar": "block_b16_scalar",
    "block_sharingtrie_b16_simd": "block_b16_simd",
    "block_sharingtrie_b8_simd_nocache": "block_b8_nocache",
    "lazy_kdtree_backed": "lazy_kd_cascaded",
    "lazy_kdtree_uncascaded": "lazy_kd_uncascaded"}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def regular(path):
    require(path.is_file() and not path.is_symlink(), f"Missing/nonregular input: {path}")
    return path


def libraries(binary):
    result = subprocess.run(["ldd", str(binary)], cwd=ROOT, capture_output=True, text=True, check=True,
                            env={**os.environ, "LC_ALL": "C"})
    require(not result.stderr.strip(), f"ldd: {result.stderr}")
    rows = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or re.fullmatch(r"linux-vdso\.so\.\d+ \(0x[0-9a-fA-F]+\)", line):
            continue
        require("not found" not in line, f"Missing dynamic dependency: {line}")
        match = re.fullmatch(r"(?:\S+ => )?(.+) \(0x[0-9a-fA-F]+\)", line)
        require(match is not None and "/" in match[1], f"Unrecognized dynamic dependency: {line}")
        path = Path(match[1])
        if not path.is_absolute():
            path = ROOT / path
        rows.append({"path": str(path), "realpath": str(path.resolve(strict=True)), "sha256": sha(path)})
    require(rows, "No dynamic dependencies resolved")
    return sorted(rows, key=lambda row: row["path"])


def frozen_identity(path, binary_sha256):
    document = json.loads(regular(path).read_text())
    require(document["binary_sha256"] == binary_sha256 and
            isinstance(document["source_revision"], str) and re.fullmatch(r"[0-9a-f]{40}", document["source_revision"]),
            "Frozen provenance does not bind this binary/source revision")
    options = document["meson_options"]
    require(len({row["name"] for row in options}) == len(options), "Duplicate frozen build option")
    require({row["name"]: row["value"] for row in options}.get("acacia_enable_diagnostics") is True,
            "Frozen binary provenance does not enable diagnostics")


def capture_identity(path, instance):
    """Validate the serial capture helper's immutable invocation/file binding."""
    document = json.loads(regular(path).read_text())
    require(type(document.get("schema_version")) is int and document["schema_version"] == 1 and
            document.get("kind") == "posets-operation-capture" and document.get("instance") == instance,
            "Capture schema/instance identity mismatch")
    sources = {str(path): sha(path)}
    for name in ("input", "binary", "frozen_provenance"):
        entry = document[name]
        source = regular(Path(entry["path"]))
        require(source.is_absolute() and sha(source) == entry["sha256"], f"Capture {name} bytes changed")
        sources[str(source)] = entry["sha256"]
    frozen_identity(Path(document["frozen_provenance"]["path"]), document["binary"]["sha256"])
    for name, expected in document["implementation_sha256"].items():
        require(Path(name).is_absolute() and sha(regular(Path(name))) == expected, "Capture implementation changed")
        sources[name] = expected
    require({Path(name).name for name in document["implementation_sha256"]} ==
            {"posets-capture.py", "posets-matrix.py", "benchlib.py", "scope_memory.py"}, "Incomplete capture implementation identity")
    require(set(document["implementation_source_paths"]) == {Path(name).name for name in document["implementation_sha256"]} and
            all(Path(source).is_absolute() and Path(source).name == name
                for name, source in document["implementation_source_paths"].items()), "Invalid historical helper source paths")
    for library in document["runtime_libraries"]:
        file = Path(library["path"])
        require(str(file.resolve(strict=True)) == library["realpath"] and sha(file) == library["sha256"],
                "Capture runtime dependency changed")
        sources[library["realpath"]] = library["sha256"]
    require(document["runtime_libraries"], "Missing capture runtime dependency identities")
    snapshot = Path(document["snapshot_directory"])
    require(snapshot.is_absolute() and snapshot.is_dir() and not snapshot.is_symlink(), "Invalid snapshot directory")
    actual = {}
    for file in snapshot.rglob("*"):
        require(not file.is_symlink() and (file.is_file() or file.is_dir()), "Nonregular snapshot evidence")
        if file.is_file():
            actual[str(file.relative_to(snapshot))] = sha(file)
            sources[str(file)] = sha(file)
    require(actual == document["snapshot_files_sha256"], "Capture snapshot inventory/hash mismatch")
    binary, tlsf = document["binary"]["path"], document["input"]["path"]
    require(document["command"] == [binary, "--arms", "real:small:backward", "--spot-fast", "off",
                                    "--r-prepass", "off", "--equivariance", "off", "-T", tlsf],
            "Capture command does not bind the isolated classic-backward input/binary")
    env = document["environment"]
    require(all(isinstance(name, str) and isinstance(value, str) for name, value in env.items()), "Invalid capture environment")
    required = {"ACACIA_DIAG": "1", "ACACIA_DIAG_INSTANCE": instance, "ACACIA_DIAG_PROGRESS_EVERY": "0",
                "ACACIA_ANTICHAIN_SNAPSHOT_DIR": str(snapshot), "ACACIA_ANTICHAIN_SNAPSHOT_CPRE": "1",
                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_FRONTIERS": "1", "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS": "1",
                "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX": "8", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS": "4096",
                "ACACIA_ANTICHAIN_SNAPSHOT_CONTAINS_MAX": "16"}
    require(all(env.get(name) == value for name, value in required.items()), "Capture instrumentation mismatch")
    result = document["result"]
    require(type(result["returncode"]) is int and type(result["timed_out"]) is bool and
            type(result["resource_limited"]) is bool, "Missing raw capture termination subtype")
    for name in ("seconds", "cpu_seconds"):
        value = result.get(name)
        require((name == "cpu_seconds" and value is None) or
                (type(value) in (int, float) and math.isfinite(value) and value >= 0), "Invalid capture duration")
    for name in ("stdout_bytes", "stderr_bytes", "memory_peak_bytes", "max_process_rss_bytes"):
        value = result.get(name)
        require(value is None or (type(value) is int and value >= 0), "Invalid capture byte counter")
    require(type(document["treatment"]["timeout"]) in (int, float) and type(document["treatment"]["serial"]) is bool and
            document["treatment"] == {"timeout": 17, "memory_max": "8G", "memory_swap_max": "0", "serial": True},
            "Unexpected capture budget")
    for name in ("stdout", "stderr"):
        file = regular(path.parent / name)
        require(sha(file) == document["logs_sha256"][name + "_sha256"], "Capture log changed")
        sources[str(file)] = sha(file)
    require(not (set(result) - (RunResult.__dataclass_fields__.keys() - {"stdout", "stderr"})), "Unknown capture process field")
    observed = RunResult((path.parent / "stdout").read_text(), (path.parent / "stderr").read_text(), **result)
    require(document["classification"] == classify_run(observed), "Capture classification disagrees with raw observation")
    return document, sources


def load_traces(plan):
    """Hash the declared, fixed split and all capture evidence, including failures."""
    document = json.loads(regular(plan).read_text())
    require(type(document.get("schema_version")) is int and document["schema_version"] == 1,
            "Unsupported trace plan schema")
    rows, sources, directories, groups, instances, inputs, owners, seen, captures = [], {str(plan): sha(plan)}, {}, {}, {}, {}, {}, set(), {}
    for number, entry in enumerate(document["traces"]):
        require(set(entry) == {"instance", "family", "split", "path", "capture_manifest"},
                "Each trace needs instance/family/split/path/capture_manifest")
        require(all(isinstance(value, str) and value for value in entry.values()) and
                entry["split"] in ("train", "heldout"),
                "Missing instance/family or invalid split")
        require(groups.setdefault(entry["family"], entry["split"]) == entry["split"],
                "A family crosses the declared train/held-out boundary")
        require(instances.setdefault(entry["instance"], (entry["family"], entry["split"])) ==
                (entry["family"], entry["split"]), "An instance crosses family/split assignments")
        path = regular(plan.parent / entry["path"]).resolve()
        require(re.fullmatch(r"(?:cpre|contains)-\d+\.tsv", path.name), "Not a finalized operation trace")
        require(path not in seen, "Repeated trace in plan")
        seen.add(path)
        capture = regular(plan.parent / entry["capture_manifest"]).resolve()
        if str(capture) not in captures:
            captures[str(capture)], bound = capture_identity(capture, entry["instance"])
            sources.update(bound)
        invocation = captures[str(capture)]
        require(invocation["instance"] == entry["instance"] and path.is_relative_to(invocation["snapshot_directory"]),
                "Operation is outside its declared instance/capture")
        require(inputs.setdefault(entry["instance"], invocation["input"]["sha256"]) == invocation["input"]["sha256"],
                "An instance refers to different TLSF bytes")
        directory = path.parent
        require(owners.setdefault(str(directory), (entry["instance"], str(capture))) ==
                (entry["instance"], str(capture)), "An automaton directory has multiple instance/capture owners")
        if str(directory) not in directories:
            files = sorted(directory.iterdir())
            require(all(file.is_file() and not file.is_symlink() for file in files),
                    "Nonregular entry in frozen automaton capture")
            hashes = {str(file): sha(file) for file in files}
            sources.update(hashes)
            with regular(directory / "meta.tsv").open() as stream:
                meta, = csv.DictReader(stream, delimiter="\t")
            require(meta["schema_version"] == "3", "Unsupported capture metadata")
            require(meta["instance"] == entry["instance"], "Snapshot metadata instance differs from invocation")
            regular(directory / "automaton.hoa")
            counts = None
            if (count_file := directory / "operation-counts.tsv").exists():
                with count_file.open() as stream:
                    raw, = csv.DictReader(stream, delimiter="\t")
                require(count_file.read_bytes().endswith(b"\n"), "Truncated operation counts")
                require(set(raw) == {"complete", "contains", "hits", "misses", "cpre_calls"},
                        "Malformed operation count table")
                counts = {name: int(value) for name, value in raw.items()}
                require(counts["complete"] == 1 and all(value >= 0 for value in counts.values()) and
                        counts["contains"] == counts["hits"] + counts["misses"], "Incomplete/inconsistent counts")
                require(counts["contains"] >= sum(bool(re.fullmatch(r"contains-\d+\.tsv", file.name)) for file in files) and
                        counts["cpre_calls"] >= sum(bool(re.fullmatch(r"cpre-\d+\.(?:tsv|skipped)", file.name)) for file in files),
                        "Complete operation counts are below observed captures")
            directories[str(directory)] = {"files_sha256": hashes, "metadata": meta,
                                            "complete_operation_counts": counts,
                                            "frequency_scope": "configured automaton only" if counts else "unknown/censored"}
        lines = path.read_text().splitlines()
        header = dict(re.findall(r"(?:^|\s)(\w+)=([^\s]+)", lines[0]))
        kind = "contains" if path.name.startswith("contains-") else "cpre"
        counts = directories[str(directory)]["complete_operation_counts"]
        if kind == "contains" and counts:
            ordinal, hits = int(header["ordinal"]), int(header["hits"])
            require(ordinal <= counts["contains"] and hits <= counts["hits"] and
                    ordinal - hits <= counts["misses"], "Query ordinal exceeds complete operation counts")
        begin = lines.index("[generators]" if kind == "contains" else "[before]") + 1
        end = lines.index("[end]" if kind == "contains" else "[actions]")
        generators = [tuple(map(int, line.split("\t"))) for line in lines[begin:end]]
        meta = directories[str(directory)]["metadata"]
        dimension, numeric = int(meta["states"]), int(meta["bool_threshold"])
        require(dimension > 0 and 0 <= numeric <= dimension and
                all(len(value) == dimension for value in generators), "Invalid logical dimension")
        coordinates = len(generators) * dimension
        rows.append({**entry, "id": number, "path": str(path), "capture_manifest": str(capture),
                     "kind": kind, "header": header,
                     "features": {"dimension": dimension, "numeric_prefix": numeric,
                                  "boolean_tail": dimension - numeric, "maxima": len(generators),
                                  "bottom_fraction": sum(value == -1 for row in generators for value in row) / coordinates if coordinates else None,
                                  "unique_numeric_prefixes": len({row[:numeric] for row in generators}),
                                  "unique_boolean_suffixes": len({row[numeric:] for row in generators}),
                                  "recorded_answer": int(header["expected"]) if kind == "contains" else None}})
    require(rows, "Empty trace plan")
    return rows, {"plan": str(plan), "sources_sha256": sources, "directories": directories, "captures": captures,
                  "sampling": "first CPre / hit / miss per frontier bin; unweighted point samples",
                  "split_policy": "fixed plan; complete families stay together; held-out data cannot tune thresholds"}


def unchanged(metadata, traces=None):
    for row in executable_entries(metadata):
        require(sha(Path(row["binary"])) == row["binary_sha256"], "A harness executable changed")
        require(libraries(Path(row["binary"])) == row["libraries"], "Resolved runtime dependencies changed")
    for name, expected in metadata.get("bound_files_sha256", {}).items():
        require(sha(regular(Path(name))) == expected, "A binary-map provenance input changed")
    for name, expected in metadata["source_sha256"].items():
        require(sha(Path(metadata.get("posets_source_dir", ROOT / "subprojects/posets")) / name) == expected,
                "A Posets source changed")
    require(sha(Path(__file__).resolve()) == metadata["driver_sha256"], "The matrix driver changed")
    for name, expected in metadata["support_sha256"].items():
        require(sha(ROOT / "benchmarking" / name) == expected, "Runner support changed")
    if traces:
        for name, expected in traces["sources_sha256"].items():
            require(sha(regular(Path(name))) == expected, f"Capture evidence changed: {name}")
        for name, entry in traces["directories"].items():
            require({str(path) for path in Path(name).iterdir()} == set(entry["files_sha256"]),
                    "Capture file inventory changed")


def save_logs(output, prefix, stdout, stderr):
    hashes = {}
    for suffix, text in (("stdout", stdout), ("stderr", stderr)):
        path = output / (prefix + "." + suffix)
        path.write_bytes(text.encode() if isinstance(text, str) else text)
        hashes[suffix + "_sha256"] = sha(path)
    return hashes


def process_record(result):
    return {key: value for key, value in asdict(result).items() if key not in ("stdout", "stderr")}


def status(result):
    return "timeout" if result.timed_out else "resource_limit" if result.resource_limited else "pass" if result.returncode == 0 else "rejected"


def measurement_build(metadata):
    common = None
    for mode in executable_entries(metadata):
        command = mode["compiler_command"]
        flags = command.get("arguments") or shlex.split(command["command"])
        if metadata.get("inventory") == contract.INVENTORY:
            contract.clean_flags(flags)
        optimization = [flag for flag in flags if re.fullmatch(r"-O(?:[0-3sgz]|fast)", flag)]
        require(mode.get("ndebug") is True and mode.get("stdlib_assertions") is False and
                optimization and optimization[-1] in ("-O3", "-Ofast") and
                any(flag.startswith("-flto") for flag in flags),
                "Timing requires release/O3-or-Ofast/LTO with NDEBUG and no stdlib assertions")
        comparable = (flags[0], mode["compiler"], tuple(flag for flag in flags
                      if flag.startswith(("-O", "-march=", "-mtune=", "-flto", "-std=", "-fno-"))))
        require(common is None or common == comparable, "Matrix compiler/target/optimization flags differ")
        common = comparable


def metrics(stdout):
    values = {}
    for name, value in re.findall(r"^  - ([\w]+): (\S+)$", stdout, flags=re.MULTILINE):
        require(name not in values, "Duplicate harness metric")
        values[name] = float(value)
    require(values and all(math.isfinite(value) and value >= 0 for value in values.values()),
            "Missing/nonfinite/negative harness metrics")
    return values


def output_identity(stdout, backend, vector):
    seen = re.findall(r"^\[--\] running tests for (.+)$", stdout, flags=re.MULTILINE)
    return seen == [f"posets::downsets::{backend}<posets::vectors::{vector}<test_value_type>>"]


def executable_entries(metadata):
    if "backend_binaries" not in metadata:
        return metadata["modes"].values()
    return [entry for mode in metadata["backend_binaries"].values() for entry in mode.values()] + list(metadata["alias_probes"].values())


def binary_entry(metadata, mode, backend):
    if "backend_binaries" in metadata:
        return metadata["backend_binaries"][metadata["aliases"][mode]][backend]
    return metadata["modes"][mode]


def replay(args, metadata, traces, output):
    rows, accepted = [], []
    scalar_mode = next(mode for mode in metadata["modes"] if metadata["aliases"][mode] == mode)
    cells = [(mode, backend, vector, binary_entry(metadata, mode, backend)) for mode, entry in metadata["modes"].items()
             if metadata["aliases"][mode] == mode for backend in entry["backends"] if backend in args.backends
             for vector in ("vector_backed", "simd_vector_backed") if vector != "vector_backed" or mode == scalar_mode]
    with campaign_scope_guard("posets-matrix"):
        for trace in traces:
            source_binary = metadata.get("source_oracle", cells[0][3])["binary"]
            source_command = [source_binary, "--trace", trace["path"], "--check=trace-source"]
            source_result = run_systemd_scope(source_command, args.source_timeout, args.memory_max, "0",
                                              unit_prefix="acacia-posets", scratch_root=output / "scopes")
            source_prefix = f"source-{trace['id']}"
            source_logs = save_logs(output, source_prefix, source_result.stdout, source_result.stderr)
            source_status = status(source_result)
            if source_status == "pass" and source_result.stdout != "[TRACE SOURCE OK]\n":
                source_status = "malformed_output"
            source_ok = source_status == "pass"
            rows.append({"trace": trace["id"], "phase": "source_oracle", "status": source_status,
                         "command": source_command, "log_prefix": source_prefix,
                         "logs_sha256": source_logs, "process": process_record(source_result)})
            for mode, backend, vector, entry in cells:
                if not source_ok:
                    rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                                 "phase": "check", "status": "source_unverified", "source_log_prefix": source_prefix})
                    continue
                command = [entry["binary"], "--trace", trace["path"], "--check=replay",
                           "--params=rounds=1", backend, vector + "<test_value_type>"]
                prefix = f"check-{trace['id']}-{mode}-{backend}-{vector}"
                result = run_systemd_scope(command, args.check_timeout, args.memory_max, "0",
                                           unit_prefix="acacia-posets", scratch_root=output / "scopes")
                logs = save_logs(output, prefix, result.stdout, result.stderr)
                outcome = status(result)
                identity = output_identity(result.stdout, backend, vector)
                require(outcome != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                             "phase": "check", "status": outcome, "process": process_record(result),
                             "command": command, "log_prefix": prefix, "logs_sha256": logs,
                             "output_identity_verified": identity})
                if outcome == "pass" and vector == "simd_vector_backed":
                    accepted.append((trace, mode, backend, vector, entry))
        if args.measure:
            order = []
            accepted_keys = {(trace["id"], mode, backend, vector)
                             for trace, mode, backend, vector, _ in accepted}
            for repetition in range(args.repetitions):
                rng = random.Random(args.seed + repetition)
                groups = [[(trace, mode, backend, vector, entry)
                           for mode, backend, vector, entry in cells if vector == "simd_vector_backed"]
                          for trace in traces]
                rng.shuffle(groups)
                for group in groups:
                    rng.shuffle(group)
                    order.extend((repetition, *cell) for cell in group
                                 if (cell[0]["id"], *cell[1:4]) in accepted_keys)
            planned = [{"repetition": repetition, "trace": trace["id"], "mode": mode, "backend": backend,
                        "vector": vector} for repetition, trace, mode, backend, vector, _ in order]
            (output / "measurement-order.json").write_text(json.dumps(planned, indent=2) + "\n")
            for repetition, trace, mode, backend, vector, entry in order:
                command = [entry["binary"], "--trace", trace["path"],
                           f"--params=rounds={args.rounds},detail={int(args.detail)}",
                           backend, vector + "<test_value_type>"]
                prefix = f"measure-{repetition}-{trace['id']}-{mode}-{backend}-{vector}"
                result = run_systemd_scope(command, args.case_timeout, args.memory_max, "0",
                                           unit_prefix="acacia-posets", scratch_root=output / "scopes")
                logs = save_logs(output, prefix, result.stdout, result.stderr)
                outcome = status(result)
                identity = output_identity(result.stdout, backend, vector)
                require(outcome != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                values = metrics(result.stdout) if outcome == "pass" else None
                if values:
                    require(values["rounds"] == args.rounds and
                            values["dimension"] == trace["features"]["dimension"] and
                            values["before_maxima"] == trace["features"]["maxima"] and
                            values["boolean_tail"] == trace["features"]["boolean_tail"],
                            "Metric/source identity mismatch")
                rows.append({"trace": trace["id"], "mode": mode, "backend": backend, "vector": vector,
                             "phase": "measure", "repetition": repetition, "status": outcome,
                             "command": command, "log_prefix": prefix, "logs_sha256": logs, "metrics": values,
                             "output_identity_verified": identity,
                             "process": process_record(result)})
    return rows


def bound_file(entry, bound):
    require(isinstance(entry, dict) and set(entry) == {"path", "sha256"}, "Invalid file binding")
    path = Path(entry["path"])
    require(path.is_absolute() and re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) and
            sha(regular(path)) == entry["sha256"], "Bound file hash mismatch")
    bound[str(path)] = entry["sha256"]
    return path


def command_args(command):
    args = command.get("arguments") or shlex.split(command["command"])
    require(isinstance(args, list) and args and all(isinstance(arg, str) for arg in args), "Invalid command argv")
    if "arguments" in command and "command" in command:
        require(args == shlex.split(command["command"]), "Command representations differ")
    return args


def command_path(command, value):
    directory = Path(command["directory"])
    require(directory.is_absolute(), "Compiler directory must be absolute")
    return (directory / value).resolve()


def normalized_command(command, source, build, object_file=None, selector=False):
    """Retain every flag; normalize only declared source/build and output paths."""
    args, normalized, index, selectors = command_args(command), [], 0, 0
    output = command_path(command, command["output"])

    def path_name(value):
        path = command_path(command, value)
        if object_file is not None and path == object_file:
            return "$OBJECT"
        for root, name in ((build, "$BUILD"), (source, "$SOURCE")):
            if path.is_relative_to(root):
                return name + "/" + str(path.relative_to(root))
        return str(path)

    while index < len(args):
        arg = args[index]
        if arg.startswith("-DPOSETS_MATRIX_BACKEND="):
            require(selector and arg.split("=", 1)[1], "Unexpected backend selector")
            selectors += 1
        elif arg in ("-o", "-MF", "-MT", "-MQ"):
            require(index + 1 < len(args), "Missing output/dependency argument")
            value = command_path(command, args[index + 1])
            expected = Path(str(output) + ".d") if arg == "-MF" else output
            require(value == expected, "Unexpected output/dependency path")
            normalized.extend((arg, "$OUTPUT" + (".d" if arg == "-MF" else "")))
            index += 1
        elif arg in ("-I", "-L", "-isystem", "-iquote"):
            require(index + 1 < len(args), "Missing include/library argument")
            normalized.extend((arg, path_name(args[index + 1])))
            index += 1
        elif arg.startswith(("-I", "-L")) and len(arg) > 2:
            normalized.append(arg[:2] + path_name(arg[2:]))
        elif not arg.startswith("-") and (arg.endswith((".cc", ".o"))):
            normalized.append(path_name(arg))
        else:
            normalized.append(arg)
        index += 1
    require(selectors == int(selector), "Missing/duplicate backend selector")
    return normalized


def inspect_binary(binary, output, name):
    require(regular(binary).read_bytes()[:4] == b"\x7fELF", "Harness must be an ELF, not a launcher")
    metadata = json.loads(subprocess.check_output([str(binary), "--metadata"], text=True))
    assembly = subprocess.check_output(["objdump", "--disassemble=posets_matrix_compare", str(binary)], text=True)
    (output / (name + "-compare.asm")).write_text(assembly)
    instructions = [match[1] for line in assembly.splitlines()
                    if (match := re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)", line))]
    require(instructions, "Missing emitted SIMD comparison kernel")
    return {**metadata, "binary": str(binary), "binary_sha256": sha(binary), "libraries": libraries(binary),
            "comparison_instructions_sha256": hashlib.sha256(bytes.fromhex(" ".join(instructions))).hexdigest()}


def abi_key(entry):
    return tuple(entry[key] for key in ("simd_type", "selected_lanes", "block_bytes", "alignment",
                                        "element_bytes", "compatible_lanes", "native_lanes", "max_fixed_lanes",
                                        "hybrid_policy", "comparison_instructions_sha256"))


def build_invocations(record, bound):
    plan = json.loads(bound_file(record["build_plan"], bound).read_text())
    bound_file(record["builder"], bound)
    require(type(plan.get("schema_version")) is int and plan["schema_version"] == 1 and
            plan.get("kind") == "posets-selector-build-plan" and
            all(plan.get(name) == record[name] for name in ("base_revision", "source_revision", "source_dir")),
            "Build plan source pin mismatch")
    rows = [row for row in plan["rows"] if (row["mode"], row["backend"]) == (record["mode"], record["backend"])]
    require(len(rows) == 1, "Missing/duplicate build plan row")
    row = rows[0]
    if record.get("inventory") == contract.INVENTORY:
        contract.check_cells(plan, "rows", True, BACKENDS)
        require(all(record.get(key) == value for key, value in contract.source_fields(plan).items()),
                "Selected build plan/record contract differs")
        bound_file(plan["rank_source_manifest"], bound)
        require(bound_file(plan["rank_contract"], bound) == Path(contract.__file__).resolve(),
                "Selected build used another source contract")
    names = ("compiler_command", "linker_command", "template_compiler_command", "template_linker_command")
    require(all(row[name] == record[name] for name in names), "Build plan argv differs from build record")
    require([item["stage"] for item in row["stages"]] == ["compile", "link"] and
            [item["stage"] for item in record["invocations"]] == ["compile", "link"], "Incomplete build invocations")
    for stage, invocation, name in zip(row["stages"], record["invocations"], names[:2]):
        spec_path = bound_file(stage["spec"], bound)
        path = bound_file({key: invocation[key] for key in ("path", "sha256")}, bound)
        spec, observed = json.loads(spec_path.read_text()), json.loads(path.read_text())
        require(observed["spec"] == spec and observed["spec_sha256"] == sha(spec_path) and
                spec["command"] == command_args(record[name]) and spec["cwd"] == record[name]["directory"],
                "Actual build invocation differs from declared argv/cwd")
        result = observed["result"]
        require(type(result["returncode"]) is int and result["returncode"] == 0 and
                result["timed_out"] is False and result["resource_limited"] is False and
                observed["memory_max"] == "8G" and observed["memory_swap_max"] == "0",
                "Build invocation failed or escaped the required memory scope")
        bound_file({"path": observed["log"], "sha256": observed["log_sha256"]}, bound)
        bound_file({"path": str(path.with_suffix(".helper.py")), "sha256": observed["helper_sha256"]}, bound)
        require(observed["benchlib_sha256"] == sha(ROOT / "benchmarking/benchlib.py"), "Build scope helper changed")
        if spec.get("environment_file"):
            environment = regular(Path(spec["environment_file"]))
            bound[str(environment)] = sha(environment)
            require(all(observed["environment_overrides"].get(key) == value
                        for key, value in json.loads(environment.read_text()).items()
                        if key not in {"ACACIA_CAMPAIGN_SCOPE_GUARD", "PYTHONDONTWRITEBYTECODE"}),
                    "Build environment changed")


def selector_source(source, base, revision, overlay):
    require(subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip() == revision and
            not subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True),
            "Measurement source pin/cleanliness mismatch")
    names = set(subprocess.check_output(["git", "diff", "--name-only", base, revision], cwd=source, text=True).splitlines())
    require(names <= {"tests/downset-bm.cc", "tests/meson.build"} and "tests/downset-bm.cc" in names,
            "Measurement overlay changes more than the selector")
    before = subprocess.check_output(["git", "show", base + ":tests/downset-bm.cc"], cwd=source, text=True)
    marker = "  register_maker<false> ((vector_types*) nullptr, (set_types*) nullptr);\n"
    require(before.count(marker) == 1, "Unknown base selector registration")
    selected = ("#if defined(POSETS_MATRIX_ONLY) && defined(POSETS_MATRIX_BACKEND)\n"
                "  register_maker<false, POSETS_MATRIX_BACKEND> ((vector_types*) nullptr);\n#else\n" + marker + "#endif\n")
    expected = before.replace(marker, selected)
    require((source / "tests/downset-bm.cc").read_text() == expected, "Selector harness overlay differs")
    before = subprocess.check_output(["git", "show", base + ":tests/meson.build"], cwd=source, text=True)
    expected = before.replace("# Keep the 14-backend matrix", "# Keep the 24-backend matrix") if "tests/meson.build" in names else before
    require((source / "tests/meson.build").read_text() == expected,
            "Selector build overlay differs")
    require(subprocess.run(["git", "apply", "--reverse", "--check", str(overlay)], cwd=source,
                           capture_output=True).returncode == 0, "Declared selector overlay does not match source")


def backend_provenance(build, output, source, map_path, rank_only=False):
    bound = {str(map_path): sha(regular(map_path)),
             str(Path(contract.__file__).resolve()): sha(Path(contract.__file__).resolve())}
    plan = json.loads(map_path.read_text())
    require(type(plan.get("schema_version")) is int and plan["schema_version"] == 1 and plan.get("kind") == "posets-per-backend-binaries",
            "Unsupported per-backend map schema")
    require(all(isinstance(plan.get(key), str) and re.fullmatch(r"[0-9a-f]{40}", plan[key])
                for key in ("base_revision", "source_revision")), "Invalid map source pins")
    selected_backends = set(contract.backends(plan, rank_only, BACKENDS))
    if rank_only:
        contract.check_cells(plan, "binaries", True, BACKENDS)
    declared_source = Path(plan["source_dir"])
    require(declared_source.is_absolute() and (source is None or source.resolve() == declared_source.resolve()),
            "Binary map/source directory mismatch")
    source = declared_source.resolve()
    if rank_only:
        approved = bound_file(plan["rank_source_manifest"], bound)
        require(bound_file(plan["rank_contract"], bound) == Path(contract.__file__).resolve(),
                "Selected map used another source contract")
        source_proof = contract.validate_source(source, plan["base_revision"], plan["source_revision"],
                                                plan["rank_source_role"], approved)
        source_binding = {key: plan[key] for key in
                          ("inventory", "rank_source_role", "rank_source_manifest", "rank_contract")}
        source_binding.update(source_proof)
    else:
        overlay = bound_file(plan["selector_overlay"], bound)
        selector_source(source, plan["base_revision"], plan["source_revision"], overlay)
        source_binding = {"selector_overlay": plan["selector_overlay"]}
    anchor_path = bound_file(plan["anchor_provenance"], bound)
    anchor = json.loads(anchor_path.read_text())
    anchor = anchor.get("provenance", anchor)
    require(anchor["posets_revision"] == plan["source_revision" if rank_only else "base_revision"] and not anchor["posets_dirty"],
            "Binary map does not match the frozen anchor source pin")
    anchor_source = Path(anchor["posets_source_dir"])
    require(anchor_source.is_absolute(), "Anchor source must be explicit")
    if rank_only:
        require(anchor_source.resolve() == source, "Selected anchor must use the actual same source revision/tree")
        require(anchor.get("rank_anchor") is True and anchor.get("rank_source_role") == plan["rank_source_role"] and
                anchor.get("rank_source_manifest") == plan["rank_source_manifest"],
                "Selected map does not match its explicit actual-revision rank anchor")
    for name, expected_hash in anchor["source_sha256"].items():
        file = regular(anchor_source / name)
        require(sha(file) == expected_hash, "Frozen anchor source changed")
        bound[str(file)] = expected_hash
    oracle = bound_file(plan["source_oracle_binary"], bound)
    require(str(oracle) == anchor["modes"]["compatible"]["binary"] and
            sha(oracle) == anchor["modes"]["compatible"]["binary_sha256"] and
            oracle == build / "tests/downset-matrix-compatible", "Source oracle is not the matching frozen compatible anchor")
    probes = {}
    for mode in MODES:
        old = anchor["modes"][mode]
        actual = inspect_binary(Path(old["binary"]), output, "anchor-" + mode)
        require(all(old.get(key) == value for key, value in actual.items()), "Frozen anchor binary/probe changed")
        require(actual["backends"] == sorted(BACKENDS),
                "Anchor backend inventory changed")
        probes[mode] = {**actual, "compiler_command": old["compiler_command"]}
    require(plan["aliases"] == {"max": "native"} and abi_key(probes["max"]) == abi_key(probes["native"]),
            "Unsupported/unproven width alias")
    require(abi_key(probes["compatible"]) != abi_key(probes["native"]), "Map does not describe two effective widths")
    entries = {mode: {} for mode in ("compatible", "native")}
    templates = {}

    def template_link(directory, mode):
        key = (str(directory), mode)
        if key not in templates:
            manifest = regular(directory / "build.ninja")
            bound[str(manifest)] = sha(manifest)
            lines = subprocess.check_output(["ninja", "-C", str(directory), "-t", "commands",
                                             "tests/downset-matrix-" + mode], text=True).splitlines()
            require(len(lines) == 2, "Expected the original one-TU matrix build")
            templates[key] = {"arguments": shlex.split(lines[-1]), "directory": str(directory),
                              "output": "tests/downset-matrix-" + mode}
        return templates[key]

    for item in plan["binaries"]:
        mode, backend = item["mode"], item["backend"]
        require(mode in entries and backend in selected_backends and backend not in entries[mode],
                "Duplicate/unknown per-backend map entry")
        binary = Path(item["binary"])
        require(binary.is_absolute() and sha(regular(binary)) == item["binary_sha256"], "Mapped binary hash mismatch")
        record_path = bound_file(item["build_record"], bound)
        record = json.loads(record_path.read_text())
        require(type(record.get("schema_version")) is int and record["schema_version"] == 1 and record.get("kind") == "posets-selector-build" and
                all(record.get(key) == value for key, value in {
                    "mode": mode, "backend": backend, "base_revision": plan["base_revision"],
                    "source_revision": plan["source_revision"], "source_dir": str(source),
                    **contract.source_fields(plan),
                    "binary_sha256": item["binary_sha256"]}.items()), "Build record/map source identity mismatch")
        db_path = bound_file(record["compile_commands"], bound)
        commands = json.loads(db_path.read_text())
        template, command = record["template_compiler_command"], record["compiler_command"]
        directory = Path(template["directory"])
        require(directory.is_absolute() and db_path == directory / "compile_commands.json" and
                command["directory"] == template["directory"], "Compiler directory/database mismatch")
        require(sum(command_args(row) == command_args(template) and row["directory"] == template["directory"] and
                    row["file"] == template["file"] and row.get("output") == template.get("output") for row in commands) == 1,
                "Missing/ambiguous original compiler template")
        require(command_path(command, command["file"]) == source / "tests/downset-bm.cc" and
                command_path(template, template["file"]) == source / "tests/downset-bm.cc",
                "Compiler is not bound to the selector harness source")
        old_command = probes[mode]["compiler_command"]
        old_directory = Path(old_command["directory"])
        require(normalized_command(template, source, directory) ==
                normalized_command(old_command, anchor_source, old_directory), "Compiler template differs from frozen anchor flags")
        selectors = [arg for arg in command_args(command) if arg.startswith("-DPOSETS_MATRIX_BACKEND=")]
        require(selectors == ["-DPOSETS_MATRIX_BACKEND=" + BACKEND_SELECTORS[backend]], "Wrong declared backend selector")
        require(normalized_command(command, source, directory, selector=True) ==
                normalized_command(template, source, directory), "Selector compiler argv changes flags")
        link, template_linker = record["linker_command"], record["template_linker_command"]
        require(link["directory"] == template_linker["directory"] == str(directory) and
                command_args(template_linker) == command_args(template_link(directory, mode)) and
                command_path(link, link["output"]) == binary,
                "Linker is not bound to the original template/output")
        old_linker = template_link(old_directory, mode)
        require(normalized_command(template_linker, source, directory, command_path(template, template["output"])) ==
                normalized_command(old_linker, anchor_source, old_directory, command_path(old_command, old_command["output"])),
                "Linker template differs from frozen anchor flags")
        require(normalized_command(link, source, directory, command_path(command, command["output"])) ==
                normalized_command(template_linker, source, directory, command_path(template, template["output"])),
                "Selector linker argv changes flags")
        build_invocations(record, bound)
        actual = inspect_binary(binary, output, mode + "-" + backend)
        require(actual["backends"] == [backend], "Mapped ELF is not the requested singleton backend")
        require(actual["width_selector"] == MODES.index(mode) and abi_key(actual) == abi_key(probes[mode]) and
                actual["compiler"] == probes[mode]["compiler"] and actual["libraries"] == probes[mode]["libraries"],
                "Mapped ELF width/compiler/runtime proof differs from anchor")
        entries[mode][backend] = {**actual, "compiler_command": command, "linker_command": link,
                                  "build_record": {"path": str(record_path), "sha256": sha(record_path)}}
    require(all(set(rows) == selected_backends for rows in entries.values()), "Missing per-backend map entries")
    modes = {mode: {key: value for key, value in probes[mode].items()
                    if key not in {"binary", "binary_sha256", "libraries", "compiler_command"}} for mode in MODES}
    if rank_only:
        for entry in modes.values():
            entry["backends"] = [contract.RANK]
    return source, {"modes": modes, "aliases": {"compatible": "compatible", "native": "native", "max": "native"},
                    "backend_binaries": entries, "alias_probes": probes, "source_oracle": probes["compatible"],
                    "bound_files_sha256": bound, "binary_map": {"path": str(map_path), "sha256": sha(map_path)},
                    "base_revision": plan["base_revision"], **source_binding,
                    "treatment": "per-backend selector; identical original replay kernels/timers"}


def provenance(build, output, source=None, binary_map=None, rank_only=False):
    if binary_map is not None:
        source, extra = (backend_provenance(build, output, source, binary_map, True) if rank_only else
                         backend_provenance(build, output, source, binary_map))
        modes, aliases = extra["modes"], extra["aliases"]
    else:
        require(not rank_only, "Selected rank requires a binary map")
        extra = {"bound_files_sha256": {str(Path(contract.__file__).resolve()):
                                        sha(Path(contract.__file__).resolve())}}
        modes, aliases = combined_provenance(build, output)
    source = (source or ROOT / "subprojects/posets").resolve()
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                                    cwd=source, text=True).splitlines()
    return {**extra, "modes": modes, "aliases": aliases, "posets_source_dir": str(source),
            "posets_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
            "source_sha256": {name: sha(source / name) for name in names if (source / name).is_file()},
            "posets_dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True),
            "support_sha256": {name: sha(ROOT / "benchmarking" / name) for name in ("benchlib.py", "scope_memory.py")},
            "driver_sha256": sha(Path(__file__).resolve()), "scheduler_reference_sha256": SCHEDULER_REFERENCE_SHA256,
            "affinity": sorted(os.sched_getaffinity(0)),
            "cwd": str(Path.cwd()),
            "environment": {name: value for name, value in os.environ.items()
                            if name.startswith(("ACACIA_", "LD_", "MALLOC_", "GLIBC_", "OMP_"))},
            "cpu": subprocess.check_output(["lscpu", "-J"], text=True)}


def combined_provenance(build, output):
    commands = json.loads((build / "compile_commands.json").read_text())
    modes, aliases, seen = {}, {}, {}
    for mode in MODES:
        binary = build / "tests" / f"downset-matrix-{mode}"
        metadata = json.loads(subprocess.check_output([str(binary), "--metadata"], text=True))
        assembly = subprocess.check_output(
            ["objdump", "--disassemble=posets_matrix_compare", str(binary)], text=True)
        (output / f"{mode}-compare.asm").write_text(assembly)
        instructions = [match[1] for line in assembly.splitlines()
                        if (match := re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)", line))]
        require(bool(instructions), "Missing emitted SIMD comparison kernel")
        instruction_hash = hashlib.sha256(bytes.fromhex(" ".join(instructions))).hexdigest()
        key = (metadata["simd_type"], metadata["alignment"], instruction_hash, metadata.get("hybrid_policy"))
        aliases[mode] = seen.setdefault(key, mode)
        selected = [row for row in commands if f"downset-matrix-{mode}.p/" in row.get("output", "")]
        require(len(selected) == 1, f"Missing/ambiguous compiler command for {mode}")
        require(len(metadata["backends"]) == len(BACKENDS) and set(metadata["backends"]) == BACKENDS,
                "The requested backend inventory changed")
        modes[mode] = {**metadata, "binary": str(binary), "binary_sha256": sha(binary),
                       "libraries": libraries(binary),
                       "comparison_instructions_sha256": instruction_hash, "compiler_command": selected[0]}
    return modes, aliases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path,
                        help="Frozen Posets source used by this build (default: subprojects/posets)")
    parser.add_argument("--binary-map", type=Path,
                        help="Hash-bound per-backend ELFs with original combined source oracle and proven width alias")
    parser.add_argument("--rank-only", action="store_true",
                        help="Explicit reviewed789 rank-only map; default still requires full48")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case-timeout", type=float, default=15)
    parser.add_argument("--check-timeout", type=float, help="Separate backend correctness cap; defaults to --case-timeout")
    parser.add_argument("--source-timeout", type=float, help="Separate scalar source-oracle cap; defaults to --check-timeout")
    parser.add_argument("--trace-plan", type=Path, help="Fixed JSON capture/split plan; otherwise run synthetic semantic cases")
    parser.add_argument("--measure", action="store_true", help="Explicitly enable serial actual-trace timing after correctness checks")
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--detail", action="store_true", help="Instrument apply/union/intersection separately; not clean CPre timings")
    parser.add_argument("--memory-max", default="8G")
    parser.add_argument("--seed", type=int, default=0, help="Fixed within-trace measurement-order shuffle seed")
    parser.add_argument("--backends", nargs="+", choices=sorted(BACKENDS), default=sorted(BACKENDS),
                        help="Explicit subset for later hybrid/profile follow-ups; default retains all24")
    args = parser.parse_args()
    no_core()
    args.check_timeout = args.case_timeout if args.check_timeout is None else args.check_timeout
    args.source_timeout = args.check_timeout if args.source_timeout is None else args.source_timeout
    if args.rank_only:
        require(args.binary_map and args.backends == [contract.RANK] and
                (args.source_timeout, args.check_timeout, args.case_timeout) == (30, 2, 2) and
                args.rounds == args.repetitions == 1 and args.memory_max == "8G" and not args.detail,
                "Selected rank requires exact backend and unchanged30/2/2 clean protocol")
    require(all(math.isfinite(cap) and cap > 0 for cap in (args.case_timeout, args.check_timeout, args.source_timeout)) and
            args.rounds > 0 and args.repetitions > 0, "Positive finite timeout/rounds/repetitions required")
    require(not args.measure or args.trace_plan, "Timing requires a fixed actual-trace plan")
    require(not args.detail or args.measure, "Detailed instrumentation requires --measure")
    build, output = args.build_dir.resolve(), args.output.resolve()
    if args.source_dir:
        args.source_dir = args.source_dir.resolve()
    if args.trace_plan:
        args.trace_plan = args.trace_plan.resolve()
    if args.binary_map:
        args.binary_map = args.binary_map.resolve()
    os.chdir(ROOT)  # Also used by ldd when a library override contains relative paths.
    require(not output.exists(), "Output exists; preserve it and choose a new directory")
    require(subprocess.run(["git", "check-ignore", "-q", str(output)], cwd=ROOT).returncode == 0,
            "Raw matrix evidence must start in an ignored output directory")
    output.mkdir(parents=True)
    metadata = (provenance(build, output, args.source_dir, args.binary_map, True) if args.rank_only else
                provenance(build, output, args.source_dir, args.binary_map))
    if args.measure:
        measurement_build(metadata)
    trace_source = None
    if args.trace_plan:
        traces, trace_source = load_traces(args.trace_plan.resolve())
        (output / "inputs.json").write_text(json.dumps({"provenance": trace_source, "traces": traces}, indent=2) + "\n")
        rows = replay(args, metadata, traces, output)
        unchanged(metadata, trace_source)
        scope = "Instrumented operation diagnosis" if args.detail else "Clean whole-CPre / sampled actual-query replay" if args.measure else "Actual-trace correctness only"
        if args.binary_map:
            scope = "Supplemental per-backend registry-instantiation treatment: " + scope
        if args.rank_only:
            scope = "Selected rank / matched789 source experiment: " + scope
        report = {"scope": scope,
                  "provenance": metadata, "inputs_sha256": sha(output / "inputs.json"),
                  "measurement_order_sha256": sha(output / "measurement-order.json") if args.measure else None,
                  "treatment": {"memory_max": args.memory_max, "memory_swap_max": "0", "serial": True,
                                "core_limit_bytes": 0,
                                "rounds": args.rounds, "repetitions": args.repetitions,
                                "backends": args.backends,
                                "seed": args.seed,
                                "detail": args.detail, "case_timeout": args.case_timeout},
                  "semantic_caps": {"source_timeout": args.source_timeout, "check_timeout": args.check_timeout},
                  "limits": ["Point samples are unweighted; full search/portfolio frequencies are not reconstructed.",
                             "Scalar vectors run as correctness controls once per backend/trace; timed cells cover unique SIMD treatments.",
                             "Each replay round reconstructs the source frontier; sharing history outside the sampled operation is unknown.",
                             "Build/rebuild include logical-to-vector materialization and backend canonicalization. CPre includes backend conversions.",
                             "CPre membership is a derived initial-state control; contains trace membership is an actual recorded query.",
                             "Process/cgroup peak and CPU include input parsing and result checks. Operation timers exclude them.",
                             "Allocation counts/cache events require separate explicitly instrumented profiles; not collected here.",
                             "The scalar representation is a correctness control; compiler loop vectorization is possible.",
                             "All-Boolean positive-dimensional traces remain distinct from zero-dimensional sets; packed-tail integration requires its own oracle."] +
                            (["Each ELF retains scalar and SIMD vectors with one backend; this supplemental compiler-context treatment does not replace full-matrix observations or solver validation."] if args.binary_map else []),
                  "traces": traces, "rows": rows,
                  "counts": dict(Counter(row["status"] for row in rows)),
                  "performance_measurements": sum(row["phase"] == "measure" for row in rows)}
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"output": str(output), "aliases": metadata["aliases"], "counts": report["counts"]}, indent=2))
        return
    rows = []
    for mode, entry in metadata["modes"].items():
        if metadata["aliases"][mode] != mode:
            continue
        for backend in entry["backends"]:
            # Scalar is an independent semantic control, not a promise about the
            # compiler's automatic vectorization of ordinary integer loops.
            for vector in ("vector_backed", "simd_vector_backed"):
                for case in CASES:
                    selected = binary_entry(metadata, mode, backend)
                    command = [selected["binary"], "--check=" + case, backend, vector + "<test_value_type>"]
                    prefix = f"{mode}-{backend}-{vector}-{case}"
                    try:
                        result = subprocess.run(command, capture_output=True, text=True,
                                                timeout=args.check_timeout, preexec_fn=no_core)
                        status, code = ("pass" if result.returncode == 0 else "rejected"), result.returncode
                        stdout, stderr = result.stdout, result.stderr
                    except subprocess.TimeoutExpired as error:
                        status, code = "timeout", None
                        stdout, stderr = error.stdout or b"", error.stderr or b""
                    logs = save_logs(output, prefix, stdout, stderr)
                    identity = output_identity(stdout.decode() if isinstance(stdout, bytes) else stdout, backend, vector)
                    require(status != "pass" or identity, "Passing cell did not invoke exactly the requested backend/vector")
                    rows.append({"mode": mode, "backend": backend, "vector": vector,
                                 "case": case, "status": status, "exit": code, "log_prefix": prefix,
                                 "logs_sha256": logs,
                                 "output_identity_verified": identity})
    unchanged(metadata)
    report = {"scope": ("Selected rank / matched789 source experiment: " if args.rank_only else "") +
                       ("Supplemental per-backend registry-instantiation treatment: " if args.binary_map else "") + "Semantic screen only; no operation timings sampled",
              "cases": "empty downset, zero-dimensional singleton and all-minus-one bottom are distinct",
              "policy": "Every rejection/crash/timeout is retained. Constructor preconditions, empty results and zero-dimensional domains stay distinct; actual-trace eligibility is checked separately.",
              "provenance": metadata, "rows": rows,
              "counts": dict(Counter(row["status"] for row in rows)), "performance_measurements": 0}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(output), "aliases": metadata["aliases"], "counts": report["counts"]}, indent=2))


if __name__ == "__main__":
    main()
