"""Synthetic-only saved-row fixtures; no campaign or workload execution."""

import copy
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location("default_table", Path(__file__).with_name("make-three-arm-table.py"))
table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(table)
# Existing scope_memory.MEMORY_KEYS, not an alternative invocation identity.
KEYS = ["solver_label", "instance", "cap_s", "run_index", "binary_sha256", "scope_unit",
        "seconds", "exit_code", "scope_memory_peak_bytes", "max_process_rss_bytes"]


def rows(verdict="TIMEOUT", events=None):
    raw = {"solver_label": "baseline-default", "instance": "synthetic.ltl", "cap_s": "17",
           "run_index": "0", "binary_sha256": "synthetic-fixture-identity", "scope_unit": "synthetic.scope",
           "seconds": "1.5" if verdict in table.DECISIVE else "17.1",
           "exit_code": {"REALIZABLE": "0", "UNREALIZABLE": "1"}.get(verdict, "124"),
           "scope_memory_peak_bytes": "123", "max_process_rss_bytes": "99",
           "result": verdict}
    memory = {key: raw[key] for key in KEYS}
    memory.update(scope_memory_events=json.dumps(events if events is not None else {"oom": 0, "oom_kill": 0}),
                  scope_memory_events_source="cgroup-v2/memory.events", scope_memory_events_missing_reason="",
                  memory_cgroup="synthetic.scope/invocation")
    return {raw["instance"]: raw}, {raw["instance"]: memory}


class SavedRowChecks(unittest.TestCase):
    def test_exact_three_arm_inventory_and_both_comparator_directions(self):
        self.assertEqual(table.LABELS, ["baseline-default", "optimized-default", "kd-scratch-restored"])
        self.assertEqual(set(table.PINS), set(table.LABELS))
        self.assertEqual(table.PINS["kd-scratch-restored"], "d363b9f901b927575ee1544b19820a146c2479d9")
        solved = {"baseline-default": {"old", "both"}, "optimized-default": {"new", "both"},
                  "kd-scratch-restored": {"old", "new", "kd"}}
        totals = {"baseline-default": 10.0, "optimized-default": 9.0, "kd-scratch-restored": 12.0}
        before = copy.deepcopy((solved, totals))
        pairs = table.compare_against_defaults("kd-scratch-restored", solved, totals)
        self.assertEqual(pairs["baseline-default"], {"gains": ["kd", "new"], "losses": ["both"], "par2_delta_s": 2.0})
        self.assertEqual(pairs["optimized-default"], {"gains": ["kd", "old"], "losses": ["both"], "par2_delta_s": 3.0})
        self.assertEqual(table.compare_against_defaults("baseline-default", solved, totals)["optimized-default"],
                         {"gains": ["old"], "losses": ["new"], "par2_delta_s": 1.0})
        self.assertEqual(before, (solved, totals))

    def test_zero_events_are_observed_zero_with_coverage(self):
        raw, memory = rows("REALIZABLE")
        audit, observations, missing = table.audit_events("baseline-default", raw, memory, KEYS)
        self.assertEqual(audit["event_observed_rows"], 1)
        self.assertTrue(audit["all_event_rows_observed"])
        self.assertEqual(audit["sum_observed_kernel_counters"], {"oom": 0, "oom_kill": 0})
        self.assertEqual((observations, missing), ([], []))
        raw, memory = rows("MEMOUT")
        audit, _, _ = table.audit_events("baseline-default", raw, memory, KEYS)
        self.assertEqual((audit["native_MEMOUT"], audit["oom_kill_observed_rows"]), (1, 0))

    def test_timeout_oom_event_is_not_relabelled_or_counted_as_memout(self):
        raw, memory = rows(events={"oom": 5, "oom_kill": 3})
        before = copy.deepcopy((raw, memory))
        audit, observations, missing = table.audit_events("baseline-default", raw, memory, KEYS)
        self.assertEqual(audit["native_TIMEOUT"], 1)
        self.assertEqual(audit["native_MEMOUT"], 0)
        self.assertEqual(audit["oom_kill_observed_rows"], 1)
        self.assertEqual(audit["native_TIMEOUT_with_oom_kill"], 1)
        self.assertEqual(audit["sum_observed_kernel_counters"]["oom_kill"], 3)
        self.assertEqual(observations[0]["native_result"], "TIMEOUT")
        self.assertFalse(missing)
        self.assertEqual(before, (raw, memory))

    def test_missing_events_are_not_complete_zero(self):
        raw, memory = rows()
        memory["synthetic.ltl"].update(scope_memory_events="", scope_memory_events_missing_reason="not published")
        audit, _, missing = table.audit_events("baseline-default", raw, memory, KEYS)
        self.assertEqual((audit["event_observed_rows"], audit["event_missing_rows"]), (0, 1))
        self.assertFalse(audit["all_event_rows_observed"])
        self.assertEqual(missing[0]["reason"], "not published")
        memory["synthetic.ltl"]["scope_memory_events_missing_reason"] = ""
        with self.assertRaisesRegex(ValueError, "without reason"):
            table.audit_events("baseline-default", raw, memory, KEYS)

    def test_each_invocation_key_and_case_inventory_is_checked(self):
        for key in KEYS:
            with self.subTest(key=key):
                raw, memory = rows()
                memory["synthetic.ltl"][key] += "-wrong"
                with self.assertRaisesRegex(ValueError, "invocation identity"):
                    table.audit_events("baseline-default", raw, memory, KEYS)
        raw, memory = rows()
        memory["extra.ltl"] = memory["synthetic.ltl"]
        with self.assertRaisesRegex(ValueError, "case identities"):
            table.audit_events("baseline-default", raw, memory, KEYS)

    def test_event_counter_types_ranges_and_provenance_are_checked(self):
        for events in ([], {}, {"oom": 0}, {"oom": 0, "oom_kill": True},
                       {"oom": 0, "oom_kill": 1.0}, {"oom": 0, "oom_kill": -1},
                       {"oom": 0, "oom_kill": 2**64}):
            with self.subTest(events=events):
                raw, memory = rows(events=events)
                with self.assertRaisesRegex(ValueError, "Invalid kernel event counters"):
                    table.audit_events("baseline-default", raw, memory, KEYS)
        raw, memory = rows()
        memory["synthetic.ltl"]["scope_memory_events_source"] = "unbound observer"
        with self.assertRaisesRegex(ValueError, "Invalid event provenance"):
            table.audit_events("baseline-default", raw, memory, KEYS)

    def test_missing_extra_duplicate_cases_and_changed_bytes_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic.tsv"
            for names in ([str(i) for i in range(1523)], [str(i) for i in range(1525)],
                          [str(i) for i in range(1523)] + ["0"]):
                with path.open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, ["instance"], delimiter="\t")
                    writer.writeheader()
                    writer.writerows({"instance": name} for name in names)
                with self.assertRaisesRegex(ValueError, "Wrong coverage"):
                    table.read_binding({"path": str(path), "sha256": table.sha(path)})
            binding = {"path": str(path), "sha256": table.sha(path)}
            path.write_text(path.read_text() + "tampered\n")
            with self.assertRaisesRegex(ValueError, "Changed source"):
                table.read_binding(binding)


if __name__ == "__main__":
    unittest.main()
