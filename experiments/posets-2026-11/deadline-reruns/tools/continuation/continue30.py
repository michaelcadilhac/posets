#!/usr/bin/env python3
"""Preserve the 205-record prefix; run only its 107 original pending visits."""
import argparse
import csv
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import time

from capture_native import digest, require, write

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
BASE = HERE.parent
MAPPING = BASE / 'continuation-source-map-v1/MAPPING-CHECKPOINT.json'
MAPPING_SHA = '0c61aa1108c42ecc44d8e7eb9b125d583143b016c5df1a6f899da0971b89f5e3'
OLD_SHA = '111b5b54ca9274f538c9fdf1ab8dc5ff46614f17fc939487cb5c9a386e29c9c0'
V2_PATH = BASE / 'followup-30s-draft-v2/followup30.py'
V2_SHA = '6cdc4851af74b26b1a8b17e7deabf81c3297a3391f5162dd0df0159fa6b01acb'
FAILED_SHA = 'b5929f053491d1fd2da9c5432e2e23ec0adc1b820cad461bb63c23611c1b24e8'
DIAGNOSTIC_SHA = 'c96efc3998c5c8ec37204fc83b3d8b0986fff58a9a078d597d5b73676d5d65da'
SIGNATURE_SHA = 'a2656835dc796b43411dd0a30f4bef6e4e2d3e4f9b30ab40ccb810ad9b4e4667'
KIND = 'SPOT_ACCEPTANCE_LIMIT_32'
GAP = {'uninterrupted_prefix_pairs': 102, 'split_original_case_index': 102,
       'uninterrupted_suffix_pairs': 53,
       'limit': 'One pair spans STOP/diagnostic/adjudication; not all 156 pairs were uninterrupted adjacent.'}


def read(path): return json.loads(Path(path).read_text())
def bind(path):
    path = Path(path).resolve(strict=True)
    return {'path': str(path), 'sha256': digest(path)}
def verify(value):
    require(bind(value['path']) == value, 'Changed binding: ' + value['path'])
