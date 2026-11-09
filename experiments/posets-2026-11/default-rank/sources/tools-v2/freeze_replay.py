#!/usr/bin/env python3
"""Freeze and validate three release replay binaries from an immutable source."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
import rank_contract as contract
ROOT = contract.REPO
SOURCE = ROOT.parent / "posets-preopt-baseline"
OUTPUT = ROOT / "_deps/replay-baseline-7c40712"
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_tool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--revision", default="7c407128326583abbb137fd3832857ed506e20fb")
    parser.add_argument("--rank-only", action="store_true",
                        help="Full24 anchor for the explicit reviewed ranked experiment")
    parser.add_argument("--rank-source-role", choices=["baseline", "candidate"])
    parser.add_argument("--rank-source-manifest", type=Path)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    build = source / "build-replay-release"
    revision = subprocess.check_output(["git", "--no-optional-locks", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    assert revision == args.revision
    if args.rank_only:
        assert args.rank_source_manifest is not None
        assert output.is_relative_to(ROOT / '_deps') and output != OUTPUT and not output.is_relative_to(source)
        contract.validate_source(source, contract.BASE, revision, args.rank_source_role,
                                 args.rank_source_manifest)
    else:
        assert args.rank_source_role is None and args.rank_source_manifest is None
        assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=source, text=True)
    assert not output.exists(), "Do not overwrite frozen artifacts"
    (output / "tests").mkdir(parents=True)
    for mode in ("compatible", "native", "max"):
        shutil.copy2(build / "tests" / f"downset-matrix-{mode}", output / "tests" / f"downset-matrix-{mode}")
    for name in ("compile_commands.json", "meson-info/intro-buildoptions.json",
                 "meson-info/intro-compilers.json", "meson-info/intro-buildsystem_files.json"):
        target = output / name
        target.parent.mkdir(exist_ok=True)
        shutil.copy2(build / name, target)
    M = load_tool(str(HERE / "posets-matrix.py"))
    metadata = M.provenance(output, output, source)
    M.measurement_build(metadata)
    if args.rank_only:
        for mode in metadata["modes"].values():
            contract.clean_flags(M.command_args(mode["compiler_command"]))
        metadata.update({"rank_anchor": True, "rank_source_role": args.rank_source_role,
                         "rank_source_manifest": contract.binding(args.rank_source_manifest),
                         "rank_contract": contract.binding(Path(contract.__file__).resolve()),
                         "production_reference_revision": contract.PRODUCTION,
                         "baseline_headers_cc_equivalent": True})
    assert len(M.BACKENDS) == 24
    assert all(set(mode["backends"]) == M.BACKENDS and len(mode["backends"]) == 24
               for mode in metadata["modes"].values())
    metadata["frozen_source_clone"] = str(source)
    metadata["frozen_source_revision"] = revision
    metadata["build_directory"] = str(build)
    metadata["purpose"] = "frozen clean release artifact; no allocation counters or timing results"
    for mode in metadata["modes"].values():
        command = mode["compiler_command"]
        actual_source = (Path(command["directory"]) / command["file"]).resolve()
        assert actual_source == source / "tests/downset-bm.cc", "Compiler source differs from frozen source"
        assert M.sha(actual_source) == metadata["source_sha256"]["tests/downset-bm.cc"]
    if not args.rank_only and revision != "7c407128326583abbb137fd3832857ed506e20fb":
        baseline = json.loads((OUTPUT / "provenance.json").read_text())
        assert all(row["compiler_command"]["command"] == baseline["modes"][name]["compiler_command"]["command"]
                   for name, row in metadata["modes"].items()), "Baseline/optimized compiler flags differ"
        metadata["matched_baseline_provenance"] = {"path": str(OUTPUT / "provenance.json"),
                                                   "sha256": M.sha(OUTPUT / "provenance.json")}
    (output / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
    for path in output.rglob("*"):
        if path.is_file():
            path.chmod(0o555 if os.access(path, os.X_OK) else 0o444)
    print(json.dumps({"path": str(output), "source_revision": revision,
                      "aliases": metadata["aliases"],
                      "modes": {name: {"backends": len(row["backends"]),
                                       "lanes": row["selected_lanes"], "sha256": row["binary_sha256"]}
                                for name, row in metadata["modes"].items()}}, indent=2))


if __name__ == "__main__":
    main()
