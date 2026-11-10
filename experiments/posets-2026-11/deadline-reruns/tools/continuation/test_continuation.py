#!/usr/bin/env python3
"""Offline synthetic fixtures only; no native CLI, ELF, scope, or corpus is run."""
from contextlib import ExitStack
import copy
import csv
from dataclasses import dataclass
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import capture_native as adapter
import continue30 as c

f = c.old_module()  # Source definitions only; never load_parent()/execute() on real inputs.
LABELS = ['anchor-e167', 'lazy-fc']


def arm(label):
    return {'label': label, 'binary': '/synthetic/' + label, 'binary_sha256': label * 8,
            'acacia_sha': 'a' * 40, 'preset': 'otf_sparse_formula_parity', 'flags': ''}


def row(name, label, outcome='TIMEOUT'):
    return {'solver_label': label, 'instance': name, 'tlsf_file': name + '.tlsf', 'cap_s': '30',
            'binary_sha256': arm(label)['binary_sha256'], 'acacia_sha': 'a' * 40,
            'flags': '', 'preset': 'otf_sparse_formula_parity', 'memory_max': '8G',
            'memory_swap_max': '0', 'collect_rusage': 'true', 'allowed_cpus': '', 'cpu_quota': '',
            'result': outcome, 'exit_code': '3' if outcome == 'ERROR' else '124',
            'seconds': '20.121184411924332' if outcome == 'ERROR' else '30.1',
            'timed_out': 'false' if outcome == 'ERROR' else 'true', 'resource_reason': '',
            'stdout_bytes': '0', 'stderr_bytes': '1080' if outcome == 'ERROR' else '0',
            'cpu_seconds': '20.123457', 'scope_memory_peak_bytes': '2345', 'max_process_rss_bytes': '1234',
            'scope_unit': 'synthetic.scope', 'scope_memory_events': json.dumps({'oom': 0, 'oom_kill': 0, 'oom_group_kill': 0}),
            'run_index': '0'}


@dataclass(frozen=True)
class Result:
    stdout: str
    stderr: str
    returncode: int
    seconds: float
    timed_out: bool
    stdout_bytes: int
    stderr_bytes: int
    resource_limited: bool = False
    cpu_seconds: float = 20.123456789
    memory_peak_bytes: int = 2345
    max_process_rss_bytes: int = 1234
    scope_unit: str = 'synthetic.scope'
    scope_memory_events: str = json.dumps({'oom': 0, 'oom_kill': 0, 'oom_group_kill': 0})


def synthetic_old(root):
    names = [f'case-{i}.ltl' for i in range(156)]
    names[102] = 'ltl2dba_R_pb_12_pe_.ltl'
    cli = str(root / 'synthetic-native.py')
    manifest = {'arms': [arm(label) for label in LABELS], 'baseline': LABELS[0],
                'allowed_cpus': None, 'cpu_quota': None, 'repo': str(root),
                'host_lock': str(root / 'synthetic-host.lock'), 'tlsf_corpus': str(root / 'corpus'),
                'implementation': {cli: 'synthetic'}, 'environment': {}}
    (root / 'corpus').mkdir()
    for name in names:
        (root / 'corpus' / (name + '.tlsf')).write_text('synthetic; never passed to an ELF\n')
    visits = []
    for i, name in enumerate(names):
        for label in LABELS if i % 2 == 0 else list(reversed(LABELS)):
            leg = root / 'old-untouched' / 'legs' / f'{i:04d}' / label
            case_list = root / 'old-untouched' / 'lists' / f'{i:04d}.list'
            visits.append({'instance': name, 'label': label, 'leg': str(leg), 'case_list': str(case_list),
                           'command': [sys.executable, '-u', cli, '--list', str(case_list), '--caps', '30',
                                       '--output', str(leg / 'raw' / (label + '.tsv')), '--flags=']})
    old = {'cohort': names, 'manifest17': manifest, 'visits': visits, 'bindings': [], 'truth17': {},
           'mapping': {name: name + '.tlsf' for name in names},
           'scored17': {label: dict.fromkeys(names, 'TIMEOUT') for label in LABELS},
           'scope': 'synthetic conditional cohort; no original evidence loaded'}
    completed = [{'original_visit_index': i, 'original_visit': visit} for i, visit in enumerate(visits[:205])]
    pending = [{'original_visit_index': i, 'original_visit': visits[i]} for i in range(205, 312)]
    return old, completed, pending


