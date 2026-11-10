#!/usr/bin/env python3
"""Offline paired-map/timeout/missing-memory fixture; no solver or timing calls."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

HERE = Path(__file__).resolve().parent


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    writer = module('pair_writer', 'prepare-paired-replay.py')
    runner = module('pair_runner', 'run-paired-replay.py')
    summary = module('pair_summary', 'summarize-paired-replay.py')
    anchor = json.loads((HERE.parent / 'acacia-posets-eval/_deps/replay-baseline-7c40712/provenance.json').read_text())
    backends = anchor['modes']['compatible']['backends']
    with tempfile.TemporaryDirectory(prefix='posets-paired-maps-') as temporary:
        root = Path(temporary)
        campaign, repo = root / 'campaign', root / 'repo'
        campaign.mkdir()
        (repo / 'benchmarking').mkdir(parents=True)
        (repo / 'benchmarking/posets-matrix.py').write_text('# fixture, never executed\n')
        for name in ('run-paired-replay.py', 'pair0-baseline-spec.json'):
            (campaign / name).write_bytes((HERE / name).read_bytes())
        maps = []
        for label, index in [('baseline', 1), ('optimized', 2)]:
            source = repo / '_deps' / label
            source.mkdir(parents=True)
            oracle = repo / ('anchor-' + label) / 'tests/downset-matrix-compatible'
            oracle.parent.mkdir(parents=True)
            oracle.write_bytes(b'fixture oracle, never executed')
            saved_anchor = root / (label + '-anchor.json')
            metadata = copy.deepcopy(anchor)
            metadata['posets_revision'] = str(index + 2) * 40
            metadata['modes']['compatible']['binary'] = str(oracle)
            metadata['modes']['compatible']['binary_sha256'] = writer.binding(oracle)['sha256']
            saved_anchor.write_text(json.dumps(metadata))
            path = root / (label + '-map.json')
            path.write_text(json.dumps({'schema_version': 1, 'kind': 'posets-per-backend-binaries',
                'source_dir': str(source), 'source_revision': str(index) * 40,
                'base_revision': str(index + 2) * 40,
                'source_oracle_binary': writer.binding(oracle),
                'anchor_provenance': writer.binding(saved_anchor),
                'binaries': [{'mode': mode, 'backend': backend} for mode in ('compatible', 'native')
                             for backend in backends]}))
            maps.append(path)
        traces = root / 'traces.json'
        traces.write_text(json.dumps({'schema_version': 1, 'traces': [{'path': 'cpre-1.tsv', 'split': 'train'}]}))
        with patch.object(writer, 'HERE', campaign), patch.object(writer, 'REPO', repo):
            plan_path = writer.prepare(None, traces, 'fixture-paired', *maps)
        plan = json.loads(plan_path.read_text())
        cells = [(mode, backend) for mode in ('compatible', 'native') for backend in backends]
        calls = []

        def git(command, **kwargs):
            variant, = [v for v in plan['variants'].values() if v['source'] == command[2]]
            return variant['revision'] + '\n' if command[-2:] == ['rev-parse', 'HEAD'] else ''

        def scope(command, **kwargs):
            spec = json.loads(Path(command[3]).read_text())
            run, = [entry for entry in plan['runs'] if entry['spec']['path'] == command[3]]
            variant = plan['variants'][run['variant']]
            assert spec['command'][spec['command'].index('--build-dir') + 1] == variant['build']
            assert spec['command'][spec['command'].index('--binary-map') + 1] == variant['binary_map']['path']
            output = Path(run['output'])
            output.mkdir(parents=True)
            process = {'memory_peak_bytes': None, 'scope_memory_events': ''}
            rows = [{'trace': 0, 'phase': 'source_oracle', 'status': 'pass', 'process': process}]
            for mode, backend in cells:
                common = {'trace': 0, 'mode': mode, 'backend': backend, 'vector': 'simd_vector_backed'}
                rows.append({**common, 'phase': 'check', 'status': 'pass', 'process': process})
                timed_out = run['pair'] == 0 and run['variant'] == 'optimized' and (mode, backend) == cells[0]
                factor = 1 if run['variant'] == 'baseline' else 0.5
                rows.append({**common, 'phase': 'measure', 'status': 'timeout' if timed_out else 'pass',
                             'metrics': None if timed_out else {'cpre': 0.01 * factor,
                             'build': 0.02 * factor, 'rebuild': 0.03 * factor},
                             'process': process})
            report = {'provenance': {'posets_revision': variant['revision'], 'posets_dirty': '',
                       'base_revision': variant['production_base_revision'], 'binary_map': variant['binary_map']},
                      'traces': [{'id': 0, 'instance': 'fixture', 'kind': 'cpre', 'split': 'train',
                                  'features': {'maxima': 2, 'dimension': 3}}], 'rows': rows}
            (output / 'report.json').write_text(json.dumps(report))
            (output / 'measurement-order.json').write_text(json.dumps([
                {'trace': 0, 'mode': mode, 'backend': backend, 'vector': 'simd_vector_backed'}
                for mode, backend in cells]))
            calls.append(run['id'])

        with contextlib.redirect_stdout(io.StringIO()), patch.object(runner, 'HERE', campaign), \
             patch.object(runner, 'HostSampler', return_value=contextlib.nullcontext()), \
             patch.object(runner.campaign, 'HOST_LOCK', root / 'host.lock'), \
             patch.object(runner.campaign, 'running_scopes', return_value=[]), \
             patch.object(runner.subprocess, 'check_output', git), patch.object(runner.subprocess, 'run', scope), \
             patch.object(sys, 'argv', ['run', str(plan_path)]):
            runner.main()
        assert len(calls) == 6
        with contextlib.redirect_stdout(io.StringIO()), patch.object(sys, 'argv', ['summary', str(plan_path)]):
            summary.summarize()
        result = json.loads((campaign / 'fixture-paired-summary.json').read_text())
        assert len(result['cells']) == 144
        assert sum(row['status'] == 'censored' for row in result['cells']) == 3
        assert len(result['failure_rows']) == 1
        assert result['failure_rows'][0]['row']['status'] == 'timeout'
        assert result['failure_rows'][0]['row']['metrics'] is None
        assert all(row['max_scope_memory_peak_bytes'] is None and row['valid_scope_memory_peaks'] == 0
                   and row['executed_invocations'] == 97 for row in result['coverage'])
        censored = [row for row in result['cells'] if row['status'] == 'censored']
        assert all(row['pairs'][0]['optimized_seconds'] is None and
                   row['pairs'][0]['optimized_over_baseline'] is None and
                   'median_paired_optimized_over_baseline' not in row for row in censored)
        assert all(row['median_paired_optimized_over_baseline'] == 0.5
                   for row in result['cells'] if row['status'] == 'paired')
        changed_map = json.loads(maps[1].read_text())
        changed_anchor = root / 'optimized-anchor.json'
        differently_flagged = json.loads(changed_anchor.read_text())
        for row in differently_flagged['modes'].values():
            row['compiler_command']['command'] += ' -mtune=generic'
        changed_anchor.write_text(json.dumps(differently_flagged))
        changed_map['anchor_provenance'] = writer.binding(changed_anchor)
        maps[1].write_text(json.dumps(changed_map))
        try:
            with patch.object(writer, 'REPO', repo):
                writer.mapped_variants(*maps)
            raise AssertionError('Differently flagged anchors accepted')
        except AssertionError as error:
            assert str(error) == 'Baseline/candidate compiler, flags or width contexts differ'
        first_binding = Path(plan['runs'][0]['output']) / 'build-source-binding.json'
        wrong = json.loads(first_binding.read_text())
        wrong['binary_map']['sha256'] = '0' * 64
        first_binding.write_text(json.dumps(wrong))
        try:
            with contextlib.redirect_stdout(io.StringIO()), patch.object(sys, 'argv', ['summary', str(plan_path)]):
                summary.summarize()
            raise AssertionError('Mismatched map binding accepted')
        except AssertionError as error:
            assert str(error) != 'Mismatched map binding accepted'
    print('PASS: six mapped runs preserve production/measurement/map bindings and order; metrics=None timeout retained as3censored cells; missing memory remains null with0/97 coverage; no solver/timing calls.')


if __name__ == '__main__':
    main()