def old_module():
    require(digest(V2_PATH) == V2_SHA, 'Frozen V2 helper changed')
    spec = importlib.util.spec_from_file_location('frozen_followup_v2', V2_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def partition(old, completed, pending):
    require(len(old['cohort']) == 156 and len(old['visits']) == 312, 'Wrong original universe')
    require([x['original_visit_index'] for x in completed] == list(range(205)), 'Prefix reordered/missing/duplicated')
    require([x['original_visit_index'] for x in pending] == list(range(205, 312)), 'Suffix reordered/missing/duplicated')
    for item in completed + pending:
        require(item['original_visit'] == old['visits'][item['original_visit_index']], 'Changed original visit')
    require(old['visits'][204]['instance'] == old['visits'][205]['instance'] == 'ltl2dba_R_pb_12_pe_.ltl'
            and old['visits'][204]['label'] == 'anchor-e167' and old['visits'][205]['label'] == 'lazy-fc', 'Wrong split boundary')


def identity(f, row, arm, manifest, visit, tlsf):
    expected = {'solver_label': arm['label'], 'instance': visit['instance'], 'tlsf_file': tlsf,
                'cap_s': '30', 'binary_sha256': arm['binary_sha256'], 'acacia_sha': arm['acacia_sha'],
                'flags': arm['flags'], 'preset': arm['preset'], 'memory_max': '8G',
                'memory_swap_max': '0', 'collect_rusage': 'true',
                'allowed_cpus': manifest['allowed_cpus'] or '', 'cpu_quota': manifest['cpu_quota'] or ''}
    require(all(row[k] == v for k, v in expected.items()), 'Original identity/resource policy mismatch')
    if row['result'] in f.DECISIVE:
        require(row['exit_code'] == ('0' if row['result'] == 'REALIZABLE' else '1'), 'Verdict/exit mismatch')
    f.resource_guard([row])


def category(f, row, stdout=None, stderr=None, diagnostic=False):
    if row['result'] in f.ORDINARY:
        return None
    require(row['result'] == 'ERROR' and row['exit_code'] == '3' and row['timed_out'] == 'false'
            and row['resource_reason'] == '' and row['stdout_bytes'] == '0' and row['stderr_bytes'] == '1080',
            'Unknown/mixed native error')
    events = json.loads(row['scope_memory_events'])
    require(all(events[k] == 0 for k in ['oom', 'oom_kill', 'oom_group_kill']), 'Mixed cgroup/library error')
    if not diagnostic:
        require(stdout == '' and stderr is not None and stderr.encode() == (HERE / 'spot-limit32.stderr.txt').read_bytes(),
                'Unknown/mixed/incomplete error signature')
    return KIND


def project(old, pending, output):
    visits = []
    for item in pending:
        original = item['original_visit']; index = item['original_visit_index']
        leg = output / 'legs' / f'{index // 2:04d}' / original['label']
        case_list = output / 'lists' / f'{index // 2:04d}.list'
        native = list(original['command'])
        for flag, value in [('--list', str(case_list)), ('--output', str(leg / 'raw' / (original['label'] + '.tsv')))]:
            require(native.count(flag) == 1, 'Ambiguous original path option')
            native[native.index(flag) + 1] = value
        require(native[0:2] == original['command'][0:2] and native[1] == '-u', 'Interpreter mode changed')
        command = native[:2] + [str(HERE / 'capture_native.py'), str(leg / 'capture'), native[2],
                                old['manifest17']['implementation'][native[2]], '--'] + native[3:]
        visits.append({'original_visit_index': index, 'instance': original['instance'], 'label': original['label'],
                       'leg': str(leg), 'case_list': str(case_list), 'native_cli': native, 'command': command})
    return visits


def source_state():
    require(digest(MAPPING) == MAPPING_SHA and digest(HERE / 'spot-limit32.stderr.txt') == SIGNATURE_SHA, 'Mapping/signature source changed')
    mapping = read(MAPPING)
    for key in ['original_plan', 'failure', 'diagnostic', 'completed205', 'remaining107', 'owner_native_source', 'benchlib']:
        verify(mapping[key])
    require(mapping['original_plan']['sha256'] == OLD_SHA and mapping['diagnostic']['sha256'] == DIAGNOSTIC_SHA, 'Wrong primary/diagnostic')
    old = read(mapping['original_plan']['path']); completed = read(mapping['completed205']['path']); pending = read(mapping['remaining107']['path'])
    partition(old, completed, pending)
    f = old_module()
    require(old['writer'] == bind(V2_PATH) and old['policy'] == bind(f.POLICY) and old['interpreter'] == bind(sys.executable), 'Original owner identity changed')
    for value in old['bindings']: verify(value)
    campaign, benchlib, coverage, manifest, cohort, truths, scored, tlsf_map, files = f.load_parent(Path(old['parent17']))
    require(old['manifest17'] == manifest and old['cohort'] == cohort and old['truth17'] == truths and old['bindings'] == files, 'Original source/cohort changed')
    require(old['visits'] == f.commands(manifest, cohort, Path(mapping['original_plan']['path']).parent, campaign.command), 'Original commands changed')
    require(old['mapping'] == {name: tlsf_map[name] for name in cohort} and old['cap_s'] == 30 and old['unsolved_PAR2_s'] == 60, 'Original cap/mapping changed')
    require(old['scored17'] == {label: {r['instance']: r['result'] for r in rows if r['instance'] in cohort} for label, rows in scored.items()}, 'Original 17s scores changed')
    require(all(not Path(x['original_visit']['leg']).exists() for x in pending), 'Original suffix contains an attempted visit')
    for item in pending:
        verify(item['source_case_list'])
        require(Path(item['source_case_list']['path']).read_text() == item['instance'] + '\n', 'Original pending list changed')
    diagnostic = read(mapping['diagnostic']['path'])
    for value in diagnostic['bindings'] + diagnostic['verified_original_inputs']: verify(value)
    log = next(x for x in diagnostic['bindings'] if x['path'].endswith('reproduction-attempt1.log'))
    require(log['sha256'] == SIGNATURE_SHA and Path(log['path']).read_bytes() == (HERE / 'spot-limit32.stderr.txt').read_bytes(), 'Independent diagnostic signature differs')
    result = diagnostic['actual_result']
    require(result['returncode'] == 3 and not result['timed_out'] and not result['resource_limited']
            and result['stdout_bytes'] == 0 and result['stderr_bytes'] == 1080, 'Diagnostic not the known independent reproduction')
    require(all(json.loads(result['scope_memory_events'])[key] == 0 for key in ['oom', 'oom_kill', 'oom_group_kill']), 'Diagnostic has mixed OOM events')
    return f, campaign, benchlib, coverage, mapping, old, completed, pending


def read_row(f, coverage, leg, arm):
    raw = leg / 'raw' / (arm['label'] + '.tsv')
    rows = coverage.load_output(raw)
    require(len(rows) == 1, 'Expected one native primary observation')
    with Path(str(raw) + '.memory.tsv').open() as stream:
        f.join_guard(rows, list(csv.DictReader(stream, delimiter='\t')))
    conflicts = raw.with_name(arm['label'] + '-conflicts.tsv')
    require(not conflicts.exists() or not coverage.load_conflicts(conflicts), 'Collected truth conflict')
    return rows[0]


def prefix(f, coverage, mapping, old, completed):
    arms = {x['label']: x for x in old['manifest17']['arms']}
    observed = {label: [] for label in arms}; classifications = []
    for item in completed:
        for key in ['record', 'raw', 'memory']: verify(item[key])
        record = read(item['record']['path']); visit = item['original_visit']
        require(record['plan'] == mapping['original_plan'] and record['visit'] == visit and record['exit_code'] == 0, 'Changed prefix record/CLI exit')
        for value in record['files']: verify(value)
        require(Path(visit['case_list']).read_text() == visit['instance'] + '\n', 'Changed original case list')
        row = read_row(f, coverage, Path(visit['leg']), arms[visit['label']])
        require((row['result'], row['exit_code'], row['seconds']) == (item['native_result'], item['native_exit_code'], item['native_seconds']), 'Mapped prefix row changed')
        identity(f, row, arms[visit['label']], old['manifest17'], visit, old['mapping'][visit['instance']])
        known = item['original_visit_index'] == 204
        if known:
            require(item['record']['sha256'] == FAILED_SHA and row['result'] == 'ERROR', 'Wrong existing error adjudication')
        kind = category(f, row, diagnostic=known)
        if kind:
            classifications.append({'original_visit_index': 204, 'native_result': 'ERROR', 'classification': kind,
                                    'adjudication_source': 'separate diagnostic; original primary stderr was not recovered',
                                    'primary_record': item['record'], 'diagnostic': mapping['diagnostic'], 'PAR2_s': 60})
        observed[visit['label']].append(row)
    require(len(classifications) == 1, 'Expected exactly one original diagnosed ERROR')
    return observed, classifications


def capture_result(f, row, capture, arm, manifest, tlsf):
    proof = read(capture / 'capture.json')
    require(proof['calls'] == 1, 'Capture invocation count differs')
    require(proof['files'] == [bind(capture / name) for name in ['stdout.txt', 'stderr.txt', 'result.json']], 'Capture file inventory differs')
    expected = {'args': [[arm['binary'], *shlex.split(arm['flags']), '-T', str((Path(manifest['tlsf_corpus']) / tlsf).resolve(strict=True))]],
                'kwargs': {'timeout': 30, 'memory_max': '8G', 'memory_swap_max': '0', 'allowed_cpus': None,
                           'cpu_quota': None, 'unit_prefix': 'acacia-syntcomp26-coverage', 'env': None}}
    require(proof['args'] == expected['args'] and proof['kwargs'] == expected['kwargs'], 'Native scoped delegation changed')
    meta = read(capture / 'result.json'); stdout = (capture / 'stdout.txt').read_text(); stderr = (capture / 'stderr.txt').read_text()
    for key, data in [('stdout_bytes', stdout), ('stderr_bytes', stderr)]:
        require(type(meta[key]) is int and meta[key] == len(data.encode()) == int(row[key]), 'Incomplete stream capture')
    for native, field in [('exit_code', 'returncode'), ('seconds', 'seconds'),
                          ('scope_unit', 'scope_unit'), ('scope_memory_peak_bytes', 'memory_peak_bytes'),
                          ('scope_memory_events', 'scope_memory_events')]:
        require(row[native] == ('' if meta[field] is None else str(meta[field])), 'Capture/native result metadata differs')
    # Mirror the unchanged native CLI's six-decimal CPU token and optional rusage override.
    usage = re.search(r'^ACACIA_RUSAGE ([0-9.]+) ([0-9.]+) ([0-9]+)$', stderr, re.M)
    cpu = float(usage[1]) + float(usage[2]) if usage else meta['cpu_seconds']
    require(row['cpu_seconds'] == ('' if cpu is None else format(cpu, '.6f')), 'Capture/native CPU differs')
    rss = int(usage[3]) * 1024 if usage else meta['max_process_rss_bytes']
    require(row['max_process_rss_bytes'] == ('' if rss is None else str(rss)), 'Capture/native RSS differs')
    require(type(meta['timed_out']) is bool and row['timed_out'] == str(meta['timed_out']).lower(), 'Capture/native timeout mismatch')
    if row['result'] == 'ERROR': require(not meta['resource_limited'], 'Mixed native resource/error')
    return category(f, row, stdout, stderr)


def prepare(output):
    require(output.parent == BASE and not output.exists(), 'Need a fresh direct-child task-only continuation directory')
    f, campaign, benchlib, coverage, mapping, old, completed, pending = source_state()
    observed, classifications = prefix(f, coverage, mapping, old, completed)
    output.mkdir(parents=True); (output / 'lists').mkdir()
    visits = project(old, pending, output)
    for visit in visits:
        path = Path(visit['case_list'])
        if not path.exists():
            with path.open('x') as stream: stream.write(visit['instance'] + '\n')
    plan = {'status': 'prepared amendment; exact root adjudication/release required before any remaining call',
            'source_mapping': bind(MAPPING), 'original_plan': mapping['original_plan'],
            'writer': bind(Path(__file__)), 'adapter': bind(HERE / 'capture_native.py'), 'signature': bind(HERE / 'spot-limit32.stderr.txt'),
            'original_record_count': 205, 'new_visit_count': 107, 'total_observations': 312,
            'adjudication': classifications, 'pair_gap': GAP, 'visits': visits,
            'policy': 'Keep every native result/exit/time/resource unchanged. Exact verified Spot-limit ERROR is separate and unsolved/PAR2=60; unknown or mixed errors STOP.',
            'limits': 'Not a fresh full run. Diagnostic retry is external and never replaces a primary record. No source, ELF, Spot, timing, flag or cgroup change.'}
    write(output / 'amendment.json', plan)


def execute(path, release):
    plan = read(path); output = path.parent
    require(plan['writer'] == bind(Path(__file__)) and plan['adapter'] == bind(HERE / 'capture_native.py')
            and plan['signature'] == bind(HERE / 'spot-limit32.stderr.txt') and plan['source_mapping'] == bind(MAPPING), 'Continuation source changed')
    approval = read(release)
    require(approval['amendment'] == bind(path) and approval['accepted_error_category'] == KIND
            and approval['max_new_solver_calls'] == 107, 'Exact adjudication/release absent')
    f, campaign, benchlib, coverage, mapping, old, completed, pending = source_state()
    observed, classifications = prefix(f, coverage, mapping, old, completed)
    require(plan['original_plan'] == mapping['original_plan'] and plan['adjudication'] == classifications
            and plan['pair_gap'] == GAP and plan['visits'] == project(old, pending, output)
            and (plan['original_record_count'], plan['new_visit_count'], plan['total_observations']) == (205, 107, 312), 'Amendment changed')
    manifest = old['manifest17']; arms = {x['label']: x for x in manifest['arms']}; truths = dict(old['truth17'])
    for rows in observed.values():
        for row in rows: f.observe_truth(truths, row)
    env = f.environment(manifest)
    with Path(manifest['host_lock']).open('a') as host, (output / 'driver.lock').open('a') as local:
        try:
            fcntl.flock(host, fcntl.LOCK_EX | fcntl.LOCK_NB); fcntl.flock(local, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError: return 75
        frozen = {'amendment': bind(path), 'root_release': bind(release), 'adjudication': classifications, 'pair_gap': GAP}
        if (output / 'frozen.json').exists(): require(read(output / 'frozen.json') == frozen, 'Changed resume authority')
        else: write(output / 'frozen.json', frozen)
        with benchlib.HostSampler(output / 'thermal.tsv', interval=30) as sampler:
            for visit in plan['visits']:
                for key in ['writer', 'adapter', 'signature']: verify(plan[key])
                leg = Path(visit['leg']); record_path = leg / 'record.json'; arm = arms[visit['label']]
                require(Path(visit['case_list']).read_text() == visit['instance'] + '\n', 'Changed continuation list')
                if record_path.exists():
                    record = read(record_path)
                    require(record['amendment'] == frozen['amendment'] and record['visit'] == visit, 'Changed completed continuation attempt')
                    for value in record['files']: verify(value)
                else:
                    require(not leg.exists(), 'Partial continuation attempt preserved; no retry')
                    if campaign.running_scopes(): return 75
                    campaign.verify_files(manifest, arm)
                    for value in old['bindings']: verify(value)
                    leg.mkdir(parents=True); (leg / 'raw').mkdir()
                    write(leg / 'started.json', {'amendment': frozen['amendment'], 'visit': visit})
                    sampler.set_leg(str(visit['original_visit_index']) + ':' + visit['label'], campaign.raw_path(leg, arm))
                    with (leg / 'stdout.log').open('x') as stream:
                        child = subprocess.Popen(visit['command'], cwd=manifest['repo'], env=env, stdout=stream, stderr=subprocess.STDOUT)
                        try: code = child.wait()
                        except BaseException:
                            child.terminate(); child.wait(timeout=20); raise
                        stream.flush(); os.fsync(stream.fileno())
                    record = {'amendment': frozen['amendment'], 'visit': visit, 'exit_code': code,
                              'files': [bind(p) for p in sorted(leg.rglob('*')) if p.is_file()]}
                    write(record_path, record)
                require(record['exit_code'] == 0, 'Native CLI/capture failed; attempt preserved')
                row = read_row(f, coverage, leg, arm)
                identity(f, row, arm, manifest, visit, old['mapping'][visit['instance']])
                kind = capture_result(f, row, leg / 'capture', arm, manifest, old['mapping'][visit['instance']])
                f.observe_truth(truths, row)
                if kind:
                    classifications.append({'original_visit_index': visit['original_visit_index'], 'native_result': 'ERROR',
                                            'classification': kind, 'adjudication_source': 'this primary capture',
                                            'primary_record': bind(record_path), 'PAR2_s': 60})
                observed[visit['label']].append(row)
                campaign.atomic_json(output / 'progress.json', {'status': 'running', 'carried': 205,
                                      'completed_new': sum(len(x) for x in observed.values()) - 205, 'total_new': 107})
        campaign.verify_files(manifest)
        for key in ['writer', 'adapter', 'signature']: verify(plan[key])
        for value in old['bindings']: verify(value)
        prefix(f, coverage, mapping, old, completed)
        summary = f.report(old, observed)
        require(all(len(rows) == 156 for rows in observed.values()), 'Incomplete composite arm')
        summary.update(original_records=205, continued_records=107, primary_observations=312,
                       adjudicated_library_errors=classifications, pair_gap=GAP,
                       diagnostic_scored_observations=0, root_release=bind(release))
        if (output / 'summary.json').exists(): require(read(output / 'summary.json') == summary, 'Changed final summary')
        else: write(output / 'summary.json', summary)
        campaign.atomic_json(output / 'progress.json', {'status': 'complete', 'carried': 205, 'completed_new': 107, 'total_new': 107})
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--prepare', type=Path); mode.add_argument('--run', type=Path)
    parser.add_argument('--release', type=Path)
    args = parser.parse_args()
    if args.prepare:
        require(args.release is None, 'Preparation cannot release targets')
        prepare(args.prepare.resolve()); return 0
    require(args.release is not None, 'Exact root release required')
    try: return execute(args.run.resolve(), args.release.resolve())
    except BaseException as error:
        write(args.run.resolve().parent / f'STOP-{time.time_ns()}.json', {'type': type(error).__name__, 'error': str(error)})
        raise


if __name__ == '__main__':
    def interrupted(signum, _frame): raise SystemExit(128 + signum)
    for handled in [signal.SIGTERM, signal.SIGHUP]: signal.signal(handled, interrupted)
    raise SystemExit(main())
