#!/usr/bin/env python3
"""Synthetic checks only: no solver, systemd scope, test build, or real corpus write."""

from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

sys.dont_write_bytecode = True
import campaign as C


def main():
    repo = C.HERE.parent / "acacia-posets-eval"
    _, coverage, reporter = C.load_support(repo)
    with tempfile.TemporaryDirectory(prefix="selfcheck-", dir=C.HERE) as directory:
        root = Path(directory)
        corpus = root / "corpus"
        corpus.mkdir()
        names = [f"i{index:04d}.ltl" for index in range(C.TOTAL)]
        listing, mapping, exceptions = root / "all.list", root / "tlsf-map.tsv", root / "exceptions.tsv"
        listing.write_text("\n".join(names) + "\n")
        mapping.write_text("instance\ttlsf\n" + "".join(f"{name}\t{name[:-4]}.tlsf\n" for name in names))
        exceptions.write_text("instance\tannotated_status\tcorrected_status\tevidence\n")
        for name in names:
            (corpus / f"{name[:-4]}.tlsf").write_text("// STATUS : unknown\n")
        arms_path = root / "arms.json"
        arms_path.write_text(json.dumps({"baseline": "shipping", "arms": [
            {"label": label, "binary": sys.executable, "acacia_sha": "test-source",
             "source_pins": {"acacia": "test-source", "posets": label},
             "preset": "otf_sparse_formula_parity"} for label in ("shipping", "candidate")]}))
        args = SimpleNamespace(repo=repo.resolve(), arms=arms_path, list=listing, tlsf_map=mapping,
                               tlsf_corpus=corpus, status_exceptions=exceptions, batch_size=32,
                               seed=260617, allowed_cpus=None, cpu_quota=None)
        manifest = C.freeze(args, coverage)
        preview = io.StringIO()
        with redirect_stdout(preview):
            assert C.main(["--repo", str(repo), "--arms", str(arms_path), "--output", str(root / "dry-run"),
                           "--list", str(listing), "--tlsf-map", str(mapping), "--tlsf-corpus", str(corpus),
                           "--status-exceptions", str(exceptions), "--dry-run"]) == 0
        assert json.loads(preview.getvalue())["instances_per_arm"] == C.TOTAL
        assert not (root / "dry-run").exists()
        assert manifest["schedule"] == C.schedule(names, ["shipping", "candidate"], 32, 260617)
        assert len(manifest["schedule"]) == 48
        assert sorted(name for batch in manifest["schedule"] for name in batch["instances"]) == names
        assert {sum(batch["arms"][0] == label for batch in manifest["schedule"])
                for label in ("shipping", "candidate")} == {24}
        output = root / "results"
        (output / "raw").mkdir(parents=True)
        C.atomic_json(output / "frozen.json", manifest)
        C.atomic_json(output / "host.json", {"synthetic": True})
        for arm in manifest["arms"]:
            rows = []
            for index, name in enumerate(names):
                result = ["TIMEOUT", "MEMOUT", "CRASH", "ERROR", "UNKNOWN"][index] if index < 5 else "REALIZABLE"
                if arm["label"] == "candidate" and index == 0:
                    result = "REALIZABLE"
                row = dict.fromkeys(coverage.OUTPUT_COLUMNS + coverage.MEMORY_COLUMNS, "")
                row.update(solver_label=arm["label"], instance=name, tlsf_file=f"{name[:-4]}.tlsf",
                           cap_s="17", result=result, seconds="1" if result == "REALIZABLE" else "17.4",
                           exit_code="0" if result == "REALIZABLE" else "124", timed_out=str(result == "TIMEOUT").lower(),
                           resource_reason="timeout" if result == "TIMEOUT" else "memory" if result == "MEMOUT" else "",
                           expectation_source="none", run_index=str(index), acacia_sha=arm["acacia_sha"],
                           binary_sha256=arm["binary_sha256"], preset=arm["preset"], flags="",
                           cpu_seconds="0.5", scope_memory_peak_bytes="1048576", max_process_rss_bytes="524288",
                           memory_max="8G", memory_swap_max="0", collect_rusage="true",
                           scope_memory_peak_source="cgroup-v2/memory.peak", memory_cgroup=f"/fake/{index}",
                           max_process_rss_source="Linux wait4.ru_maxrss (maximum reaped process; not scope peak)")
                if arm["label"] == "candidate" and index == 1:
                    row.update(scope_memory_peak_missing_reason="synthetic missing peak")
                if index == 6:
                    row["seconds"] = "17.05"
                rows.append(row)
            path = C.raw_path(output, arm)
            coverage.atomic_write_tsv(path, coverage.OUTPUT_COLUMNS, rows)
            coverage.write_memory_sidecar(path, rows)
            coverage.atomic_write_tsv(path.with_name(f"{arm['label']}-conflicts.tsv"), coverage.CONFLICT_COLUMNS, [])
        raw_hashes = {str(path): C.digest(path) for path in (output / "raw").iterdir()}
        with redirect_stdout(io.StringIO()):
            assert C.summarize(manifest, output, coverage, reporter) == 0
        report = json.loads((output / "summary.json").read_text())
        shipping = report["series"]["shipping"]
        candidate = report["series"]["candidate"]
        assert shipping["solved"] == C.TOTAL - 6
        assert shipping["par2_total_s"] == C.TOTAL - 6 + 6 * 34
        assert all(shipping["failure_subtypes"][key] == 1 for key in ("MEMOUT", "CRASH", "ERROR", "UNKNOWN"))
        assert shipping["cpu"]["total_seconds"] == C.TOTAL / 2
        assert shipping["peak_memory"]["max_mib"] == 1
        assert candidate["peak_memory"]["max_mib"] == "missing"
        assert candidate["peak_memory"]["scope"]["max_bytes"] is None
        assert candidate["peak_memory"]["scope"]["numerator"] == C.TOTAL - 1
        assert len(shipping["wall_cap_overruns"]) == 1
        assert shipping["wall_cap_overruns"][0]["native_seconds"] == "17.05"
        assert shipping["native_outcomes"]["TIMEOUT"] == 1
        assert shipping["failure_subtypes"]["TIMEOUT"] == 2
        scored_rows = coverage.load_output(output / "strict-wall-scoring/shipping.tsv")
        assert scored_rows[6]["result"] == "TIMEOUT" and scored_rows[6]["seconds"] == "17.05"
        assert coverage.load_output(C.raw_path(output, manifest["arms"][0]))[6]["result"] == "REALIZABLE"
        assert report["candidate_vs_baseline"]["candidate"] == {"gains": [names[0]], "losses": [], "par2_delta_s": -33}
        assert all(C.digest(path) == expected for path, expected in raw_hashes.items())
        candidate_arm = manifest["arms"][1]
        path = C.raw_path(output, candidate_arm)
        rows = coverage.load_output(path)
        rows[6].update(result="UNREALIZABLE", exit_code="1")
        coverage.atomic_write_tsv(path, coverage.OUTPUT_COLUMNS, rows)
        coverage.write_memory_sidecar(path, rows)
        with redirect_stdout(io.StringIO()):
            assert C.summarize(manifest, output, coverage, reporter) == 3
        report = json.loads((output / "summary.json").read_text())
        assert not report["usable"] and any(conflict["against"] == "each other" for conflict in report["conflicts"])
        assert C.cross_arm_conflicts({"shipping": coverage.load_output(C.raw_path(output, manifest["arms"][0])),
                                      "candidate": rows}) == [{"instance": names[6], "verdicts":
                                                                {"shipping": "REALIZABLE", "candidate": "UNREALIZABLE"}}]
        bad = copy.deepcopy(rows)
        bad[1]["instance"] = bad[0]["instance"]
        coverage.atomic_write_tsv(path, coverage.OUTPUT_COLUMNS, bad)
        try:
            C.read_rows(coverage, path, candidate_arm, set(names), manifest)
        except ValueError as error:
            assert "duplicate" in str(error)
        else:
            raise AssertionError("Duplicate instance accepted")
        cmd = C.command(manifest, candidate_arm, root / "batch.list", output)
        assert "--flags=" in cmd and "--caps" in cmd and cmd[cmd.index("--caps") + 1] == "17"
        assert cmd[cmd.index("--conflict-policy") + 1] == "stop"
        with patch.dict(C.os.environ, {"ACACIA_OUTER_DEADLINE_MONOTONIC": "123"}):
            try:
                C.freeze(args, coverage)
            except ValueError as error:
                assert "Unset inherited" in str(error)
            else:
                raise AssertionError("Inherited stale deadline accepted")
        with patch.object(C.subprocess, "run", return_value=SimpleNamespace(returncode=0, stderr="", stdout=
                          "acacia-other.scope loaded active running unrelated\nacacia-failed.scope loaded failed failed old\n")):
            assert C.running_scopes() == ["acacia-other.scope"]
    print("PASS: seeded balanced batches, native command/dry-run, full1524 PAR2/failures, strict memory/CPU coverage,")
    print("      native17.05s scoring adapter, overrun truth conflicts, immutable raw evidence, duplicates/scopes/env guards.")


if __name__ == "__main__":
    main()
