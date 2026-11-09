"""Apply an approved immutable evidence allowlist; never launch workloads.

This draft is deliberately blocked by ready_for_apply=false and pending entries.
Version the allowlist/documents after the outstanding outcomes are reviewed.
"""

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "work/posets/experiments/posets-2026-11"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def task_path(raw):
    path = Path(raw)
    require(path.is_absolute(), f"absolute task path required: {path}")
    require(path.resolve() == path, f"symlink or noncanonical path: {path}")
    require(any(path.is_relative_to(ROOT / part) for part in ("work", "outputs")),
            f"outside task-owned work/outputs: {path}")
    return path


def relative_path(raw):
    path = Path(raw)
    require(not path.is_absolute() and path.parts and ".." not in path.parts,
            f"unsafe package-relative path: {raw}")
    require(str(path) == raw, f"noncanonical package-relative path: {raw}")
    return path


def bound_bytes(path, binding):
    data = task_path(str(path)).read_bytes()
    require(len(data) == binding["bytes"] and digest(data) == binding["sha256"],
            f"bytes/hash mismatch: {path}")
    require(not data.startswith(b"\x7fELF"), f"ELF excluded: {path}")
    if path.suffix == ".json":
        json.loads(data)
    return data


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allowlist", required=True, type=Path)
    parser.add_argument("--allowlist-sha256", required=True)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    require(args.apply, "no mutation without explicit --apply")
    raw = task_path(str(args.allowlist)).read_bytes()
    require(digest(raw) == args.allowlist_sha256, "allowlist hash mismatch")
    plan = json.loads(raw)
    require(plan.get("ready_for_apply") is True and not plan["pending"],
            "PENDING draft: complete and review a new allowlist/documents first")
    require(task_path(plan["package"]) == PACKAGE, "wrong package")
    backup = task_path(str(args.backup))
    require(not backup.exists() and not backup.is_relative_to(PACKAGE),
            "backup must be a fresh directory outside the package")

    # Validate every input before creating backups or changing the package.
    metadata = {name: bound_bytes(PACKAGE / name, binding)
                for name, binding in plan["existing_package_metadata"].items()}
    origins = json.loads(metadata["copy-origins.json"])
    previous = origins["files"]
    require(len(previous) == plan["preserve_existing_exact_copies"],
            "unexpected historical-copy count")
    for entry in previous:
        bound_bytes(PACKAGE / relative_path(entry["path"]), entry)
    inventory = json.loads(metadata["inventory.json"])
    for entry in inventory["files"]:
        bound_bytes(PACKAGE / relative_path(entry["path"]), entry)
    expected = {entry["path"] for entry in inventory["files"]}
    expected.update(("inventory.json", "SHA256SUMS"))
    actual = {str(path.relative_to(PACKAGE)) for path in PACKAGE.rglob("*")
              if path.is_file()}
    require(actual == expected, "unexpected or missing existing package files")
    for path in PACKAGE.rglob("*"):
        require(not path.is_symlink(), f"symlink excluded: {path}")

    additions = []
    seen = set(expected)
    for entry in plan["files"]:
        rel = relative_path(entry["destination"])
        require(str(rel) not in seen, f"duplicate/existing destination: {rel}")
        seen.add(str(rel))
        if entry["bytes"] > plan["maximum_new_file_bytes_without_new_review"]:
            approved = plan.get("reviewed_large_file_bindings", {}).get(str(rel), {})
            require(all(approved.get(key) == entry[key] for key in ("sha256", "bytes")),
                    f"new large artifact needs separate exact-hash review: {rel}")
        if str(rel).endswith((".tar.gz", ".tar", ".zip", ".7z")):
            approved = plan.get("approved_numerical_archive_bindings", {}).get(str(rel), {})
            require(all(approved.get(key) == entry[key] for key in ("sha256", "bytes")),
                    f"new numerical archive needs separate verification/approval: {rel}")
        source = task_path(entry["origin"])
        require(str(source.relative_to(ROOT)) == entry["origin_relative_to_task"],
                f"origin-relative binding mismatch: {source}")
        additions.append((entry, bound_bytes(source, entry)))
    documents = []
    document_names = set()
    for entry in plan["document_replacements"]:
        rel = relative_path(entry["path"])
        require(str(rel) in metadata, f"unbacked document replacement: {rel}")
        require(str(rel) not in document_names, f"duplicate document: {rel}")
        document_names.add(str(rel))
        documents.append((rel, bound_bytes(Path(entry["template"]), entry)))
    require(document_names == {"README.md", "EVIDENCE.md", "RECREATE.md", "references.json"},
            "only the four reviewed documentation replacements are supported")

    backup.mkdir(parents=True, exist_ok=False)
    for name, data in metadata.items():
        (backup / name).write_bytes(data)
    write_json(backup / "refresh-input.json", {
        "allowlist": str(args.allowlist), "sha256": digest(raw),
        "script": str(Path(__file__).resolve()),
        "script_sha256": digest(Path(__file__).read_bytes()),
        "scope": "backup of small derived metadata only; historical copies remain in place"})

    for entry, data in additions:
        target = PACKAGE / entry["destination"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
        require(target.read_bytes() == bound_bytes(Path(entry["origin"]), entry),
                f"new exact copy mismatch: {target}")
        origins["files"].append({"path": entry["destination"],
            **{key: entry[key] for key in ("sha256", "bytes", "origin",
               "origin_relative_to_task", "kind", "preservation")}})
    for rel, data in documents:
        (PACKAGE / rel).write_bytes(data)
    write_json(PACKAGE / "copy-origins.json", origins)
    for entry in previous[:plan["preserve_existing_exact_copies"]]:
        bound_bytes(PACKAGE / entry["path"], entry)
    write_json(PACKAGE / "STATIC-CHECKS.json", {
        "scope": "Static exact-byte/JSON/inventory refresh; no workload/script reruns",
        "prior_snapshot": str(backup),
        "historical_exact_copies_preserved": plan["preserve_existing_exact_copies"],
        "new_exact_copies_verified_against_origins": len(additions),
        "historical_origin_files_reread": False,
        "historical_status_fields_preserved": True,
        "all_packaged_JSON_parsed": True,
        "new_allowlist": str(args.allowlist), "new_allowlist_sha256": digest(raw),
        "archive_policy": "Existing approved phase-one archive preserved; only separately approved numerical archive copies; no archive creation/extraction",
        "new_archive_copies": [entry["destination"] for entry, _ in additions
                               if entry["destination"].endswith((".tar.gz", ".tar", ".zip", ".7z"))],
        "runtime_revalidation": "Not performed by package refresh"})
    origin_paths = {entry["path"] for entry in origins["files"]}
    files = []
    for path in sorted(PACKAGE.rglob("*")):
        require(not path.is_symlink(), f"symlink excluded: {path}")
        if not path.is_file() or path.name in {"inventory.json", "SHA256SUMS"}:
            continue
        data = path.read_bytes()
        require(not data.startswith(b"\x7fELF"), f"ELF excluded: {path}")
        if path.suffix == ".json":
            json.loads(data)
        rel = str(path.relative_to(PACKAGE))
        files.append({"path": rel, "sha256": digest(data), "bytes": len(data),
            "kind": "exact historical copy" if rel in origin_paths
                    else "derived package documentation/index"})
    inventory.update(scope="Completed phase-one and separately reviewed default-rank evidence",
                     files=files, file_count_excluding_inventory_and_checksums=len(files))
    write_json(PACKAGE / "inventory.json", inventory)
    paths = sorted(path for path in PACKAGE.rglob("*")
                   if path.is_file() and path.name != "SHA256SUMS")
    (PACKAGE / "SHA256SUMS").write_text("".join(
        f"{digest(path.read_bytes())}  {path.relative_to(PACKAGE)}\n" for path in paths))
    print(json.dumps({"historical_exact_copies_preserved": plan["preserve_existing_exact_copies"],
                      "new_exact_copies": len(additions), "backup": str(backup)}))


if __name__ == "__main__":
    main()
