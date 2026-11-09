#!/usr/bin/env python3
"""Materialize the verified grown seeds with Acacia's existing byte decoder."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent / "acacia-posets-eval"
PLAN = Path(__file__).resolve().with_name("pipeline-prep.json")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    plan = json.loads(PLAN.read_text())
    decoder = ROOT / "build-posets/tests/tlsf-fuzz-replay"
    evidence = ROOT / "_deps/evidence-fuzz-20261105"
    output = ROOT / "_deps/decoded-fuzz-20261105"
    assert not output.exists(), "Choose a fresh output directory; do not overwrite evidence"
    output.mkdir()
    records = []
    for kind, corpus in plan["corpora"].items():
        assert len(corpus["seeds"]) == corpus["count"]
        for seed in corpus["seeds"]:
            source = evidence / seed["relative_path"]
            assert sha(source) == seed["sha256"], f"Changed frozen seed: {source}"
            command = [str(decoder), "decode", kind, str(source)]
            original = subprocess.run(command, capture_output=True, text=True, check=True, timeout=10).stdout
            variants = [("original", original, command)]
            if "SEMANTICS: Finite," in original:
                infinite_command = [str(decoder), "decode-infinite", kind, str(source)]
                text = subprocess.run(infinite_command, capture_output=True, text=True, check=True, timeout=10).stdout
                assert text != original and "SEMANTICS: Finite," not in text
                variants.append(("infinite-companion", text, infinite_command))
            for variant, text, invocation in variants:
                identifier = f"{kind}-{seed['sha256']}-{variant}"
                target = output / (identifier + ".tlsf")
                target.write_text(text)
                records.append({"id": identifier, "kind": kind, "variant": variant,
                                "seed": str(source), "seed_sha256": seed["sha256"],
                                "tlsf": str(target), "tlsf_sha256": sha(target), "command": invocation})
    assert sum(row["variant"] == "original" for row in records) == 3390
    manifest = {"decoder": str(decoder), "decoder_sha256": sha(decoder),
                "plan_sha256": sha(PLAN), "source_decode_sha256": sha(ROOT / "subprojects/tlsf-tools/test/fuzz/decode.c"),
                "seed_count": 3390, "input_count": len(records),
                "unique_tlsf_count": len({row["tlsf_sha256"] for row in records}),
                "note": "Only byte-to-text decoding ran. Finite originals and infinite companions are distinct problems; no solver/capture/performance claim.",
                "inputs": records}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({key: value for key, value in manifest.items() if key != "inputs"}), flush=True)


if __name__ == "__main__":
    main()
