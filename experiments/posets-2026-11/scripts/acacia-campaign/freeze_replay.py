#!/usr/bin/env python3
"""Freeze and validate three release replay binaries from an immutable source."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent / "acacia-posets-eval"
SOURCE = ROOT.parent / "posets-preopt-baseline"
OUTPUT = ROOT / "_deps/replay-baseline-7c40712"
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import load_tool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--revision", default="7c407128326583abbb137fd3832857ed506e20fb")
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    build = source / "build-replay-release"
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    assert revision == args.revision
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
    M = load_tool("benchmarking/posets-matrix.py")
    metadata = M.provenance(output, output, source)
    M.measurement_build(metadata)
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
    if revision != "7c407128326583abbb137fd3832857ed506e20fb":
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
