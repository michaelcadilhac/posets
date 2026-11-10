#!/usr/bin/env python3
"""Run an isolated setup/check command through Acacia's 8G/no-swap scope owner."""
import argparse
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent / "acacia-posets-eval"
sys.path.insert(0, str(ROOT / "benchmarking"))
from benchlib import run_systemd_scope


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path)
    parser.add_argument("record", type=Path)
    args = parser.parse_args()
    args.spec = args.spec.resolve(strict=True)
    args.record = args.record.resolve()
    helper_source = Path(__file__).read_bytes()
    spec = json.loads(args.spec.read_text())
    command = spec["command"]
    assert command and all(isinstance(part, str) for part in command), "Expected argv strings"
    cwd = Path(spec["cwd"]).resolve(strict=True)
    assert cwd.is_relative_to(ROOT), "Only the isolated Acacia checkout may be used"
    assert args.record.resolve().is_relative_to(ROOT.parent), "Evidence must remain in this task's work directory"
    overrides = json.loads(Path(spec["environment_file"]).read_text()) if spec.get("environment_file") else {}
    overrides["ACACIA_CAMPAIGN_SCOPE_GUARD"] = os.environ.get("ACACIA_CAMPAIGN_SCOPE_GUARD") or f"scoped-task:{os.getpid()}"
    overrides["PYTHONDONTWRITEBYTECODE"] = "1"
    os.chdir(cwd)
    args.record.parent.mkdir(parents=True, exist_ok=True)
    args.record.with_suffix(".helper.py").write_bytes(helper_source)
    log = args.record.with_suffix(".log")
    with log.open("w") as stream:
        def consume(line):
            stream.write(line)
            stream.flush()
        result = run_systemd_scope(command, timeout=spec.get("timeout", 600),
                                  memory_max="8G", memory_swap_max="0", env=os.environ | overrides,
                                  unit_prefix="acacia-posets-setup", capture_consumer=consume)
        os.fsync(stream.fileno())
    payload = {"spec": spec, "spec_sha256": hashlib.sha256(args.spec.read_bytes()).hexdigest(),
               "helper_sha256": hashlib.sha256(helper_source).hexdigest(),
               "benchlib_sha256": hashlib.sha256((ROOT / "benchmarking/benchlib.py").read_bytes()).hexdigest(),
               "memory_max": "8G", "memory_swap_max": "0", "environment_overrides": overrides,
               "result": dataclasses.asdict(result), "log": str(log.resolve()),
               "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest()}
    args.record.write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({"record": str(args.record), "exit_code": result.returncode,
                      "timed_out": result.timed_out, "resource_limited": result.resource_limited,
                      "seconds": result.seconds, "scope": result.scope_unit}), flush=True)
    return 0 if not (result.timed_out or result.resource_limited) and result.returncode in spec.get("expected_exit_codes", [0]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
