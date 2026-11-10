"""Offline fixture: every subprocess observation is synthetic."""

import copy
import importlib.util
import json
from pathlib import Path
import signal
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("repeat", HERE / "repeat.py")
repeat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repeat)


class Coverage:
    DECISIVE_RESULTS = {"REALIZABLE", "UNREALIZABLE"}
    TOOL_EXIT_CODES = {"acacia": {"REALIZABLE": 0, "UNREALIZABLE": 1}}

    @staticmethod
    def load_output(path):
        return [json.loads(path.read_text())]

    @staticmethod
    def read_tlsf_map(_path):
        return {c["instance"]: c["instance"].removesuffix(".ltl") + ".tlsf" for c in INVENTORY["cells"]}

    @staticmethod
    def load_conflicts(_path):
        return []


INVENTORY = json.loads((HERE / "inventory.json").read_text())


class Sampler:
    def __init__(self, *_args, **_kwargs):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        pass

    def set_leg(self, *_args):
        pass


def plan_fixture():
    return {"inventory": repeat.binding(HERE / "inventory.json"), "repo": "/synthetic-not-executed",
            "cap_s": 17, "memory_max": "8G", "memory_swap_max": "0", "allowed_cpus": None,
            "cpu_quota": None, "collect_rusage": True, "seed": 260617, "environment": {},
            "inputs": {"tlsf_map": {"path": "/synthetic-map"}, "status_exceptions": {"path": "/synthetic-status"}},
            "tlsf_corpus": "/synthetic-corpus", "interpreter": repeat.binding(sys.executable), "bindings": [],
            "arms": {sha: e["arm"] for sha, e in INVENTORY["arms_by_binary_sha256"].items()},
            "pairs": INVENTORY["pairs"],
            "cells": [{k: c[k] for k in ("cell_id", "pair_id", "instance", "selection_reasons")} for c in INVENTORY["cells"]],
            "schedule": repeat.schedule(INVENTORY["cells"], 260617)}


class Child:
    calls = 0
    opposite = False

    def __init__(self, argv, **_kwargs):
        type(self).calls += 1
        def arg(name):
            return argv[argv.index(name) + 1]
        raw = Path(arg("--output"))
        name = Path(arg("--list")).read_text().strip()
        arm = next(e["arm"] for e in INVENTORY["arms_by_binary_sha256"].values() if e["arm"]["binary"] == arg("--bin"))
        before = raw.parents[1].name == "before"
        verdict = "UNREALIZABLE" if self.opposite and not before else "REALIZABLE"
        row = {"instance": name, "tlsf_file": name.removesuffix(".ltl") + ".tlsf",
               "solver_label": arm["label"], "binary_sha256": arm["binary_sha256"], "acacia_sha": arm["acacia_sha"],
               "result": verdict, "exit_code": "1" if verdict == "UNREALIZABLE" else "0",
               "seconds": "17.05" if self.opposite and before else "1", "cap_s": "17", "flags": "",
               "preset": "otf_sparse_formula_parity", "memory_max": "8G", "memory_swap_max": "0",
               "allowed_cpus": "", "cpu_quota": "", "collect_rusage": "true", "scope_unit": "synthetic.scope",
               "cpu_seconds": "2", "max_process_rss_bytes": "10", "scope_memory_peak_bytes": "20",
               "scope_memory_events": '{"oom_kill":0}'}
        raw.write_text(json.dumps(row))
        Path(str(raw) + ".memory.tsv").write_text("synthetic-sidecar-only\n")

    def wait(self, **_kwargs):
        return 0


class RepeatTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.plan = plan_fixture()
        self.plan["host_lock"] = str(self.root / "host.lock")
        self.plan_path = self.root / "plan.json"
        self.plan_path.write_text(json.dumps(self.plan))
        self.campaign = repeat.campaign_module()
        self.stack = []
        for target, value in [("load_support", lambda _p: (SimpleNamespace(HostSampler=Sampler), Coverage(), None)), ("running_scopes", lambda: [])]:
            p = patch.object(self.campaign, target, value)
            p.start()
            self.stack.append(p)
        for p in [patch.object(repeat, "campaign_module", lambda: self.campaign),
                  patch.object(repeat, "environment", lambda _p: {}),
                  patch.object(repeat.subprocess, "Popen", Child)]:
            p.start()
            self.stack.append(p)
        Child.calls = 0
        Child.opposite = False

    def tearDown(self):
        for p in reversed(self.stack):
            p.stop()
        self.temporary.cleanup()

    def test_schedule_and_censor_policy(self):
        repeat.validate(self.plan)
        visits = self.plan["schedule"]
        self.assertEqual(len(visits), 225)
        for cell in self.plan["cells"]:
            orders = [v["order"] for v in visits if v["cell_id"] == cell["cell_id"]]
            self.assertEqual(len(orders), 5)
            self.assertTrue(all(a == list(reversed(b)) for a, b in zip(orders, orders[1:])))
        self.assertEqual(sum(v["order"][0] == "before" for v in visits), 113)
        row = {"result": "REALIZABLE", "seconds": "1"}
        timed = dict(row, seconds="1.1")
        self.assertTrue(repeat.cell_summary([row] * 5, [timed] * 5)["serious_timing_loss"])
        timeout = dict(row, result="TIMEOUT", seconds="17.04")
        result = repeat.cell_summary([row] * 5, [timed] * 4 + [timeout])
        self.assertIsNone(result["median_paired_ratio"])
        self.assertFalse(result["serious_timing_loss"])
        self.assertTrue(repeat.cell_summary([row] * 5, [timeout] * 4 + [row])["serious_solve_loss"])
        self.assertTrue(repeat.cell_summary([timeout] + [row] * 4, [row] + [timeout] * 4)["serious_solve_loss"])
        minority = [dict(row, seconds=str(x)) for x in [0.5, 0.5, 1.1, 1.2, 1.2]]
        self.assertFalse(repeat.cell_summary([row] * 5, minority)["serious_timing_loss"])
        self.assertEqual(repeat.score(dict(row, seconds="17.05"))["par2"], 34)
        bad = copy.deepcopy(self.plan)
        bad["cells"][0]["instance"] = "unplanned.ltl"
        with self.assertRaisesRegex(ValueError, "cohort changed"):
            repeat.validate(bad)

    def test_all_mocked_legs_resume_and_tamper(self):
        out = self.root / "run"
        self.assertEqual(repeat.execute(self.plan, self.plan_path, out), 0)
        self.assertEqual(Child.calls, 450)
        summary = json.loads((out / "summary.json").read_text())
        self.assertEqual(summary["rows"], 450)
        self.assertEqual(summary["resource_coverage"]["cpu_seconds"]["valid"], 450)
        self.assertEqual(repeat.execute(self.plan, self.plan_path, out, summary_only=True), 0)
        self.assertEqual(Child.calls, 450)
        log = next(out.glob("legs/*/*/stdout.log"))
        log.write_text("tampered evidence")
        with self.assertRaisesRegex(ValueError, "Changed input"):
            repeat.execute(self.plan, self.plan_path, out)
        self.assertEqual(Child.calls, 450)

    def test_interrupted_evidence_and_overcap_truth_stop(self):
        out = self.root / "interrupted"
        v = self.plan["schedule"][0]
        leg = out / "legs" / v["id"] / v["order"][0]
        leg.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "Unbound output"):
            repeat.execute(self.plan, self.plan_path, out)
        self.assertEqual(Child.calls, 0)
        Child.opposite = True
        out = self.root / "conflict"
        with self.assertRaisesRegex(ValueError, "Opposite native verdict"):
            repeat.execute(self.plan, self.plan_path, out)
        self.assertEqual(Child.calls, 2)
        self.assertEqual(len(list(out.glob("legs/*/*/record.json"))), 2)
        before_raw = next(p for p in out.glob("legs/*/before/raw/*.tsv") if not p.name.endswith(".memory.tsv"))
        self.assertEqual(json.loads(before_raw.read_text())["seconds"], "17.05")

    def test_signal_cleanup_and_invalid_resources(self):
        class CancelledChild(Child):
            terminated = False
            waits = 0

            def wait(self, **_kwargs):
                type(self).waits += 1
                if self.waits == 1:
                    repeat.interrupted(signal.SIGTERM, None)
                return 0

            def terminate(self):
                type(self).terminated = True

        out = self.root / "cancelled"
        with patch.object(repeat.subprocess, "Popen", CancelledChild):
            with self.assertRaises(SystemExit) as caught:
                repeat.execute(self.plan, self.plan_path, out)
        self.assertEqual(caught.exception.code, 143)
        self.assertTrue(CancelledChild.terminated)
        self.assertEqual(CancelledChild.waits, 2)
        self.assertEqual(len(list(out.glob("legs/*/*/started.json"))), 1)
        self.assertEqual(len(list(out.glob("legs/*/*/record.json"))), 0)
        for field, bad in [("cpu_seconds", "nan"), ("cpu_seconds", "-1"),
                           ("max_process_rss_bytes", "-1"), ("scope_memory_peak_bytes", "inf"),
                           ("scope_memory_peak_bytes", "1.5")]:
            with self.subTest(field=field, value=bad):
                with self.assertRaisesRegex(ValueError, "Invalid resource"):
                    repeat.resource_coverage([{field: bad}])
        missing = repeat.resource_coverage([{}])
        self.assertEqual(missing["cpu_seconds"]["valid"], 0)
        self.assertIsNone(missing["cpu_seconds"]["aggregate"])


if __name__ == "__main__":
    unittest.main()
