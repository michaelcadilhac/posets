#!/usr/bin/env python3
"""Collect review-only Acacia metadata; never builds or launches a solver."""
if not __debug__:
    raise SystemExit('Validation requires unoptimized Python')
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent / 'acacia-posets-eval'
BUILD = ROOT / 'build-posets'
GATE = HERE / 'final-acacia-gate'
OUT = GATE / 'proposed'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def binding(path):
    return {'path': str(Path(path).resolve(strict=True)), 'sha256': sha(path)}


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def main():
    OUT.mkdir(exist_ok=False)
    checks = read(GATE / 'checks-summary.json')
    inventory = read(GATE / 'fresh-build-inventory.json')
    preflight = read(GATE / 'preflight.json')
    environment_file = ROOT / '_deps/build-env.json'
    env = os.environ | read(environment_file)
    patch = OUT / 'acacia-integration-final.patch'
    patch.write_bytes(subprocess.check_output(['git', '-C', str(ROOT), 'diff', '--binary', '--full-index']))
    assert sha(patch) == preflight['integration_patch']['sha256']
    repositories = []
    for saved in preflight['repositories']:
        path = Path(saved['path'])
        assert git(path, 'rev-parse', 'HEAD') == saved['revision']
        status = git(path, 'status', '--porcelain')
        assert path == ROOT or not status
        repositories.append(saved | {'status_after_checks': status})

    artifacts = []
    paths = [BUILD / name for name in ['compile_commands.json', 'acacia_build_config.hh', 'acacia_version.hh']]
    paths += [BUILD / 'meson-info' / name for name in ['intro-buildoptions.json', 'intro-targets.json', 'intro-tests.json', 'intro-compilers.json']]
    paths += [BUILD / 'subprojects/tlsf-tools/oxidd' / name for name in ['oxidd-build-stamp', 'oxidd_build_stamp.h', 'oxidd_verified.h', 'liboxidd_ffi_c.a']]
    for source in paths:
        target = OUT / source.name
        shutil.copyfile(source, target)
        artifacts.append(binding(target))
    commands = read(OUT / 'compile_commands.json')
    ninja = shutil.which('ninja')
    argv = [ninja, '-C', str(BUILD), '-t', 'commands', *['src/' + name for name in inventory['candidate_target_names']]]
    result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True, check=True)
    assert not result.stderr.strip()
    expanded_commands = OUT / 'ninja-command-expansion.txt'
    expanded_commands.write_text(result.stdout)

    libraries, arms = {}, []
    for checked in checks['arms_checked']:
        binary = Path(checked['binary'])
        assert sha(binary) == checked['binary_sha256_before_remaining_checks']
        label = checked['label']
        result = subprocess.run(['ldd', str(binary)], cwd=ROOT, env=env, capture_output=True, text=True, check=True)
        assert 'not found' not in result.stdout and not result.stderr.strip()
        ldd_file = OUT / (label + '-ldd.txt')
        ldd_file.write_text(result.stdout)
        dependencies = []
        for line in result.stdout.splitlines():
            match = re.search(r'=>\s+(/\S+)\s+\(', line) or re.match(r'\s*(/\S+)\s+\(', line)
            if match:
                path = Path(match.group(1)).resolve(strict=True)
                if str(path) not in libraries:
                    libraries[str(path)] = binding(path)
                dependencies.append(libraries[str(path)])
        assert len(dependencies) >= 4
        if label == 'anchor-e167':
            anchor = read(HERE / 'anchor-frozen/provenance.json')
            assert sha(binary) == anchor['binary_sha256']
            for old in anchor['runtime_libraries']:
                assert sha(old['path']) == old['sha256']
            compiler_rows = []
            header = ROOT / '_deps/anchor-acacia/build-anchor/acacia_version.hh'
        else:
            prefix = 'src/' + binary.name + '.p/'
            compiler_rows = [row for row in commands if row['output'].startswith(prefix) and (row['file'].endswith('solver/solve_game_vector.cc') or row['file'].endswith('version.cc'))]
            assert len(compiler_rows) == 2
            for row in compiler_rows:
                args = row.get('arguments') or shlex.split(row['command'])
                assert all(flag in args for flag in ['-O3', '-Ofast', '-march=native', '-DNO_VERBOSE', '-DNDEBUG', '-flto=1'])
            header = BUILD / 'acacia_version.hh'
        for dependency in dependencies:
            if Path(dependency['path']).name.startswith(('libspot', 'libbddx')):
                assert Path(dependency['path']).is_relative_to(ROOT / '_deps/spot')
        pins = {'acacia': '86dd487001dc75a3c1c0dfa9db50a9882a532db1', 'posets': checked['posets_revision'], 'tlsf': '2debd5fc87e24de2ae0fb6008b3f7a7779432ff3', 'oxidd': '288a6efcf553f18fe4bf722635157f2453bb2618'}
        if label != 'anchor-e167':
            pins['acacia_integration_patch_sha256'] = sha(patch)
        arms.append({'label': label, 'binary': str(binary), 'binary_sha256': sha(binary), 'flags': '', 'preset': 'otf_sparse_formula_parity', 'acacia_sha': pins['acacia'], 'source_pins': pins, 'version_output': checks['version_output'][label], 'generated_version_header': binding(header), 'runtime_libraries': dependencies, 'ldd': binding(ldd_file), 'compiler_commands_for_variable_TUs': compiler_rows, 'expected_downset': checked['expected_downset'], 'expected_metadata': checked.get('expected_metadata')})
    labels = ['anchor-e167', 'candidate-default', 'kd', 'kd-narrow', 'trie', 'trie-compact', 'comp', 'comp-simd', 'block8', 'block8-uncached', 'block16', 'lazy-fc', 'lazy-search', 'sharingtree', 'simple-sharingtree']
    assert [arm['label'] for arm in arms] == labels
    files = {'list': ROOT / 'tests/suites/benchmarks/syntcomp26/all.list', 'tlsf_map': ROOT / 'tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv', 'status_exceptions': ROOT / 'benchmarking/syntcomp26-status-exceptions.tsv'}
    corpus_check = read(HERE / 'corpus-verification.json')
    assert sha(files['list']) == corpus_check['suite_list_sha256']
    assert sha(files['tlsf_map']) == corpus_check['tlsf_map_sha256']
    names = [line.strip() for line in files['list'].read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')]
    assert len(names) == len(set(names)) == 1524
    support = ['run-syntcomp26-coverage.py', 'benchlib.py', 'scope_memory.py', 'suite_paths.py', 'cactus-report.py', 'make-panel.py', 'family_names.py']
    helpers = [binding(HERE / name) for name in ['campaign.py', 'selfcheck.py', 'scoped_task.py']]
    helpers += [binding(ROOT / 'benchmarking' / name) for name in support]
    assert sha(HERE / 'campaign.py') == '20724d11b3b364ccc416ef24927a641fdf706f45f6ec923f10b7864cdfab0a1e'
    host = OUT / 'host.json'
    write(host, {'lscpu': json.loads(subprocess.check_output(['lscpu', '--json'], text=True)), 'uname': list(os.uname()), 'affinity': sorted(os.sched_getaffinity(0)), 'no_allowed_cpus_or_cpu_quota_override': True})
    source_hashes = {}
    for row in commands:
        source = Path(row['file'])
        source = (source if source.is_absolute() else Path(row['directory']) / source).resolve()
        if source.is_file():
            assert source.is_relative_to(ROOT)
            source_hashes[str(source)] = sha(source)
    record_paths = [GATE / (name + '-record.json') for name in ['configure', 'build', 'unit']] + [Path(stage['record']['path']) for stage in checks['stages']]
    assert len(record_paths) == len(set(record_paths)) == 57
    resources = []
    for path in record_paths:
        record = read(path)
        result = record['result']
        assert result['returncode'] in record['spec'].get('expected_exit_codes', [0]) and not result['timed_out'] and not result['resource_limited']
        assert result['memory_peak_bytes'] is not None and result['cpu_seconds'] is not None
        events = json.loads(result['scope_memory_events'])
        assert not events.get('oom') and not events.get('oom_kill')
        resources.append({'record': binding(path), 'spec_sha256': record['spec_sha256'], 'log': {'path': record['log'], 'sha256': record['log_sha256']}, 'memory_peak_bytes': result['memory_peak_bytes'], 'cpu_seconds': result['cpu_seconds'], 'seconds': result['seconds'], 'expected_exit_codes': record['spec'].get('expected_exit_codes', [0]), 'actual_exit_code': result['returncode']})
    runtime = {'flags_for_all_arms': '', 'preset': 'otf_sparse_formula_parity', 'cap_s': 17, 'memory_max': '8G', 'memory_swap_max': '0', 'allowed_cpus': None, 'cpu_quota': None, 'batch_size': 32, 'seed': 260617, 'serial': True}
    provenance = {'schema_version': 1, 'status': 'proposed/pending-root-freeze', 'scope': 'Final15 Acacia primary arms, provenance only; no primary solver experiment or official freeze.', 'repositories': repositories, 'task_only_acacia_patch': binding(patch), 'production_posets_revision': 'cc058ca96ebc667f727ea2f0c2360b77d60192d3', 'measurement_selector_commits_not_used_by_solver': True, 'pristine_anchor_provenance': binding(HERE / 'anchor-frozen/provenance.json'), 'fresh_inventory': binding(GATE / 'fresh-build-inventory.json'), 'build_artifacts': artifacts, 'build_environment': binding(environment_file), 'host': binding(host), 'compiler': read(GATE / 'intro-compilers.json'), 'ninja_command_expansion': {'arguments': argv, 'directory': str(ROOT), 'stdout': binding(expanded_commands), 'tool_version': subprocess.check_output([ninja, '--version'], text=True).strip()}, 'compiled_source_file_sha256': source_hashes, 'runtime_libraries_unique': list(libraries.values()), 'arms': arms, 'helpers': helpers, 'corpus': {'logical_instances': 1524, 'native_inputs': {key: binding(value) for key, value in files.items()}, 'tlsf_corpus_read_only': '/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus', 'verified_snapshot_entries': 1586, 'verification': binding(HERE / 'corpus-verification.json'), 'note': 'Existing1586-file verification retained;1524 mapped identities are the primary coverage unit. Native freeze/dry-run will recompute mapped corpus digest.'}, 'checks': binding(GATE / 'checks-summary.json'), 'resource_records': resources, 'CLI_evidence_limit': checks['verbose_limit'], 'Boolean_evidence_limit': checks['Boolean_limit'], 'primary_runtime': runtime, 'freeze_and_launch_authorized': False, 'metadata_preparer': binding(__file__)}
    provenance_path = OUT / 'provenance.json'
    write(provenance_path, provenance)
    manifest = {'schema_version': 1, 'status': 'proposed/pending-root-freeze', 'baseline': 'anchor-e167', 'arms': [{key: arm[key] for key in ['label', 'binary', 'binary_sha256', 'flags', 'preset', 'acacia_sha', 'source_pins']} | {'provenance': str(provenance_path), 'provenance_sha256': sha(provenance_path)} for arm in arms], 'exact15_labels': labels, 'primary_settings': runtime, 'no_solver_or_dry_run_started': True}
    manifest_path = OUT / 'arms.json'
    write(manifest_path, manifest)
    assert len(manifest['arms']) == 15 and all(arm['flags'] == '' and arm['preset'] == 'otf_sparse_formula_parity' for arm in manifest['arms'])
    assert all(arm['source_pins']['posets'] == ('e167f43646b880c69046bab7110dbb327686c024' if arm['label'] == 'anchor-e167' else 'cc058ca96ebc667f727ea2f0c2360b77d60192d3') for arm in manifest['arms'])
    summary_path = OUT / 'gate-summary.json'
    write(summary_path, {'status': 'proposed/pending-root-freeze', 'checks': binding(GATE / 'checks-summary.json'), 'provenance': binding(provenance_path), 'arms': binding(manifest_path), 'exact15_empty_primary_flags_validated': True, 'baseline_pristine_anchor': True, 'source_patch_and_pins_verified': True, 'build_tests_authoritatively_true': True, 'unit87_version1_Boolean112_CLI45_passed': True, 'distinct_scoped_setup_build_check_calls': 57, 'memory_peak_coverage': {'valid': 57, 'total': 57, 'max_bytes': max(row['memory_peak_bytes'] for row in resources)}, 'cpu_coverage': {'valid': 57, 'total': 57, 'sum_seconds': sum(row['cpu_seconds'] for row in resources)}, 'oom_count': 0, 'current_binaries_still_in_build_locations_not_officially_frozen': True, 'official_freeze_dryrun_and_primary_launch_not_started': True})
    print(json.dumps({'proposed_arms': binding(manifest_path), 'provenance': binding(provenance_path), 'gate_summary': binding(summary_path), 'scope_calls': 57, 'unique_runtime_libraries': len(libraries), 'compiled_source_files': len(source_hashes)}, indent=2))


if __name__ == '__main__':
    main()
