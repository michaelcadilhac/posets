#!/usr/bin/env python3
"""Mock one rejected invocation and verify halt/evidence; no real child work."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

HERE = Path(__file__).resolve().parent


def main():
    spec = importlib.util.spec_from_file_location('supervised_pairs', HERE / 'run-paired-replay-stop-on-correctness.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    ordinary = [{'phase': 'check', 'status': status} for status in
                ('pass', 'timeout', 'resource_limit', 'source_unverified')]
    assert runner.correctness_rejections({'rows': ordinary}) == []
    assert len(runner.correctness_rejections({'rows': ordinary + [
        {'phase': phase, 'status': 'rejected'} for phase in ('source_oracle', 'check', 'measure')]})) == 3
    with tempfile.TemporaryDirectory(prefix='posets-correctness-stop-') as temporary:
        root = Path(temporary)
        bound = root / 'bound.json'
        bound.write_text('{}')
        def binding(path):
            return {'path': str(path), 'sha256': runner.sha(path)}
        runs = []
        for pair, variant in [(0, 'baseline'), (0, 'optimized'), (1, 'optimized'),
                              (1, 'baseline'), (2, 'baseline'), (2, 'optimized')]:
            run_id = f'{pair}-{variant}'
            path = root / (run_id + '-spec.json')
            path.write_text(json.dumps({'source_revision': '1' * 40,
                'frozen_build_source_manifest': binding(bound), 'environment_file': str(bound)}))
            runs.append({'id': run_id, 'pair': pair, 'variant': variant, 'spec': binding(path),
                         'output': str(root / run_id), 'record': str(root / (run_id + '-record.json'))})
        plan_path = root / 'fixture-plan.json'
        plan_path.write_text(json.dumps({'orchestrator': binding(HERE / 'run-paired-replay-stop-on-correctness.py'),
            'driver': binding(bound), 'trace_plan': binding(bound), 'split': 'train', 'runs': runs,
            'variants': {v: {'source': str(root), 'revision': '1' * 40} for v in ('baseline', 'optimized')}}))
        calls = []
        def git(command, **kwargs):
            return '1' * 40 + '\n' if command[-2:] == ['rev-parse', 'HEAD'] else ''
        def invoke(command, **kwargs):
            calls.append(command)
            output = Path(runs[0]['output'])
            output.mkdir()
            report = {'provenance': {'posets_revision': '1' * 40, 'posets_dirty': ''},
                      'traces': [{'split': 'train'}],
                      'rows': ordinary + [{'phase': 'check', 'status': 'rejected', 'trace': 0}]}
            (output / 'report.json').write_text(json.dumps(report))
        with patch.object(runner, 'HERE', root), patch.object(runner, 'HostSampler', return_value=contextlib.nullcontext()), \
             patch.object(runner.campaign, 'HOST_LOCK', root / 'lock'), \
             patch.object(runner.campaign, 'running_scopes', return_value=[]), \
             patch.object(runner.subprocess, 'check_output', git), patch.object(runner.subprocess, 'run', invoke), \
             patch.object(sys, 'argv', ['run', str(plan_path)]), contextlib.redirect_stdout(io.StringIO()):
            try:
                runner.main()
                raise AssertionError('Rejected invocation continued')
            except SystemExit as error:
                assert error.code == 3
        assert len(calls) == 1
        state = json.loads((root / 'fixture-progress.json').read_text())
        assert state['status'] == 'stopped_correctness' and state['completed'] == []
        evidence = json.loads(Path(state['evidence']).read_text())
        assert evidence['rows'] == [{'phase': 'check', 'status': 'rejected', 'trace': 0}]
        assert (Path(runs[0]['output']) / 'report.json').is_file()
        assert (Path(runs[0]['output']) / 'build-source-binding.json').is_file()
    print('PASS: rejected source/check/measure stops before next invocation; ordinary timeout/OOM/unverified retained; report, binding and fatal rows preserved. Real child calls:0.')


if __name__ == '__main__':
    main()
