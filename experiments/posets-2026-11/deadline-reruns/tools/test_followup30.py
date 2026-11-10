#!/usr/bin/env python3
"""Synthetic offline checks; no campaign, source ELF, corpus, or scope is run."""
import copy
import csv
import importlib.util
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('followup30', HERE / 'followup30.py')
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)


def arm(label):
    return {'label': label, 'binary': '/synthetic/' + label, 'binary_sha256': label * 64,
            'acacia_sha': 'a' * 40, 'source_pins': {'posets': f.E167 if label == 'a' else 'b' * 40},
            'preset': 'otf_sparse_formula_parity', 'flags': ''}


def manifest():
    return {'arms': [arm('a'), arm('b')], 'baseline': 'a', 'cap_s': 17,
            'memory_max': '8G', 'memory_swap_max': '0', 'collect_rusage': True,
            'allowed_cpus': None, 'cpu_quota': None,
            'repo': str(f.ROOT / 'work/acacia-posets-eval'),
            'host_lock': f'/tmp/acacia-posets-benchmark-{os.getuid()}.lock'}


def row(name, result='REALIZABLE', duration='5', label='a', cap='17', kills=0):
    return {'instance': name, 'result': result, 'seconds': duration, 'cap_s': cap,
            'solver_label': label, 'binary_sha256': label * 64, 'tlsf_file': name + '.tlsf',
            'acacia_sha': 'a' * 40, 'preset': 'otf_sparse_formula_parity', 'flags': '',
            'memory_max': '8G', 'memory_swap_max': '0', 'collect_rusage': 'true',
            'allowed_cpus': '', 'cpu_quota': '', 'cpu_seconds': '4.1',
            'max_process_rss_bytes': '1234', 'scope_memory_peak_bytes': '2345',
            'scope_memory_events': json.dumps({'oom': kills, 'oom_kill': kills, 'oom_group_kill': 0}),
            'scope_unit': 'acacia-synthetic.scope', 'run_index': '0',
            'exit_code': '0' if result == 'REALIZABLE' else '1' if result == 'UNREALIZABLE' else '124'}


def source_panels():
    names = ['covered', 'native-timeout', 'only-memout', 'overrun', 'oom-timeout', 'edge']
    native = {
        'a': [row(names[0]), row(names[1], 'TIMEOUT', '17.1'), row(names[2], 'MEMOUT', '10'),
              row(names[3], 'REALIZABLE', '17.000001'), row(names[4], 'TIMEOUT', '17.2', kills=2),
              row(names[5], 'UNREALIZABLE', '17')],
        'b': [row(names[0], label='b'), row(names[1], 'MEMOUT', '10', 'b'),
              row(names[2], 'MEMOUT', '10', 'b'), row(names[3], 'REALIZABLE', '16', 'b'),
              row(names[4], 'MEMOUT', '10', 'b'), row(names[5], 'UNREALIZABLE', '17', 'b')],
    }
    scored = {a: [dict(r, result=f.scored_result(r, 17)) for r in rows] for a, rows in native.items()}
    return names, native, scored