class ContinuationTests(unittest.TestCase):
    def test_exact_signature_only_and_identity_before_adjudication(self):
        value = row('x', LABELS[0], 'ERROR')
        signature = (c.HERE / 'spot-limit32.stderr.txt').read_text()
        manifest = {'allowed_cpus': None, 'cpu_quota': None}
        c.identity(f, value, arm(LABELS[0]), manifest, {'instance': 'x'}, 'x.tlsf')
        self.assertEqual(c.category(f, value, '', signature), c.KIND)
        for stdout, stderr in [('', signature + 'another error\n'), ('REALIZABLE\n', signature),
                               ('', signature.replace('limit is 32', 'limit is 64')),
                               ('', signature.replace('Exception caught:', '', 1)), ('', signature[:-1])]:
            with self.subTest(stderr=stderr[-30:]), self.assertRaises(ValueError):
                c.category(f, value, stdout, stderr)
        for key, replacement in [('result', 'CRASH'), ('exit_code', '2'), ('timed_out', 'true'),
                                 ('resource_reason', 'memory'), ('stderr_bytes', '1079'),
                                 ('scope_memory_events', json.dumps({'oom': 1, 'oom_kill': 1, 'oom_group_kill': 0}))]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                c.category(f, dict(value, **{key: replacement}), '', signature)
        for key, replacement in [('solver_label', LABELS[1]), ('tlsf_file', 'other.tlsf'), ('cap_s', '17'),
                                 ('binary_sha256', 'other'), ('scope_memory_peak_bytes', '-1')]:
            with self.subTest(identity=key), self.assertRaises(ValueError):
                c.identity(f, dict(value, **{key: replacement}), arm(LABELS[0]), manifest, {'instance': 'x'}, 'x.tlsf')
        for ordinary in f.ORDINARY:
            self.assertIsNone(c.category(f, dict(value, result=ordinary)))

    def test_adapter_preserves_delegate_identity_arguments_and_native_main(self):
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / 'capture'
            command = ['exact-elf', '-T', 'same.tlsf']
            kwargs = {'timeout': 30, 'env': None, 'memory_max': '8G', 'memory_swap_max': '0'}
            answer = Result('', 'text\n', 3, 1.2, False, 0, 5)
            seen = []
            def original(*args, **actual):
                self.assertFalse(destination.exists())
                self.assertIs(args[0], command)
                self.assertEqual(actual, kwargs)
                seen.append((args, actual))
                return answer
            native = SimpleNamespace(run_systemd_scope=original)
            argv = ['--caps', '30', '--unchanged']
            def main(actual):
                self.assertIs(actual, argv)
                self.assertIs(native.run_systemd_scope(command, **kwargs), answer)
                return 0
            native.main = main
            self.assertEqual(adapter.invoke(native, argv, destination), 0)
            self.assertIs(native.run_systemd_scope, original)
            self.assertEqual(len(seen), 1)
            self.assertEqual((destination / 'stderr.txt').read_text(), answer.stderr)
            self.assertEqual(c.read(destination / 'result.json')['seconds'], answer.seconds)
            with self.assertRaisesRegex(ValueError, 'Duplicate/partial'):
                adapter.invoke(native, argv, destination)
            self.assertEqual(len(seen), 1)

    def test_partition_and_only_list_output_adapter_projection(self):
        with tempfile.TemporaryDirectory() as temp:
            old, completed, pending = synthetic_old(Path(temp))
            c.partition(old, completed, pending)
            projected = c.project(old, pending, Path(temp) / 'new')
            self.assertEqual(len(projected), 107)
            self.assertEqual(projected[0]['original_visit_index'], 205)
            self.assertEqual(projected[0]['label'], 'lazy-fc')
            self.assertEqual(c.GAP['uninterrupted_prefix_pairs'], 102)
            self.assertEqual(c.GAP['uninterrupted_suffix_pairs'], 53)
            for item, visit in zip(pending, projected):
                before = item['original_visit']['command']; after = visit['native_cli']
                allowed = [before.index('--list') + 1, before.index('--output') + 1]
                self.assertEqual([i for i, (a, b) in enumerate(zip(before, after)) if a != b], allowed)
                self.assertEqual(len(before), len(after))
                self.assertEqual(visit['command'][:2], before[:2])
                self.assertEqual(visit['command'][4:7], [before[2], 'synthetic', '--'])
                self.assertEqual(visit['command'][7:], after[3:])
                self.assertEqual(after[after.index('--caps') + 1], '30')
            for mode in ['missing', 'duplicate', 'reordered', 'changed-original', 'prefix-short']:
                cc, pp = copy.deepcopy(completed), copy.deepcopy(pending)
                if mode == 'missing': pp.pop()
                elif mode == 'duplicate': pp[-1] = pp[0]
                elif mode == 'reordered': pp[0], pp[1] = pp[1], pp[0]
                elif mode == 'changed-original': pp[0]['original_visit']['command'].append('--route-records')
                else: cc.pop()
                with self.subTest(mode=mode), self.assertRaises(ValueError):
                    c.partition(old, cc, pp)

    def test_capture_inventory_delegation_complete_streams_and_metadata(self):
        for mode in ['inventory', 'kwargs', 'argv', 'truncated', 'cpu']:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); (root / 'x.tlsf').write_text('synthetic\n')
                value = row('x', LABELS[0], 'ERROR')
                signature = (c.HERE / 'spot-limit32.stderr.txt').read_text()
                answer = Result('', signature, 3, float(value['seconds']), False, 0, 1080)
                destination = root / 'capture'
                adapter.Capture(lambda *args, **kwargs: answer, destination)(
                    [arm(LABELS[0])['binary'], '-T', str(root / 'x.tlsf')], timeout=30,
                    memory_max='8G', memory_swap_max='0', allowed_cpus=None, cpu_quota=None,
                    unit_prefix='acacia-syntcomp26-coverage', env=None)
                manifest = {'tlsf_corpus': str(root)}
                self.assertEqual(c.capture_result(f, value, destination, arm(LABELS[0]), manifest, 'x.tlsf'), c.KIND)
                proof = c.read(destination / 'capture.json')
                if mode == 'inventory': proof['files'] = []
                elif mode == 'kwargs': proof['kwargs']['env'] = {}
                elif mode == 'argv': proof['args'][0].append('--new-flag')
                elif mode == 'cpu': value['cpu_seconds'] = '1.000000'
                else:
                    (destination / 'stderr.txt').write_text(signature[:-1])
                    proof['files'] = [c.bind(destination / name) for name in ['stdout.txt', 'stderr.txt', 'result.json']]
                (destination / 'capture.json').write_text(json.dumps(proof))
                with self.assertRaises(ValueError):
                    c.capture_result(f, value, destination, arm(LABELS[0]), manifest, 'x.tlsf')

    def owner(self, root, mode='normal'):
        old, completed, pending = synthetic_old(root)
        output = root / 'new'; output.mkdir(); (output / 'lists').mkdir()
        visits = c.project(old, pending, output)
        for visit in visits:
            path = Path(visit['case_list'])
            if not path.exists(): path.write_text(visit['instance'] + '\n')
        observed = {label: [] for label in LABELS}
        for visit in old['visits'][:205]:
            observed[visit['label']].append(row(visit['instance'], visit['label'],
                                               'ERROR' if len(observed[LABELS[0]]) + len(observed[LABELS[1]]) == 204 else 'TIMEOUT'))
        original_copy = copy.deepcopy(observed)
        adjudication = [{'original_visit_index': 204, 'classification': c.KIND, 'native_result': 'ERROR',
                         'adjudication_source': 'separate diagnostic; original primary stderr was not recovered', 'PAR2_s': 60}]
        mapping = {'original_plan': {'path': 'synthetic-original-plan', 'sha256': 'old'}}
        plan = {'writer': c.bind(c.HERE / 'continue30.py'), 'adapter': c.bind(c.HERE / 'capture_native.py'),
                'signature': c.bind(c.HERE / 'spot-limit32.stderr.txt'), 'source_mapping': c.bind(c.MAPPING),
                'original_plan': mapping['original_plan'], 'adjudication': adjudication, 'pair_gap': c.GAP,
                'visits': visits, 'original_record_count': 205, 'new_visit_count': 107, 'total_observations': 312}
        path = output / 'amendment.json'; c.write(path, plan)
        release = root / 'release.json'
        c.write(release, {'amendment': c.bind(path), 'accepted_error_category': c.KIND, 'max_new_solver_calls': 107})
        state = {'calls': [], 'terminated': 0, 'cleanup_waits': 0}
        class Sampler:
            def __init__(self, *args, **kwargs): pass
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def set_leg(self, *args): pass
        def write_rows(destination, values):
            with destination.open('w') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(values), delimiter='\t')
                writer.writeheader(); writer.writerow(values)
        def popen(command, **kwargs):
            visit = visits[len(state['calls'])]
            self.assertEqual(command, visit['command'])
            self.assertEqual(kwargs['cwd'], old['manifest17']['repo'])
            self.assertEqual(kwargs['env'], {'synthetic': 'unchanged'})
            state['calls'].append(visit['original_visit_index'])
            class Child:
                def wait(self, timeout=None):
                    if timeout is not None:
                        state['cleanup_waits'] += 1; return 0
                    if mode == 'cancel': raise SystemExit(143)
                    value = row(visit['instance'], visit['label'], 'ERROR' if mode in ['known-error', 'unknown-error', 'identity-error'] else 'TIMEOUT')
                    stderr = (c.HERE / 'spot-limit32.stderr.txt').read_text() if value['result'] == 'ERROR' else ''
                    if mode == 'unknown-error': stderr = 'X' + stderr[1:]  # Same byte count; must classify text.
                    answer = Result('', stderr, int(value['exit_code']), float(value['seconds']),
                                    value['timed_out'] == 'true', 0, len(stderr.encode()))
                    target = Path(visit['leg']) / 'capture'
                    def delegate(*args, **actual): return answer
                    adapter.Capture(delegate, target)([arm(visit['label'])['binary'], '-T', str((root / 'corpus' / value['tlsf_file']).resolve())],
                        timeout=30, memory_max='8G', memory_swap_max='0', allowed_cpus=None, cpu_quota=None,
                        unit_prefix='acacia-syntcomp26-coverage', env=None)
                    if mode == 'identity-error': value['tlsf_file'] = 'wrong.tlsf'
                    raw = Path(visit['leg']) / 'raw' / (visit['label'] + '.tsv')
                    write_rows(raw, value); write_rows(Path(str(raw) + '.memory.tsv'), value)
                    return 0  # Native CLI can succeed while recording a solver ERROR.
                def terminate(self): state['terminated'] += 1
            return Child()
        def load_rows(path):
            with Path(path).open() as stream: return list(csv.DictReader(stream, delimiter='\t'))
        def atomic(path, data): path.write_text(json.dumps(data))
        campaign = SimpleNamespace(running_scopes=lambda: [], verify_files=lambda *args: None,
                                   raw_path=lambda leg, a: leg / 'raw' / (a['label'] + '.tsv'), atomic_json=atomic)
        coverage = SimpleNamespace(load_output=load_rows, load_conflicts=lambda path: [])
        state['source'] = (f, campaign, SimpleNamespace(HostSampler=Sampler), coverage, mapping, old, completed, pending)
        stack = ExitStack(); self.addCleanup(stack.close)
        stack.enter_context(patch.object(c, 'source_state', return_value=state['source']))
        stack.enter_context(patch.object(c, 'prefix', side_effect=lambda *args: (copy.deepcopy(observed), copy.deepcopy(adjudication))))
        stack.enter_context(patch.object(f, 'environment', return_value={'synthetic': 'unchanged'}))
        stack.enter_context(patch.object(c.subprocess, 'Popen', side_effect=popen))
        state.update(original=observed, original_copy=original_copy, output=output, visits=visits)
        return path, release, state

    def test_real_controller_107_once_and_completed_resume_zero_calls(self):
        with tempfile.TemporaryDirectory() as temp:
            path, release, state = self.owner(Path(temp))
            self.assertEqual(c.execute(path, release), 0)
            self.assertEqual(state['calls'], list(range(205, 312)))
            summary = c.read(state['output'] / 'summary.json')
            self.assertEqual([summary['results'][label]['cohort_cases'] for label in LABELS], [156, 156])
            self.assertEqual(summary['results'][LABELS[0]]['native_outcomes'], {'TIMEOUT': 155, 'ERROR': 1})
            self.assertEqual(summary['results'][LABELS[0]]['conditional_PAR2_s'], 156 * 60)
            self.assertEqual(summary['primary_observations'], 312)
            self.assertEqual(summary['diagnostic_scored_observations'], 0)
            self.assertEqual(summary['pair_gap'], c.GAP)
            self.assertEqual(state['original'], state['original_copy'])
            self.assertEqual(c.execute(path, release), 0)
            self.assertEqual(len(state['calls']), 107)

    def test_partial_attempt_and_cancellation_never_retry_or_launch_next(self):
        for mode in ['partial', 'cancel']:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                path, release, state = self.owner(Path(temp), mode)
                first = Path(state['visits'][0]['leg'])
                if mode == 'partial':
                    first.mkdir(parents=True); c.write(first / 'started.json', {'partial': True})
                    with self.assertRaisesRegex(ValueError, 'Partial continuation'): c.execute(path, release)
                    self.assertEqual(state['calls'], [])
                else:
                    with self.assertRaises(SystemExit): c.execute(path, release)
                    self.assertEqual(state['calls'], [205])
                    self.assertEqual((state['terminated'], state['cleanup_waits']), (1, 1))
                    self.assertTrue((first / 'started.json').exists())
                    self.assertFalse((first / 'record.json').exists())
                    with self.assertRaisesRegex(ValueError, 'Partial continuation'): c.execute(path, release)
                    self.assertEqual(state['calls'], [205])

    def test_unknown_or_wrong_identity_error_keeps_record_and_stops_before_next(self):
        for mode in ['unknown-error', 'identity-error']:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                path, release, state = self.owner(Path(temp), mode)
                with self.assertRaises(ValueError): c.execute(path, release)
                self.assertEqual(state['calls'], [205])
                leg = Path(state['visits'][0]['leg'])
                self.assertTrue((leg / 'record.json').exists())
                self.assertTrue((leg / 'capture/stderr.txt').exists())
                self.assertFalse((state['output'] / 'summary.json').exists())
                self.assertEqual(c.read(leg / 'record.json')['exit_code'], 0)

    def test_known_future_primary_error_stays_error_and_costs_60(self):
        with tempfile.TemporaryDirectory() as temp:
            path, release, state = self.owner(Path(temp), 'known-error')
            self.assertEqual(c.execute(path, release), 0)
            summary = c.read(state['output'] / 'summary.json')
            self.assertEqual(len(summary['adjudicated_library_errors']), 108)
            self.assertEqual(sum(panel['conditional_PAR2_s'] for panel in summary['results'].values()), 312 * 60)
            self.assertEqual(sum(panel['native_outcomes'].get('ERROR', 0) for panel in summary['results'].values()), 108)


if __name__ == '__main__':
    unittest.main()
