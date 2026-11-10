#!/usr/bin/env python3
"""Pack exact saved numerical evidence; verify its bytes after extraction."""
import csv
import fcntl
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RAW = ROOT / "work/default-optimization/final-primary-default-rank-v3/raw"
PLAN = HERE / "plan.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    with Path(f"/tmp/acacia-posets-benchmark-{os.getuid()}.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        archive = HERE / "default-native-evidence.tar.gz"
        extracted = HERE / "verified-extraction"
        assert not archive.exists() and not extracted.exists()
        plan = json.loads(PLAN.read_text())
        assert len(plan["members"]) == 2
        assert [a["arm"] for a in plan["members"]] == ["baseline-default", "optimized-default"]
        assert plan["frozen_sha256"] == "548ee0e77b9518a9f96a5f187b8f761966399509607c4fc393fa94a58e1cc5bb"
        payloads = {}
        inventory = {}
        for arm in plan["members"]:
            for kind in ("native", "memory_sidecar"):
                entry = arm[kind]
                source = Path(entry["path"])
                assert source.is_file() and not source.is_symlink()
                assert source.resolve().parent == RAW.resolve()
                data = source.read_bytes()
                assert digest(data) == entry["sha256"]
                name = "raw/" + source.name
                assert name not in payloads
                payloads[name] = data
                inventory[name] = dict(source=str(source), sha256=digest(data), size=len(data),
                                       arm=arm["arm"], kind=kind)
        assert len(payloads) == 4
        checksums = "".join(f"{inventory[n]['sha256']}  {n}\n" for n in sorted(inventory)).encode()
        payloads["SHA256SUMS"] = checksums
        payloads["README.txt"] = (
            "Exact native TSVs and memory sidecars from the completed two-arm, 1524-case\n"
            "Acacia default-rank campaign. Native bytes, float strings and labels unchanged.\n"
            "Baseline-default Posets789bf99; optimized-default Posets30e6685.\n"
            "17-second strict-wall scoring; 8 GiB/no swap. All unsolved cost34s PAR2.\n"
            "Native MEMOUT labels and kernel OOM-kill events are separate observations;\n"
            "the companion table/event audit retains both without relabelling/rescoring.\n"
            "This is a separate campaign from the preceding15-arm backend comparison.\n"
            "\nVerify4 data files with SHA256SUMS. The archive has6 regular members total.\n"
            "Frozen manifest SHA256: 548ee0e77b9518a9f96a5f187b8f761966399509607c4fc393fa94a58e1cc5bb\n"
            "No corpus, operation captures, executable, build tree or unrelated WIP is included.\n"
        ).encode()
        with archive.open("xb") as stream:
            with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as tar:
                    for name, data in sorted(payloads.items()):
                        info = tarfile.TarInfo(name)
                        info.size = len(data)
                        info.mode = 0o644
                        info.mtime = info.uid = info.gid = 0
                        info.uname = info.gname = ""
                        tar.addfile(info, io.BytesIO(data))
        extracted.mkdir()
        with tarfile.open(archive, "r:gz") as tar:
            members = tar.getmembers()
            assert len(members) == 6 and {m.name for m in members} == set(payloads)
            for member in members:
                name = PurePosixPath(member.name)
                assert member.isfile() and not name.is_absolute() and ".." not in name.parts
                data = tar.extractfile(member).read()
                assert data == payloads[member.name]
                destination = extracted / member.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
        counts = {"native": 0, "memory_sidecar": 0}
        for name, entry in inventory.items():
            data = (extracted / name).read_bytes()
            assert digest(data) == entry["sha256"] and len(data) == entry["size"]
            rows = list(csv.DictReader(io.StringIO(data.decode()), delimiter="\t"))
            assert len(rows) == 1524 and len({r["instance"] for r in rows}) == 1524
            assert {r["solver_label"] for r in rows} == {entry["arm"]}
            counts[entry["kind"]] += len(rows)
        assert counts == {"native": 3048, "memory_sidecar": 3048}
        (HERE / "SHA256SUMS").write_bytes(checksums)
        result = dict(status="pass", archive=str(archive), archive_sha256=digest(archive.read_bytes()),
                      archive_bytes=archive.stat().st_size, data_members=4, total_members=6,
                      source_bytes=sum(x["size"] for x in inventory.values()), rows=counts,
                      plan_sha256=digest(PLAN.read_bytes()), script_sha256=digest(Path(__file__).read_bytes()),
                      members=inventory, verification="Fresh extraction, exact bytes/SHA256/size/rows/identities; no source mutation")
        (HERE / "verification.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({k: result[k] for k in ("status", "archive_sha256", "archive_bytes", "source_bytes", "rows")}))


if __name__ == "__main__":
    main()
