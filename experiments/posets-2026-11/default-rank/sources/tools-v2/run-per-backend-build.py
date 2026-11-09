#!/usr/bin/env python3
"""Execute prepared selector compile/link specs serially; retain every attempt."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import argparse
import fcntl
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import rank_contract as contract
sys.path.insert(0, str(contract.SUPPORT))
import campaign
_spec = importlib.util.spec_from_file_location('selector_prepare', HERE / 'prepare-per-backend-build.py')
prepare = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(prepare)


def binding(path):
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def verify(item):
    assert binding(Path(item['path']))['sha256'] == item['sha256'], item


def fresh_attempt(path):
    candidate, attempt = path, 1
    while any(candidate.with_suffix(suffix).exists() for suffix in
              ('.json', '.log', '.helper.py', '.started')):
        candidate = path.with_stem(path.stem + f'-attempt{attempt}')
        attempt += 1
    candidate.with_suffix('.started').touch(exist_ok=False)
    return candidate


def verify_spec(row, stage):
    verify(stage['spec'])
    spec = json.loads(Path(stage['spec']['path']).read_text())
    expected = row[{'compile': 'compiler_command', 'link': 'linker_command'}[stage['stage']]]
    assert spec['command'] == expected['arguments']
    assert Path(spec['cwd']).resolve() == Path(expected['directory']).resolve()
    assert spec.get('expected_exit_codes', [0]) == [0]
    return spec


def verify_row(plan, row):
    template = row['template_compiler_command']
    original = prepare.argv(template)
    obj = Path(row['compiler_command']['output'])
    binary = Path(row['binary'])
    source = Path(plan['source_dir'])
    assert Path(template['directory']).resolve() == source / 'build-replay-release'
    assert (Path(template['directory']) / template['file']).resolve() == source / 'tests/downset-bm.cc'
    assert row['backend'] in prepare.BACKENDS
    selector = prepare.ALIASES.get(row['backend'], 'posets::downsets::' + row['backend'])
    expected = prepare.redirected(original, {'-o': obj, '-MF': str(obj) + '.d', '-MQ': obj}, selector)
    assert row['compiler_command']['arguments'] == expected
    assert row['compiler_command']['directory'] == template['directory']
    assert row['compiler_command']['file'] == template['file']
    old_object = original[original.index('-o') + 1]
    link_template = row['template_linker_command']
    expected_link = prepare.redirected(prepare.argv(link_template), {'-o': binary})
    assert expected_link.count(old_object) == 1
    expected_link[expected_link.index(old_object)] = str(obj)
    assert row['linker_command']['arguments'] == expected_link
    assert row['linker_command']['output'] == str(binary)
    assert row['linker_command']['directory'] == template['directory'] == link_template['directory']


def verify_invocations(row, previous):
    assert [item['stage'] for item in previous['invocations']] == ['compile', 'link']
    for stage, invocation in zip(row['stages'], previous['invocations']):
        verify(invocation)
        saved = json.loads(Path(invocation['path']).read_text())
        assert saved['spec'] == verify_spec(row, stage)
        assert saved['spec_sha256'] == stage['spec']['sha256']
        assert saved['result']['returncode'] == 0
        assert not saved['result']['timed_out'] and not saved['result']['resource_limited']


def expected_record(plan, row):
    return {'schema_version': 1, 'kind': 'posets-selector-build',
            **{name: plan[name] for name in ('base_revision', 'source_revision', 'source_dir', 'compile_commands')},
            **contract.source_fields(plan),
            **{name: row[name] for name in ('mode', 'backend', 'compiler_command', 'linker_command',
                                           'template_compiler_command', 'template_linker_command')}}


def check_inventory(plan, rank_only=False):
    order = contract.check_cells(plan, 'rows', rank_only, prepare.BACKENDS)
    total = 2 if rank_only else 48
    assert plan['backend_order'] == order
    assert [(row['mode'], row['backend']) for row in plan['rows']] == [
        (mode, backend) for mode in ('compatible', 'native') for backend in order]
    for field in ('binary', 'build_record'):
        assert len({row[field] for row in plan['rows']}) == total
    assert len({row['compiler_command']['output'] for row in plan['rows']}) == total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('--backend', help='Build pilot backend only; final map still requires all48 cells')
    parser.add_argument('--rank-only', action='store_true')
    args = parser.parse_args()
    plan_path = args.plan.resolve(strict=True)
    plan = json.loads(plan_path.read_text())
    check_inventory(plan, args.rank_only)
    total = 2 if args.rank_only else 48
    assert args.backend is None or args.backend in plan['backend_order']
    source_names = ('rank_source_manifest', 'rank_contract') if args.rank_only else ('selector_overlay',)
    for name in ('generator', 'anchor_provenance', 'compile_commands') + source_names:
        verify(plan[name])
    if args.rank_only:
        assert plan['rank_contract'] == binding(Path(contract.__file__).resolve())
        contract.validate_source(Path(plan['source_dir']), plan['base_revision'], plan['source_revision'],
                                 plan['rank_source_role'], Path(plan['rank_source_manifest']['path']))
    assert subprocess.check_output(['git', '-C', plan['source_dir'], 'rev-parse', 'HEAD'], text=True).strip() == plan['source_revision']
    assert not subprocess.check_output(['git', '-C', plan['source_dir'], 'status', '--porcelain'])
    compiler_templates = json.loads(Path(plan['compile_commands']['path']).read_text())
    for row in plan['rows']:
        assert row['template_compiler_command'] in compiler_templates
        verify_row(plan, row)
        for stage in row['stages']:
            verify_spec(row, stage)
    with campaign.HOST_LOCK.open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for row in plan['rows']:
            if args.backend and row['backend'] != args.backend:
                continue
            record_path = Path(row['build_record'])
            if record_path.exists():
                previous = json.loads(record_path.read_text())
                assert all(previous[name] == value for name, value in expected_record(plan, row).items())
                assert previous['binary_sha256'] == binding(Path(row['binary']))['sha256']
                verify_invocations(row, previous)
                continue
            invocations = []
            for stage in row['stages']:
                scopes = campaign.running_scopes()
                if scopes:
                    campaign.atomic_json(plan_path.with_name('build-progress.json'),
                                         {'status': 'paused_contention', 'scopes': scopes})
                    raise SystemExit(75)
                verify_spec(row, stage)
                verify(plan['compile_commands'])
                record = fresh_attempt(Path(stage['record']))
                print(f"{row['mode']} {row['backend']} {stage['stage']}", flush=True)
                subprocess.run([sys.executable, '-B', str(contract.SUPPORT / 'scoped_task.py'),
                                stage['spec']['path'], str(record)], check=True)
                invocations.append({'stage': stage['stage'], **binding(record)})
            payload = {**expected_record(plan, row), 'binary_sha256': binding(Path(row['binary']))['sha256'],
                       'invocations': invocations, 'build_plan': binding(plan_path),
                       'builder': binding(Path(__file__).resolve())}
            verify_invocations(row, payload)
            campaign.atomic_json(record_path, payload)
            print(json.dumps({'built': row['backend'], 'mode': row['mode'],
                              'binary_sha256': payload['binary_sha256']}), flush=True)
        completed = [row for row in plan['rows'] if Path(row['build_record']).is_file()]
        campaign.atomic_json(plan_path.with_name('build-progress.json'),
                             {'status': 'complete' if len(completed) == total else 'pilot_complete',
                              'completed': len(completed), 'total': total})
        if len(completed) == total:
            entries = []
            for row in plan['rows']:
                record = json.loads(Path(row['build_record']).read_text())
                assert record['binary_sha256'] == binding(Path(row['binary']))['sha256']
                entries.append({'mode': row['mode'], 'backend': row['backend'], 'binary': row['binary'],
                                'binary_sha256': record['binary_sha256'],
                                'build_record': binding(Path(row['build_record']))})
            binary_map = {'schema_version': 1, 'kind': 'posets-per-backend-binaries',
                          **{name: plan[name] for name in ('base_revision', 'source_revision', 'source_dir',
                             'anchor_provenance', 'source_oracle_binary', 'aliases') + source_names},
                          **({'inventory': contract.INVENTORY, 'rank_source_role': plan['rank_source_role']}
                             if args.rank_only else {}),
                          'binaries': entries}
            path = plan_path.with_name('binary-map.json')
            if path.exists():
                assert json.loads(path.read_text()) == binary_map
            else:
                campaign.atomic_json(path, binary_map)
            print(f'Complete{total}-cell map: ' + str(path), flush=True)


if __name__ == '__main__':
    main()