class Followup30Tests(unittest.TestCase):
    def test_timeout_union_includes_overrun_and_oom_but_not_pure_memout(self):
        names, native, scored = source_panels()
        original = copy.deepcopy(native)
        cohort, truths = f.select_cohort(names, native, scored)
        self.assertEqual(cohort, ['native-timeout', 'overrun', 'oom-timeout'])
        self.assertEqual(truths, {'covered': 'REALIZABLE', 'overrun': 'REALIZABLE', 'edge': 'UNREALIZABLE'})
        self.assertEqual(native, original)

    def test_source_panel_missing_duplicate_or_rescored_time_is_rejected(self):
        for mode in ('missing', 'duplicate', 'scored-time', 'wrong-cap', 'duplicate-name'):
            names, native, scored = source_panels()
            if mode == 'missing':
                native['a'].pop()
            elif mode == 'duplicate':
                native['a'][-1] = dict(native['a'][0])
            elif mode == 'scored-time':
                scored['a'][0]['seconds'] = '0.1'
            elif mode == 'wrong-cap':
                native['a'][0]['cap_s'] = '30'
            else:
                names[-1] = names[0]
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                f.select_cohort(names, native, scored)

    def test_raw_truth_conflicts_stop_even_when_beyond_either_deadline(self):
        truths = {}
        f.observe_truth(truths, row('x', 'REALIZABLE', '17.01'))
        with self.assertRaisesRegex(ValueError, 'Opposite raw verdict'):
            f.observe_truth(truths, row('x', 'UNREALIZABLE', '30.01', 'b', '30'))
        f.observe_truth(truths, row('y', 'UNREALIZABLE', '30.01', 'a', '30'))
        with self.assertRaises(ValueError):
            f.observe_truth(truths, row('y', 'REALIZABLE', '29', 'b', '30'))

    def test_native_error_outcomes_reject_valid_followup_and_source_rows(self):
        for outcome in ('CRASH', 'ERROR', 'AUDIT-ERROR', 'SYFCO-FAIL'):
            bad = row('x', outcome, '1', cap='30')
            f.resource_guard([bad])
            with self.subTest(outcome=outcome), self.assertRaisesRegex(ValueError, 'Unsupported native outcome'):
                f.check_row30(bad, arm('a'), manifest(), 'x', 'x.tlsf')
            names, native, scored = source_panels()
            native['a'][1]['result'] = outcome
            scored['a'][1] = dict(native['a'][1])
            native['b'][1]['result'] = 'TIMEOUT'
            scored['b'][1] = dict(native['b'][1])
            with self.subTest(source_outcome=outcome), self.assertRaisesRegex(ValueError, 'Unsupported native outcome'):
                f.select_cohort(names, native, scored)
        unknown = row('x', 'UNKNOWN', '1', cap='30')
        f.check_row30(unknown, arm('a'), manifest(), 'x', 'x.tlsf')
        self.assertEqual(f.scored_result(unknown, 30), 'UNKNOWN')

    def test_cap_projection_changes_only_the_cap_and_visits_both_arms_once(self):
        calls = []
        def original(m, a, case_list, leg):
            command = ['python', 'coverage.py', '--caps', '17', '--bin', a['binary'],
                       '--list', str(case_list), '--output', str(leg / 'raw.tsv'), '--flags=']
            calls.append(command)
            return command
        visits = f.commands(manifest(), ['x', 'y'], Path('/synthetic/output'), original)
        self.assertEqual([(v['instance'], v['label']) for v in visits],
                         [('x', 'a'), ('x', 'b'), ('y', 'b'), ('y', 'a')])
        for old, visit in zip(calls, visits):
            changed = [i for i, (a, b) in enumerate(zip(old, visit['command'])) if a != b]
            self.assertEqual(len(old), len(visit['command']))
            self.assertEqual(changed, [3])
            self.assertEqual(visit['command_delta'], {'index': 3, 'before': '17', 'after': '30'})
            self.assertEqual(old[3], '17')
        with self.assertRaises(ValueError):
            f.commands(manifest(), ['x'], Path('/synthetic'), lambda *args: ['--caps', '17', '--caps', '30'])
        with self.assertRaises(ValueError):
            f.commands(manifest(), ['x'], Path('/synthetic'), lambda *args: ['--caps', '30'])

    def test_complete_gate_requires_a_full_usable_exactly_bound_two_arm_source(self):
        m = manifest()
        progress = {'status': 'complete', 'completed': {'a': 1524, 'b': 1524}}
        summary = {'usable': True, 'conflicts': [], 'frozen_manifest_sha256': 'freeze'}
        f.complete_gate(m, progress, summary, 'freeze')
        for mode in ('unfinished', 'short', 'unusable', 'conflict', 'freeze', 'wrong-baseline', 'cap', 'arm', 'flags'):
            mm, pp, ss = copy.deepcopy(m), copy.deepcopy(progress), copy.deepcopy(summary)
            if mode == 'unfinished': pp['status'] = 'running'
            elif mode == 'short': pp['completed']['b'] = 1523
            elif mode == 'unusable': ss['usable'] = False
            elif mode == 'conflict': ss['conflicts'] = ['opposite truth']
            elif mode == 'freeze': ss['frozen_manifest_sha256'] = 'other'
            elif mode == 'wrong-baseline': mm['arms'][0]['source_pins']['posets'] = 'other'
            elif mode == 'cap': mm['cap_s'] = 30
            elif mode == 'arm': mm['arms'].pop()
            else: mm['arms'][1]['flags'] = '--backward'
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                f.complete_gate(mm, pp, ss, 'freeze')

    def test_followup_identity_and_resource_acceptance(self):
        valid = row('x', duration='30.01', cap='30')  # Kept as native truth; not scored solved.
        f.check_row30(valid, arm('a'), manifest(), 'x', 'x.tlsf')
        for key, value in [('cap_s', '17'), ('binary_sha256', 'other'), ('preset', 'other'),
                           ('flags', '--backward'), ('tlsf_file', 'other'), ('solver_label', 'b'),
                           ('exit_code', '1'), ('seconds', 'nan'), ('seconds', '-1'),
                           ('cpu_seconds', 'inf'), ('scope_memory_peak_bytes', '-1'),
                           ('max_process_rss_bytes', ''), ('scope_memory_events', '{"oom":0}')]:
            bad = dict(valid, **{key: value})
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                f.check_row30(bad, arm('a'), manifest(), 'x', 'x.tlsf')
        for value in ('true', '-1', '1.5'):
            bad = dict(valid, scope_memory_events='{"oom":0,"oom_kill":' + value + ',"oom_group_kill":0}')
            with self.subTest(value=value), self.assertRaises(ValueError):
                f.resource_guard([bad])

    def test_exact_resource_sidecar_join(self):
        native = [row('x', cap='30')]
        side = [{k: native[0][k] for k in f.MEMORY_KEYS}]
        f.join_guard(native, side)
        for changed in ([], side + side, [dict(side[0], instance='other')],
                        [dict(side[0], scope_unit='other.scope')], [dict(side[0], cap_s='17')]):
            with self.subTest(side=changed), self.assertRaises(ValueError):
                f.join_guard(native, changed)

    def test_conditional_report_charges_sixty_and_keeps_native_oom_labels(self):
        plan = {'manifest17': manifest(), 'cohort': ['x', 'y'], 'scope': 'synthetic conditional cohort',
                'scored17': {'a': {'x': 'TIMEOUT', 'y': 'REALIZABLE'},
                             'b': {'x': 'TIMEOUT', 'y': 'TIMEOUT'}}}
        observed = {'a': [row('x', duration='30', cap='30'), row('y', duration='30.01', cap='30')],
                    'b': [row('x', 'TIMEOUT', '30.1', 'b', '30', 2),
                          row('y', 'MEMOUT', '10', 'b', '30', 3)]}
        original = copy.deepcopy(observed)
        answer = f.report(plan, observed)
        self.assertEqual(answer['selected_cases'], 2)
        self.assertEqual(answer['results']['a']['solved30'], 1)
        self.assertEqual(answer['results']['a']['conditional_PAR2_s'], 90)
        self.assertEqual(answer['results']['a']['resolved17_TIMEOUT'], 1)
        self.assertEqual(answer['results']['a']['native_outcomes'], {'REALIZABLE': 2})
        self.assertEqual(answer['results']['b']['conditional_PAR2_s'], 120)
        self.assertEqual(answer['results']['b']['native_outcomes'], {'TIMEOUT': 1, 'MEMOUT': 1})
        self.assertEqual(answer['results']['b']['oom_kill_invocations'], 2)
        self.assertEqual(answer['results']['b']['oom_kill_counter_sum'], 5)
        self.assertEqual(observed, original)
        for bad in ({'a': observed['a']}, dict(observed, extra=[]),
                    dict(observed, a=observed['a'][:1]), dict(observed, a=[observed['a'][0]] * 2)):
            with self.subTest(panel=bad), self.assertRaises(ValueError):
                f.report(plan, bad)
        empty = dict(plan, cohort=[])
        self.assertEqual(f.report(empty, {'a': [], 'b': []})['results']['a']['conditional_PAR2_s'], 0)

    def test_mocked_controller_completion_resume_partial_error_and_cancel(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source17.json'
            source.write_text('synthetic source binding\n')
            bindings = [f.binding(source)]
            m = manifest()
            m.update(host_lock=str(root / 'host.lock'), environment={})
            scored = {'a': [row('x', 'TIMEOUT', '17.1')],
                      'b': [row('x', 'REALIZABLE', '16', 'b')]}

            def command(_manifest, current, case_list, leg):
                return ['synthetic-coverage', '--caps', '17', '--bin', current['binary'],
                        '--solver-label', current['label'], '--list', str(case_list),
                        '--output', str(leg / 'raw' / (current['label'] + '.tsv'))]

            campaign = SimpleNamespace(command=command, verify_files=lambda *_args: None,
                                       running_scopes=lambda: [],
                                       raw_path=lambda leg, current: leg / 'raw' / (current['label'] + '.tsv'))

            def load_output(path):
                with path.open() as stream:
                    return list(csv.DictReader(stream, delimiter='\t'))

            coverage = SimpleNamespace(load_output=load_output, load_conflicts=lambda _path: [])

            class Sampler:
                exits = 0
                def __init__(self, *_args, **_kwargs): pass
                def __enter__(self): return self
                def __exit__(self, *_args): type(self).exits += 1
                def set_leg(self, *_args): pass

            class Child:
                calls = []
                mode = 'complete'

                def __init__(self, argv, **_kwargs):
                    self.argv = argv
                    self.terminated = False
                    self.waits = []
                    type(self).calls.append(self)
                    def arg(name): return argv[argv.index(name) + 1]
                    raw = Path(arg('--output'))
                    value = row(Path(arg('--list')).read_text().strip(),
                                'CRASH' if self.mode == 'error' else 'REALIZABLE',
                                '5', arg('--solver-label'), arg('--caps'))
                    with raw.open('x') as stream:
                        writer = csv.DictWriter(stream, fieldnames=list(value), delimiter='\t')
                        writer.writeheader(); writer.writerow(value)
                    with Path(str(raw) + '.memory.tsv').open('x') as stream:
                        writer = csv.DictWriter(stream, fieldnames=f.MEMORY_KEYS, delimiter='\t')
                        writer.writeheader(); writer.writerow({k: value[k] for k in f.MEMORY_KEYS})

                def wait(self, timeout=None):
                    self.waits.append(timeout)
                    if self.mode == 'cancel' and not self.terminated:
                        f.interrupted(signal.SIGTERM, None)
                    return 0  # A native error row may still have a successful coverage-CLI exit.

                def terminate(self): self.terminated = True

            def parent(_path):
                return (campaign, SimpleNamespace(HostSampler=Sampler), coverage, copy.deepcopy(m),
                        ['x'], {'x': 'REALIZABLE'}, copy.deepcopy(scored), {'x': 'x.tlsf'}, copy.deepcopy(bindings))

            def plan_at(name):
                output = root / name
                (output / 'lists').mkdir(parents=True)
                (output / 'lists/0000.list').write_text('x\n')
                plan = {'parent17': str(root / 'source17'), 'writer': f.binding(HERE / 'followup30.py'),
                        'policy': f.binding(f.POLICY), 'interpreter': f.binding(sys.executable),
                        'bindings': bindings, 'manifest17': m, 'cohort': ['x'], 'truth17': {'x': 'REALIZABLE'},
                        'scored17': {'a': {'x': 'TIMEOUT'}, 'b': {'x': 'REALIZABLE'}},
                        'mapping': {'x': 'x.tlsf'}, 'scope': 'synthetic one-case controller cohort',
                        'cap_s': 30, 'unsolved_PAR2_s': 60,
                        'visits': f.commands(m, ['x'], output, command)}
                path = output / 'plan.json'
                f.write_new(path, plan)
                return path, plan

            def owner(path):
                with patch.object(sys, 'argv', [str(HERE / 'followup30.py'), '--run', str(path)]):
                    return f.main()

            with patch.object(f, 'load_parent', parent), patch.object(f, 'environment', lambda _m: {}), \
                    patch.object(f.subprocess, 'Popen', Child):
                completed, plan = plan_at('complete')
                self.assertEqual(owner(completed), 0)
                self.assertEqual([(c.argv[c.argv.index('--solver-label') + 1],
                                   c.argv[c.argv.index('--caps') + 1]) for c in Child.calls],
                                 [('a', '30'), ('b', '30')])
                self.assertEqual(owner(completed), 0)
                self.assertEqual(len(Child.calls), 2)

                partial, plan = plan_at('partial')
                first = Path(plan['visits'][0]['leg'])
                first.mkdir(parents=True)
                (first / 'started.json').write_text('{"partial":true}\n')
                with self.assertRaisesRegex(ValueError, 'Interrupted attempt preserved'):
                    owner(partial)
                self.assertEqual(len(Child.calls), 2)
                self.assertTrue((first / 'started.json').exists())
                self.assertFalse((partial.parent / 'summary30.json').exists())
                self.assertEqual(len(list(partial.parent.glob('STOP-*.json'))), 1)

                Child.mode = 'error'
                error, plan = plan_at('error')
                with self.assertRaisesRegex(ValueError, 'Unsupported native outcome: CRASH'):
                    owner(error)
                first = Path(plan['visits'][0]['leg'])
                self.assertEqual(len(Child.calls), 3)
                self.assertTrue((first / 'started.json').exists())
                self.assertTrue((first / 'record.json').exists())
                self.assertEqual(load_output(first / 'raw/a.tsv')[0]['result'], 'CRASH')
                self.assertFalse((error.parent / 'summary30.json').exists())
                self.assertEqual(len(list(error.parent.glob('STOP-*.json'))), 1)

                Child.mode = 'cancel'
                cancelled, plan = plan_at('cancel')
                previous_exits = Sampler.exits
                with self.assertRaises(SystemExit) as stopped:
                    owner(cancelled)
                self.assertEqual(stopped.exception.code, 128 + signal.SIGTERM)
                self.assertEqual(len(Child.calls), 4)
                self.assertTrue(Child.calls[-1].terminated)
                self.assertEqual(Child.calls[-1].waits, [None, 20])
                self.assertEqual(Sampler.exits, previous_exits + 1)
                first = Path(plan['visits'][0]['leg'])
                self.assertTrue((first / 'started.json').exists())
                self.assertFalse((first / 'record.json').exists())
                self.assertFalse((cancelled.parent / 'summary30.json').exists())
                self.assertEqual(len(list(cancelled.parent.glob('STOP-*.json'))), 1)
                with self.assertRaisesRegex(ValueError, 'Interrupted attempt preserved'):
                    owner(cancelled)
                self.assertEqual(len(Child.calls), 4)
            self.assertEqual(source.read_text(), 'synthetic source binding\n')


if __name__ == '__main__':
    unittest.main()
