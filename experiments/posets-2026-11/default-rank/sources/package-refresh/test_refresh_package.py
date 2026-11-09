"""Small synthetic safety fixtures; execute only after the owner's lane release."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("refresh-package.py")
spec = importlib.util.spec_from_file_location("refresh_package", SCRIPT)
refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(refresh)


def binding(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class RefreshSafety(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.package = self.root / "work/package"
        self.package.mkdir(parents=True)
        self.plan_path = self.root / "work/allowlist.json"
        self.backup = self.root / "work/before"
        self.old = b"preserved historical bytes\n"
        (self.package / "old.txt").write_bytes(self.old)
        self.old_entry = {"path": "old.txt", **binding(self.old),
                          "origin": str(self.root / "work/no-longer-present.txt")}
        for name in ("README.md", "EVIDENCE.md", "RECREATE.md"):
            (self.package / name).write_text("old documentation\n")
        for name in ("references.json", "STATIC-CHECKS.json"):
            (self.package / name).write_text("{}\n")
        (self.package / "copy-origins.json").write_text(json.dumps({"files": [self.old_entry]}))
        entries = [{"path": path.name, **binding(path.read_bytes())}
                   for path in sorted(self.package.iterdir())]
        (self.package / "inventory.json").write_text(json.dumps({"files": entries}))
        (self.package / "SHA256SUMS").write_text("old checksum snapshot\n")
        source = self.root / "work/source.json"
        source.write_text('{"completed": true}\n')
        documents = []
        for name in ("README.md", "EVIDENCE.md", "RECREATE.md", "references.json"):
            template = self.root / "work" / name
            template.write_text("{}\n" if name.endswith(".json") else "new documentation\n")
            documents.append({"path": name, "template": str(template),
                              **binding(template.read_bytes())})
        self.plan = {"package": str(self.package), "ready_for_apply": True, "pending": [],
            "preserve_existing_exact_copies": 1,
            "existing_package_metadata": {path.name: binding(path.read_bytes())
                for path in self.package.iterdir() if path.name != "old.txt"},
            "maximum_new_file_bytes_without_new_review": 500000,
            "files": [{"destination": "new/source.json", "origin": str(source),
                "origin_relative_to_task": "work/source.json", **binding(source.read_bytes()),
                "kind": "synthetic fixture", "preservation": "exact bytes"}],
            "document_replacements": documents}
        self.before = self.snapshot()

    def snapshot(self):
        return {str(path.relative_to(self.package)): path.read_bytes()
                for path in self.package.rglob("*") if path.is_file()}

    def invoke(self):
        raw = (json.dumps(self.plan) + "\n").encode()
        self.plan_path.write_bytes(raw)
        argv = [str(SCRIPT), "--allowlist", str(self.plan_path), "--allowlist-sha256",
                binding(raw)["sha256"], "--backup", str(self.backup), "--apply"]
        with patch.object(refresh, "ROOT", self.root), patch.object(refresh, "PACKAGE", self.package), \
                patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()):
            refresh.main()

    def test_pending_refuses_before_any_mutation(self):
        self.plan["pending"] = [{"status": "PENDING"}]
        with self.assertRaisesRegex(ValueError, "PENDING"):
            self.invoke()
        self.assertEqual(self.before, self.snapshot())
        self.assertFalse(self.backup.exists())

    def test_bad_hash_refuses_before_any_mutation(self):
        self.plan["files"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "bytes/hash mismatch"):
            self.invoke()
        self.assertEqual(self.before, self.snapshot())
        self.assertFalse(self.backup.exists())

    def test_large_addition_requires_its_own_exact_hash_binding(self):
        self.plan["maximum_new_file_bytes_without_new_review"] = 4
        with self.assertRaisesRegex(ValueError, "separate exact-hash review"):
            self.invoke()
        self.assertEqual(self.before, self.snapshot())
        self.assertFalse(self.backup.exists())
        entry = self.plan["files"][0]
        self.plan["reviewed_large_file_bindings"] = {
            entry["destination"]: {key: entry[key] for key in ("sha256", "bytes")}}
        self.invoke()

    def test_new_archive_is_not_implicitly_approved_by_the_file_allowlist(self):
        self.plan["files"][0]["destination"] = "new/synthetic.tar.gz"
        with self.assertRaisesRegex(ValueError, "separate verification/approval"):
            self.invoke()
        self.assertEqual(self.before, self.snapshot())
        self.assertFalse(self.backup.exists())

    def test_refresh_preserves_copies_backs_up_metadata_and_checksums_all_files(self):
        self.invoke()
        self.assertEqual(self.old, (self.package / "old.txt").read_bytes())
        for name in self.plan["existing_package_metadata"]:
            self.assertEqual(self.before[name], (self.backup / name).read_bytes())
        origins = json.loads((self.package / "copy-origins.json").read_text())
        self.assertEqual(self.old_entry, origins["files"][0])
        for line in (self.package / "SHA256SUMS").read_text().splitlines():
            sha, name = line.split("  ", 1)
            self.assertEqual(sha, binding((self.package / name).read_bytes())["sha256"])
        after = self.snapshot()
        with self.assertRaisesRegex(ValueError, "backup must be a fresh"):
            self.invoke()
        self.assertEqual(after, self.snapshot())


if __name__ == "__main__":
    unittest.main()
