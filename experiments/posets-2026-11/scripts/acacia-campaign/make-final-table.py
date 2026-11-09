#!/usr/bin/env python3
"""Derive both predeclared comparators from a completed, unchanged campaign."""

import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path


LABELS = ["anchor-e167", "candidate-default", "kd", "kd-narrow", "trie",
          "trie-compact", "comp", "comp-simd", "block8", "block8-uncached",
          "block16", "lazy-fc", "lazy-search", "sharingtree", "simple-sharingtree"]
DECISIVE = {"REALIZABLE", "UNREALIZABLE"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_binding(binding):
    path = Path(binding["path"])
    require(sha(path) == binding["sha256"], f"Changed source: {path}")
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    by_name = {row["instance"]: row for row in rows}
    require(len(rows) == len(by_name) == 1524, f"Wrong coverage: {path}")
    return by_name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = args.campaign.resolve(strict=True)
    manifest = json.loads((root / "frozen.json").read_text())
    progress = json.loads((root / "progress.json").read_text())
    summary = json.loads((root / "summary.json").read_text())
    require(progress["status"] == "complete", "Campaign is not complete")
    require(summary["usable"] and not summary["conflicts"], "Unresolved conflicts")
    require(summary["frozen_manifest_sha256"] == sha(root / "frozen.json"), "Manifest changed")
    require([arm["label"] for arm in manifest["arms"]] == LABELS, "Wrong arm inventory")
    require(set(summary["series"]) == set(LABELS), "Wrong summary inventory")
    require(manifest["baseline"] == LABELS[0], "Wrong primary comparator")
    require(manifest["cap_s"] == 17 and manifest["memory_max"] == "8G"
            and manifest["memory_swap_max"] == "0", "Wrong resource treatment")
    require(all(arm["flags"] == "" for arm in manifest["arms"]), "Nonempty primary flags")
    solved = {}
    totals = {}
    raw_by_arm = {}
    truth = {}
    for label in LABELS:
        entry = summary["series"][label]
        source = summary["strict_wall_scoring"]["sources"][label]
        for key in ("raw_memory", "scored_memory"):
            require(source[key] is not None and sha(source[key]["path"]) == source[key]["sha256"],
                    f"Missing or changed memory source: {label}/{key}")
        raw = read_binding(source["raw"])
        scored = read_binding(source["scored"])
        require(raw.keys() == scored.keys(), f"Scoring identities differ: {label}")
        if raw_by_arm:
            require(raw.keys() == next(iter(raw_by_arm.values())).keys(), "Arm identities differ")
        require(dict(Counter(row["result"] for row in raw.values())) == entry["native_outcomes"],
                f"Native counts differ: {label}")
        passed = set()
        overrun_count = 0
        total = 0.0
        for name, row in raw.items():
            seconds = float(row["seconds"])
            require(math.isfinite(seconds) and seconds >= 0, f"Invalid time: {label}/{name}")
            verdict = row["result"]
            if verdict in DECISIVE:
                require(truth.get(name, verdict) == verdict, f"Opposite verdicts: {name}")
                truth[name] = verdict
            overrun = verdict in DECISIVE and seconds > 17
            expected_scored = dict(row, result="TIMEOUT") if overrun else row
            require(scored[name] == expected_scored, f"Unexpected scoring change: {label}/{name}")
            overrun_count += overrun
            if verdict in DECISIVE and seconds <= 17:
                passed.add(name)
                total += seconds
            else:
                total += 34
        require(len(passed) == entry["solved"], f"Solved count differs: {label}")
        require(abs(total - entry["par2_total_s"]) <= 0.00001, f"PAR2 differs: {label}")
        require(overrun_count == len(entry["wall_cap_overruns"]), f"Overrun count differs: {label}")
        solved[label], totals[label], raw_by_arm[label] = passed, entry["par2_total_s"], raw
    rows, comparisons = [], {}
    for label in LABELS:
        entry = summary["series"][label]
        pairs = {}
        for comparator in ["anchor-e167", "candidate-default"]:
            pairs[comparator] = {
                "gains": sorted(solved[label] - solved[comparator]),
                "losses": sorted(solved[comparator] - solved[label]),
                "par2_delta_s": round(totals[label] - totals[comparator], 6),
            }
        if label != "anchor-e167":
            existing = summary["candidate_vs_baseline"][label]
            require(set(existing["gains"]) == set(pairs["anchor-e167"]["gains"])
                    and set(existing["losses"]) == set(pairs["anchor-e167"]["losses"])
                    and abs(existing["par2_delta_s"] - pairs["anchor-e167"]["par2_delta_s"]) < 0.00001,
                    f"Anchor comparator differs: {label}")
        scope = entry["peak_memory"]["scope"]
        cpu = entry["cpu"]
        cpu_values = [float(row["cpu_seconds"]) for row in raw_by_arm[label].values()
                      if row["cpu_seconds"]]
        require(all(math.isfinite(value) and value >= 0 for value in cpu_values),
                f"Invalid CPU observation: {label}")
        require(len(cpu_values) == cpu["observed_rows"], f"CPU coverage differs: {label}")
        if len(cpu_values) == 1524:
            require(cpu["total_seconds"] is not None
                    and abs(sum(cpu_values) - cpu["total_seconds"]) < 0.00001,
                    f"CPU aggregate differs: {label}")
        require(cpu["observed_rows"] == 1524 or cpu["total_seconds"] is None,
                f"Partial CPU aggregate: {label}")
        require(scope["numerator"] == 1524 or (scope["max_bytes"] is None and scope["median_bytes"] is None),
                f"Partial memory aggregate: {label}")
        rows.append({
            "arm": label, "solved": len(solved[label]), "instances": 1524,
            "par2_total_s": totals[label],
            "anchor_gains": len(pairs["anchor-e167"]["gains"]),
            "anchor_losses": len(pairs["anchor-e167"]["losses"]),
            "anchor_par2_delta_s": pairs["anchor-e167"]["par2_delta_s"],
            "candidate_default_gains": len(pairs["candidate-default"]["gains"]),
            "candidate_default_losses": len(pairs["candidate-default"]["losses"]),
            "candidate_default_par2_delta_s": pairs["candidate-default"]["par2_delta_s"],
            "cpu_observed_rows": cpu["observed_rows"], "cpu_total_s": cpu["total_seconds"],
            "scope_peak_observed_rows": scope["numerator"],
            "scope_peak_max_bytes": scope["max_bytes"], "scope_peak_median_bytes": scope["median_bytes"],
            "wall_cap_overruns": len(entry["wall_cap_overruns"]),
            "native_outcomes": entry["native_outcomes"], "failure_subtypes": entry["failure_subtypes"],
        })
        comparisons[label] = pairs
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    result = {
        "scope": "One complete 15-arm campaign; strict measured-wall <=17 scoring; all unsolved cost34s. No significance claim.",
        "summary": {"path": str(root / "summary.json"), "sha256": sha(root / "summary.json")},
        "manifest": {"path": str(root / "frozen.json"), "sha256": sha(root / "frozen.json")},
        "derivation_script_sha256": sha(__file__), "rows": rows, "comparisons": comparisons,
    }
    (out / "solver-comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    with (out / "solver-comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({k: json.dumps(v, sort_keys=True) if isinstance(v, dict) else v for k, v in row.items()})
    print(f"Verified 22860 raw/scored rows and both comparators; wrote {out}")


if __name__ == "__main__":
    main()
