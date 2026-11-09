#!/usr/bin/env python3
"""Prepare scoped compile/link specs for the unchanged per-backend replay TU."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
import rank_contract as contract
REPO = contract.REPO
ALIASES = {
    'lazy_kdtree_backed': 'lazy_kd_cascaded', 'lazy_kdtree_uncascaded': 'lazy_kd_uncascaded',
    'block_sharingtrie_backed': 'block_b8_simd',
    'block_sharingtrie_b1_scalar': 'block_b1_scalar',
    'block_sharingtrie_b4_scalar': 'block_b4_scalar',
    'block_sharingtrie_b4_simd': 'block_b4_simd',
    'block_sharingtrie_b8_scalar': 'block_b8_scalar',
    'block_sharingtrie_b16_scalar': 'block_b16_scalar',
    'block_sharingtrie_b16_simd': 'block_b16_simd',
    'block_sharingtrie_b8_simd_nocache': 'block_b8_nocache',
}
BACKENDS = set(ALIASES) | {
    'bboxtree_backed', 'comptrie_backed', 'kdtree_backed', 'vector_or_kdtree_backed',
    'vector_backed', 'rank_bucketed_vector_backed', 'filtered_vector_backed',
    'vector_backed_bin', 'vector_backed_one_dim_split_intersection_only',
    'sharingtree_backed', 'simple_sharingtree_backed', 'sharingtrie_backed',
    'skiplist_backed', 'cst_backed',
}


def check_anchor(anchor):
    backends = anchor['modes']['compatible']['backends']
    assert len(backends) == 24 and set(backends) == BACKENDS
    assert backends == sorted(BACKENDS)
    for mode in ('compatible', 'native', 'max'):
        assert anchor['modes'][mode]['backends'] == backends
    native, maximum = (anchor['modes'][mode] for mode in ('native', 'max'))
    fields = ('selected_lanes', 'simd_type', 'alignment', 'element_bytes', 'native_lanes',
              'max_fixed_lanes', 'comparison_instructions_sha256', 'hybrid_policy')
    assert all(native[field] == maximum[field] for field in fields)
    assert native['selected_lanes'] == native['native_lanes'] == native['max_fixed_lanes']
    assert anchor['aliases'] == {'compatible': 'compatible', 'native': 'native', 'max': 'native'}
    return backends


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bind(path):
    return {'path': str(path), 'sha256': sha(path)}


def argv(command):
    return list(command['arguments']) if 'arguments' in command else shlex.split(command['command'])


def redirected(command, replacements, selector=None):
    result = list(command)
    for flag, value in replacements.items():
        assert result.count(flag) == 1, (flag, result)
        result[result.index(flag) + 1] = str(value)
    if selector:
        assert not any(part.startswith('-DPOSETS_MATRIX_BACKEND') for part in result)
        result.append('-DPOSETS_MATRIX_BACKEND=' + selector)
    return result


def write_new(path, value):
    with path.open('x') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--base-revision', required=True)
    parser.add_argument('--selector-overlay', type=Path)
    parser.add_argument('--rank-only', action='store_true')
    parser.add_argument('--rank-source-role', choices=['baseline', 'candidate'])
    parser.add_argument('--rank-source-manifest', type=Path)
    parser.add_argument('--anchor-provenance', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    assert source.is_relative_to(REPO / '_deps') and output.is_relative_to(source / 'build-replay-release')
    assert re.fullmatch(r'[0-9a-f]{40}', args.base_revision)
    anchor_path = args.anchor_provenance.resolve(strict=True)
    anchor = json.loads(anchor_path.read_text())
    anchor = anchor.get('provenance', anchor)
    backends = check_anchor(anchor)
    revision = subprocess.check_output(['git', '--no-optional-locks', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    if args.rank_only:
        assert args.selector_overlay is None and args.rank_source_manifest is not None
        contract.validate_source(source, args.base_revision, revision, args.rank_source_role,
                                 args.rank_source_manifest)
        assert anchor['posets_revision'] == revision and Path(anchor['posets_source_dir']).resolve() == source
        assert anchor.get('rank_anchor') is True and anchor.get('rank_source_role') == args.rank_source_role
        assert anchor.get('rank_source_manifest') == bind(args.rank_source_manifest.resolve(strict=True))
        backends = [contract.RANK]
        source_fields = {'inventory': contract.INVENTORY, 'rank_source_role': args.rank_source_role,
                         'rank_source_manifest': bind(args.rank_source_manifest.resolve(strict=True)),
                         'rank_contract': bind(Path(contract.__file__).resolve())}
    else:
        assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=source)
        assert args.selector_overlay is not None and args.rank_source_role is None and args.rank_source_manifest is None
        overlay = args.selector_overlay.resolve(strict=True)
        assert anchor['posets_revision'] == args.base_revision
        changed = subprocess.check_output(['git', 'diff', '--name-only', args.base_revision, revision], cwd=source, text=True)
        assert changed.splitlines() == ['tests/downset-bm.cc'], changed
        patch = subprocess.check_output(['git', 'diff', '--binary', '--full-index', '--no-ext-diff', args.base_revision,
                                         revision, '--', 'tests/downset-bm.cc'], cwd=source)
        assert patch == overlay.read_bytes(), 'Measurement source differs from the exact approved selector patch'
        source_fields = {'selector_overlay': bind(overlay)}
    build = source / 'build-replay-release'
    commands_path = build / 'compile_commands.json'
    commands = json.loads(commands_path.read_text())
    assert not output.exists(), 'Never overwrite a build/evidence directory'
    output.mkdir()
    (output / 'records').mkdir()
    rows = []
    for mode in ('compatible', 'native'):
        original_object = f'tests/downset-matrix-{mode}.p/downset-bm.cc.o'
        matches = [row for row in commands if row.get('output') == original_object]
        assert len(matches) == 1
        template = matches[0]
        assert Path(template['directory']).resolve() == build
        compiler = argv(template)
        if args.rank_only:
            contract.clean_flags(compiler)
        raw = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands',
                                       f'tests/downset-matrix-{mode}'], text=True)
        ninja_commands = [shlex.split(line) for line in raw.splitlines() if line.strip()]
        assert len(ninja_commands) == 2 and ninja_commands[0] == compiler
        template_link = ninja_commands[1]
        assert template_link.count(original_object) == 1
        for backend in backends:
            stem = mode + '-' + backend
            obj = output / (stem + '.o')
            binary = output / stem
            selected = ALIASES.get(backend, 'posets::downsets::' + backend)
            actual_compile = redirected(compiler, {'-o': obj, '-MF': str(obj) + '.d', '-MQ': obj}, selected)
            actual_link = redirected(template_link, {'-o': binary})
            actual_link[actual_link.index(original_object)] = str(obj)
            command_objects = {
                'compiler_command': {'arguments': actual_compile, 'directory': str(build),
                                     'file': template['file'], 'output': str(obj)},
                'linker_command': {'arguments': actual_link, 'directory': str(build), 'output': str(binary)},
                'template_compiler_command': template,
                'template_linker_command': {'arguments': template_link, 'directory': str(build),
                                            'output': f'tests/downset-matrix-{mode}'},
            }
            stages = []
            for stage, command in [('compile', actual_compile), ('link', actual_link)]:
                spec = {'command': command, 'cwd': str(build),
                        'environment_file': str(REPO / '_deps/build-env.json'), 'timeout': 600}
                path = output / 'records' / f'{stem}-{stage}-spec.json'
                write_new(path, spec)
                stages.append({'stage': stage, 'spec': bind(path),
                               'record': str(path.with_name(f'{stem}-{stage}-record.json'))})
            rows.append({'mode': mode, 'backend': backend, 'binary': str(binary),
                         'build_record': str(output / 'records' / f'{stem}-build.json'),
                         **command_objects, 'stages': stages})
    plan = {'schema_version': 1, 'kind': 'posets-selector-build-plan', 'base_revision': args.base_revision,
            'source_revision': revision, 'source_dir': str(source), **source_fields,
            'anchor_provenance': bind(anchor_path), 'compile_commands': bind(commands_path),
            'source_oracle_binary': {'path': anchor['modes']['compatible']['binary'],
                                     'sha256': anchor['modes']['compatible']['binary_sha256']},
            'aliases': {'max': 'native'}, 'backend_order': backends,
            'generator': bind(Path(__file__).resolve()), 'rows': rows}
    assert len(rows) == (2 if args.rank_only else 48)
    write_new(output / 'build-plan.json', plan)
    print(output / 'build-plan.json')


if __name__ == '__main__':
    main()
