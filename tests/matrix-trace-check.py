#!/usr/bin/env python3
"""Exercise recorded-operation replay and reject incomplete/malformed evidence."""

import json
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(binary, path, backend="vector_backed", vector="simd_vector_backed", ok=True, source=False):
    command = [binary, "--trace", str(path), "--check=" + ("trace-source" if source else "trace"),
               backend, vector + "<test_value_type>"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10, preexec_fn=no_core)
    # Malformed inputs throw (SIGABRT) or return 1. Tool/ISA/memory failures,
    # including Valgrind's error-exitcode=18, are never successful rejections.
    allowed = (0,) if ok else (1, -signal.SIGABRT)
    assert result.returncode in allowed, (command, result.returncode, result.stdout, result.stderr)
    if ok and not source:
        expected = f"[--] running tests for posets::downsets::{backend}<posets::vectors::{vector}<test_value_type>>"
        assert [line for line in result.stdout.splitlines() if line.startswith("[--]")] == [expected]
        assert not any(line.startswith("  - ") for line in result.stdout.splitlines()), "Correctness mode emitted timings"


def maxima(values):
    values = set(values)
    return sorted(value for value in values if not any(
        value != other and all(a <= b for a, b in zip(value, other)) for other in values))


def cpre(directory, numeric, before):
    dimension = len(before[0])
    meta = directory / "meta.tsv"
    meta.write_text("schema_version\tstates\tbool_threshold\tinit_state\tworker\tinstance\n"
                    f"3\t{dimension}\t{numeric}\t0\tfixture\tfixture\n")
    # Two output choices, signed bottom constraints, repeated source endpoints
    # and an absent transition row. Zero numeric prefix is a valid Boolean set.
    actions = [[(i, i, i % 2) for i in range(dimension)],
               [(i, (i + 1) % dimension, 1) for i in range(dimension - 1)]]
    preimages = []
    for action in actions:
        for value in before:
            image = [1] * numeric + [0] * (dimension - numeric)
            for destination, source, increment in action:
                image[source] = min(image[source], max(-1, value[destination] - increment))
            preimages.append(tuple(image))
    after = maxima(tuple(min(a, b) for a, b in zip(value, image))
                   for value in before for image in preimages)
    def rank(value):
        return "\t".join(map(str, value)) + "\n"
    text = f"# schema_version=2 loop=1 k=2 actions=2 before={len(before)} input=1\n[before]\n"
    text += "".join(map(rank, before)) + "[actions]\n"
    for i, action in enumerate(actions):
        text += f"action\t{i}\n" + "".join(rank(edge) for edge in action)
    text += f"[after]\t{len(after)}\n" + "".join(map(rank, after))
    path = directory / "cpre-1.tsv"
    path.write_text(text)
    return path, text, before


def contains(directory, query, before, expected):
    text = (f"# schema_version=1 k=2 loop=1 maxima={len(before)} expected={int(expected)} "
            f"bin=0 ordinal=2 hits=1 origin=picker\n[query]\n")
    text += "\t".join(map(str, query)) + "\n[generators]\n"
    text += "".join("\t".join(map(str, value)) + "\n" for value in before) + "[end]\n"
    path = directory / "contains-0.tsv"
    path.write_text(text)
    return path, text


def main():
    binary = sys.argv[1]
    backends = json.loads(subprocess.check_output([binary, "--metadata"], text=True))["backends"]
    with tempfile.TemporaryDirectory(prefix="posets-trace-") as temporary:
        root = Path(temporary)
        for dimension, numeric in ((2, 0), (17, 8), (65, 64)):
            directory = root / str(dimension)
            directory.mkdir()
            before = maxima((tuple(0 if i % 2 == parity else -1 for i in range(dimension)) for parity in (0, 1)))
            path, original, before = cpre(directory, numeric, before)
            run(binary, path, source=True)
            for backend in backends:
                for vector in ("vector_backed", "simd_vector_backed"):
                    run(binary, path, backend, vector)
            if dimension == 2:
                # A valid zero-output action list keeps production's bottom
                # accumulator. This is not an empty region or zero dimension.
                zero = "# schema_version=2 loop=1 k=2 actions=0 before=1 input=1\n[before]\n0\t0\n[actions]\n[after]\t1\n-1\t-1\n"
                path.write_text(zero)
                run(binary, path, source=True)
                for backend in backends:
                    for vector in ("vector_backed", "simd_vector_backed"):
                        run(binary, path, backend, vector)
                path.write_text(original)
            for query, answer in ((before[0], True), (tuple(2 for _ in range(dimension)), False)):
                member, text = contains(directory, query, before, answer)
                for backend in backends:
                    run(binary, member, backend)
                for bad in (text[:-1], text.replace("hits=1", "hits=3"),
                            text.replace("bin=0", "bin=1"), text.replace("expected=" + str(int(answer)), "expected=" + str(int(not answer))),
                            text.replace("origin=picker", "origin=unknown"), text.replace("[end]\n", "")):
                    member.write_text(bad)
                    run(binary, member, ok=False)
                member.write_text(text)
            for bad in (original[:-1], original.replace("[after]", "[missing]"),
                        original.replace("actions=2", "actions=3"), original.replace("action\t1", "action\t3"),
                        original.replace("k=2", "k=128"), original.replace("[before]\n", "[before]\n99999999999999999999\n")):
                path.write_text(bad)
                run(binary, path, ok=False)
            path.write_text(original)
            meta = directory / "meta.tsv"
            original_meta = meta.read_text()
            meta.write_text(original_meta.replace(f"3\t{dimension}\t", "3\t0\t"))
            run(binary, path, ok=False)
            meta.write_text(original_meta)
            copy = directory / "saved.tsv"
            path.rename(copy)
            path.symlink_to(copy)
            run(binary, path, ok=False)
        print("validated exact backend dispatch, all-Boolean/padded replay, actual queries and malformed evidence")


if __name__ == "__main__":
    main()
