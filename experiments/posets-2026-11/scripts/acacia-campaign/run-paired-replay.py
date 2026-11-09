#!/usr/bin/env python3
"""Run a frozen paired replay plan through the existing scoped CLI."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import collections
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import campaign
sys.path.insert(0, str(HERE.parent / 'acacia-posets-eval/benchmarking'))
from benchlib import HostSampler


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify(binding):
    assert sha(binding['path']) == binding['sha256'], binding


def main():
    plan_path = Path(sys.argv[1]).resolve(strict=True)
    plan = json.loads(plan_path.read_text())
    verify(plan['orchestrator'])
    prefix = plan_path.with_name(plan_path.stem.removesuffix('-plan'))
    verify(plan['driver'])
    verify(plan['trace_plan'])
    assert [(run['pair'], run['variant']) for run in plan['runs']] == [
        (0, 'baseline'), (0, 'optimized'), (1, 'optimized'),
        (1, 'baseline'), (2, 'baseline'), (2, 'optimized')]
    assert all(not name.startswith(('ACACIA_DIAG', 'ACACIA_PHASE_RECORDS',
                                    'ACACIA_OUTER_DEADLINE_MONOTONIC')) for name in os.environ)
    completed, audits = [], []
    with campaign.HOST_LOCK.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with HostSampler(prefix.with_name(prefix.name + '-thermal.tsv'), interval=30):
            for run in plan['runs']:
                scopes = campaign.running_scopes()
                if scopes:
                    campaign.atomic_json(prefix.with_name(prefix.name + '-progress.json'),
                                         {'status': 'paused_contention', 'scopes': scopes,
                                          'completed': completed})
                    raise SystemExit(75)
                verify(plan['driver'])
                verify(plan['trace_plan'])
                verify(run['spec'])
                spec = json.loads(Path(run['spec']['path']).read_text())
                verify(spec['frozen_build_source_manifest'])
                variant = plan['variants'][run['variant']]
                assert spec['source_revision'] == variant['revision']
                if variant.get('binary_map'):
                    verify(variant['binary_map'])
                    assert spec['binary_map'] == variant['binary_map']
                    assert spec['production_base_revision'] == variant['production_base_revision']
                    assert spec['command'][spec['command'].index('--binary-map') + 1] == variant['binary_map']['path']
                source_revision = subprocess.check_output(
                    ['git', '-C', variant['source'], 'rev-parse', 'HEAD'], text=True).strip()
                assert source_revision == variant['revision']
                assert not subprocess.check_output(
                    ['git', '-C', variant['source'], 'status', '--porcelain'], text=True)
                output = Path(run['output'])
                assert not output.exists(), 'Fresh outputs required; preserve partial evidence before rescheduling'
                campaign.atomic_json(prefix.with_name(prefix.name + '-progress.json'),
                                     {'status': 'running', 'current': run['id'], 'completed': completed})
                print('Starting ' + run['id'], flush=True)
                subprocess.run([sys.executable, '-B', str(HERE / 'scoped_task.py'),
                                run['spec']['path'], run['record']], check=True)
                report_path = output / 'report.json'
                report = json.loads(report_path.read_text())
                assert report['provenance']['posets_revision'] == variant['revision']
                assert report['provenance']['posets_dirty'] == ''
                if variant.get('binary_map'):
                    assert report['provenance']['binary_map'] == variant['binary_map']
                    assert report['provenance']['base_revision'] == variant['production_base_revision']
                assert all(trace['split'] == plan['split'] for trace in report['traces'])
                campaign.atomic_json(output / 'build-source-binding.json', {
                    'paired_plan': {'path': str(plan_path), 'sha256': sha(plan_path)},
                    'run': run, 'source_directory': variant['source'],
                    'source_revision': variant['revision'],
                    'production_base_revision': variant.get('production_base_revision', variant['revision']),
                    'binary_map': variant.get('binary_map'),
                    'frozen_build_source_manifest': spec['frozen_build_source_manifest'],
                    'driver': plan['driver'], 'build_environment': {
                        'path': spec['environment_file'], 'sha256': sha(spec['environment_file'])},
                    'report_sha256': sha(report_path),
                    'note': 'Verification/process overhead is separate from operation clocks.'})
                completed.append(run['id'])
                counts = {phase: dict(collections.Counter(row['status'] for row in report['rows']
                          if row['phase'] == phase)) for phase in ('source_oracle', 'check', 'measure')}
                print(json.dumps({'completed': run['id'], 'counts': counts}), flush=True)
                pair_outputs = {entry['variant']: Path(entry['output']) for entry in plan['runs']
                                if entry['pair'] == run['pair'] and entry['id'] in completed}
                if len(pair_outputs) == 2:
                    orders = {name: [tuple(row[field] for field in
                              ('trace', 'mode', 'backend', 'vector')) for row in json.loads(
                              (path / 'measurement-order.json').read_text())]
                              for name, path in pair_outputs.items()}
                    common = set(orders['baseline']) & set(orders['optimized'])
                    equal = [key for key in orders['baseline'] if key in common] == [
                             key for key in orders['optimized'] if key in common]
                    audit = {'pair': run['pair'], 'common_cells': len(common),
                             'common_relative_order_equal': equal,
                             'baseline_only': sorted(set(orders['baseline']) - common),
                             'optimized_only': sorted(set(orders['optimized']) - common)}
                    audits.append(audit)
                    campaign.atomic_json(prefix.with_name(prefix.name + '-order-audit.json'), audits)
                    assert equal, 'Common cell order changed; retain evidence and halt'
            campaign.atomic_json(prefix.with_name(prefix.name + '-progress.json'),
                                 {'status': 'complete', 'completed': completed, 'order_audits': audits})
    print('Paired replay plan complete; host window released', flush=True)


if __name__ == '__main__':
    main()
