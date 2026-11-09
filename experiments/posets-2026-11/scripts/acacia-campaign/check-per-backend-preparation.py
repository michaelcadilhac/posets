#!/usr/bin/env python3
"""Offline argv/provenance/resume selfcheck; all external observations are mocked."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import shlex
import subprocess
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
    gen = module('selector_prepare', 'prepare-per-backend-build.py')
    run = module('selector_build', 'run-per-backend-build.py')
    anchor = json.loads((HERE.parent / 'acacia-posets-eval/_deps/replay-baseline-7c40712/provenance.json').read_text())
    with tempfile.TemporaryDirectory(prefix='posets-build-prep-') as temporary:
        root = Path(temporary)
        repo = root / 'repo'
        source = repo / '_deps/source'
        build = source / 'build-replay-release'
        build.mkdir(parents=True)
        (source / 'tests').mkdir()
        overlay = root / 'selector.patch'
        overlay.write_bytes(b'fixed selector-only patch\n')
        anchor_path = root / 'anchor.json'
        anchor_path.write_text(json.dumps(anchor))
        commands = [copy.deepcopy(anchor['modes'][mode]['compiler_command']) for mode in ('compatible', 'native')]
        for row in commands:
            row['directory'] = str(build)
        (build / 'compile_commands.json').write_text(json.dumps(commands))
        calls, fail_next = [], [True]

        def observe(command, **kwargs):
            if command[:3] == ['git', 'status', '--porcelain'] or command[-2:] == ['status', '--porcelain']:
                return b''
            if 'rev-parse' in command:
                return '2' * 40 + '\n'
            if '--name-only' in command:
                return 'tests/downset-bm.cc\n'
            if '--binary' in command:
                return overlay.read_bytes()
            assert command[0] == 'ninja', command
            mode = command[-1].removeprefix('tests/downset-matrix-')
            template, = [row for row in commands if f'-{mode}.p/' in row['output']]
            return template['command'] + '\n' + shlex.join([
                'c++', '-o', f'tests/downset-matrix-{mode}', template['output'],
                '-flto=1', '-Wl,--as-needed', '-Wl,--no-undefined', '-Wl,-O1']) + '\n'

        def execute(command, **kwargs):
            assert Path(command[2]).name == 'scoped_task.py', command
            spec = json.loads(Path(command[3]).read_text())
            record = Path(command[4])
            assert not record.exists(), 'Attempt evidence would be overwritten'
            calls.append(command)
            if fail_next[0]:
                fail_next[0] = False
                record.write_text('{"failed": true}\n')
                raise subprocess.CalledProcessError(1, command)
            record.write_text(json.dumps({'spec': spec, 'spec_sha256': run.binding(Path(command[3]))['sha256'],
                                         'result': {'returncode': 0, 'timed_out': False,
                                                    'resource_limited': False}}))
            if '-c' not in spec['command']:
                output = Path(spec['command'][spec['command'].index('-o') + 1])
                output.write_bytes(('placeholder ELF ' + output.name).encode())

        with patch.object(gen, 'REPO', repo), patch.object(gen.subprocess, 'check_output', observe), \
             patch.object(run.subprocess, 'run', execute), patch.object(run.campaign, 'HOST_LOCK', root / 'lock'), \
             patch.object(run.campaign, 'running_scopes', return_value=[]), contextlib.redirect_stdout(io.StringIO()):
            args = ['prepare', '--source', str(source), '--base-revision', anchor['posets_revision'],
                    '--selector-overlay', str(overlay), '--anchor-provenance', str(anchor_path),
                    '--output', str(build / 'per-backend')]
            with patch.object(sys, 'argv', args):
                gen.main()
            plan_path = build / 'per-backend/build-plan.json'
            plan = json.loads(plan_path.read_text())
            assert len(plan['rows']) == 48 and plan['backend_order'] == anchor['modes']['compatible']['backends']
            duplicate = copy.deepcopy(plan)
            duplicate['rows'][1] = duplicate['rows'][0]
            try:
                run.check_inventory(duplicate)
                raise AssertionError('Duplicate/missing backend accepted')
            except AssertionError as error:
                assert str(error) != 'Duplicate/missing backend accepted'
            for row in plan['rows']:
                actual = row['compiler_command']['arguments']
                old = gen.argv(row['template_compiler_command'])
                selector, = [part for part in actual if part.startswith('-DPOSETS_MATRIX_BACKEND=')]
                normalized = [part for part in actual if part != selector]
                for flag in ('-o', '-MF', '-MQ'):
                    normalized[normalized.index(flag) + 1] = old[old.index(flag) + 1]
                assert normalized == old
            orphan = root / 'interrupted.json'
            orphan.with_suffix('.log').write_text('keep interrupted log')
            assert run.fresh_attempt(orphan).stem == 'interrupted-attempt1'
            assert orphan.with_suffix('.log').read_text() == 'keep interrupted log'
            invalid = copy.deepcopy(plan['rows'][0])
            invalid['compiler_command']['arguments'].append('-O2')
            try:
                run.verify_row(plan, invalid)
                raise AssertionError('Extra compiler option accepted')
            except AssertionError as error:
                assert str(error) != 'Extra compiler option accepted'
            bad_anchor = copy.deepcopy(anchor)
            bad_anchor['modes']['max']['alignment'] += 1
            try:
                gen.check_anchor(bad_anchor)
                raise AssertionError('Invalid native/max alias accepted')
            except AssertionError as error:
                assert str(error) != 'Invalid native/max alias accepted'
            pilot = ['build', str(plan_path), '--backend', 'kdtree_backed']
            try:
                with patch.object(sys, 'argv', pilot):
                    run.main()
                raise AssertionError('Injected failure did not stop build')
            except subprocess.CalledProcessError:
                pass
            failed, = list((build / 'per-backend/records').glob('*-record.json'))
            failed_bytes = failed.read_bytes()
            with patch.object(sys, 'argv', pilot):
                run.main()
            assert not (build / 'per-backend/binary-map.json').exists()
            after_pilot = len(calls)
            with patch.object(sys, 'argv', pilot):
                run.main()
            assert len(calls) == after_pilot
            with patch.object(sys, 'argv', ['build', str(plan_path)]):
                run.main()
            binary_map = json.loads((build / 'per-backend/binary-map.json').read_text())
            assert len(binary_map['binaries']) == 48 and binary_map['aliases'] == {'max': 'native'}
            assert len(calls) == 97 and failed.read_bytes() == failed_bytes
            assert failed.with_stem(failed.stem + '-attempt1').is_file()
            before_resume = len(calls)
            with patch.object(sys, 'argv', ['build', str(plan_path)]):
                run.main()
            assert len(calls) == before_resume
            before = plan_path.read_bytes()
            try:
                with patch.object(sys, 'argv', args):
                    gen.main()
                raise AssertionError('Existing plan was overwritten')
            except AssertionError as error:
                assert 'overwrite' in str(error)
            assert plan_path.read_bytes() == before
    print('PASS:48 scoped-spec cells, exact selector/argv deltas, stopped failure, preserved retry evidence, pilot/full resume, complete48-entry map, no overwrite. External compiler/Ninja/Git/ELF calls:0.')
    print(json.dumps({'tested_sources': [run.binding(HERE / name) for name in
          ('prepare-per-backend-build.py', 'run-per-backend-build.py', 'check-per-backend-preparation.py')]}))


if __name__ == '__main__':
    main()
