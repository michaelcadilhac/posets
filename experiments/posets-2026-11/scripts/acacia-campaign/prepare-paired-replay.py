#!/usr/bin/env python3
"""Write six paired-run specs from frozen artifacts; never runs a solver/build."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'acacia-posets-eval/benchmarking'))
from benchlib import load_tool

HERE = Path(__file__).resolve().parent
REPO = HERE.parent / 'acacia-posets-eval'
BASELINE = REPO / '_deps/replay-baseline-7c40712'


def binding(path):
    path = path.resolve(strict=True)
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def write_new(path, value):
    with path.open('x') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def combined_variants(candidate):
    candidate = candidate.resolve(strict=True)
    assert candidate.is_relative_to(REPO / '_deps')
    metadata = json.loads((candidate / 'provenance.json').read_text())
    baseline = json.loads((BASELINE / 'provenance.json').read_text())
    source = Path(metadata['frozen_source_clone']).resolve(strict=True)
    revision = metadata['frozen_source_revision']
    assert re.fullmatch(r'[0-9a-f]{40}', revision)
    for mode, row in metadata['modes'].items():
        assert len(row['backends']) == 24 and set(row['backends']) == set(baseline['modes'][mode]['backends'])
        assert row['ndebug'] and not row['stdlib_assertions']
        assert row['compiler_command']['command'] == baseline['modes'][mode]['compiler_command']['command']
        assert binding(Path(row['binary']))['sha256'] == row['binary_sha256']
    return {
        'baseline': {'build': str(BASELINE), 'source': baseline['frozen_source_clone'],
                     'revision': baseline['frozen_source_revision'],
                     'production_base_revision': baseline['frozen_source_revision'],
                     'manifest': binding(BASELINE / 'provenance.json')},
        'optimized': {'build': str(candidate), 'source': str(source), 'revision': revision,
                      'production_base_revision': revision,
                      'manifest': binding(candidate / 'provenance.json')},
    }


def anchor_context(anchor):
    M = load_tool('benchmarking/posets-matrix.py')
    source = Path(anchor.get('posets_source_dir') or anchor['frozen_source_clone']).resolve(strict=True)
    fields = ('compiler', 'simd_type', 'selected_lanes', 'alignment', 'element_bytes',
              'block_bytes', 'compatible_lanes', 'native_lanes', 'max_fixed_lanes', 'hybrid_policy')
    return [(mode, tuple(row[field] for field in fields),
             M.normalized_command(row['compiler_command'], source,
                                  Path(row['compiler_command']['directory']).resolve(strict=True)))
            for mode, row in anchor['modes'].items()]


def mapped_variants(baseline_map, candidate_map):
    result, contexts = {}, []
    for label, path in [('baseline', baseline_map), ('optimized', candidate_map)]:
        path = path.resolve(strict=True)
        metadata = json.loads(path.read_text())
        assert metadata['schema_version'] == 1 and metadata['kind'] == 'posets-per-backend-binaries'
        assert all(re.fullmatch(r'[0-9a-f]{40}', metadata[key]) for key in ('source_revision', 'base_revision'))
        source = Path(metadata['source_dir']).resolve(strict=True)
        assert source.is_relative_to(REPO / '_deps')
        anchor_path = Path(metadata['anchor_provenance']['path'])
        assert binding(anchor_path) == metadata['anchor_provenance']
        anchor = json.loads(anchor_path.read_text())
        anchor = anchor.get('provenance', anchor)
        contexts.append(anchor_context(anchor))
        backends = anchor['modes']['compatible']['backends']
        assert len(backends) == 24 and backends == sorted(set(backends))
        assert metadata['base_revision'] == anchor['posets_revision']
        assert [(row['mode'], row['backend']) for row in metadata['binaries']] == [
            (mode, backend) for mode in ('compatible', 'native') for backend in backends]
        oracle = Path(metadata['source_oracle_binary']['path']).resolve(strict=True)
        assert binding(oracle) == metadata['source_oracle_binary']
        assert str(oracle) == anchor['modes']['compatible']['binary']
        assert metadata['source_oracle_binary']['sha256'] == anchor['modes']['compatible']['binary_sha256']
        result[label] = {'build': str(oracle.parent.parent), 'source': str(source),
                         'revision': metadata['source_revision'],
                         'production_base_revision': metadata['base_revision'],
                         'binary_map': binding(path), 'manifest': binding(path)}
    assert contexts[0] == contexts[1], 'Baseline/candidate compiler, flags or width contexts differ'
    return result


def prepare(candidate, traces, prefix, baseline_map=None, candidate_map=None):
    assert re.fullmatch(r'[a-z0-9][a-z0-9-]*', prefix), 'Use a fresh lowercase/hyphen prefix'
    assert bool(baseline_map) == bool(candidate_map)
    assert (candidate is not None) != bool(candidate_map), 'Choose combined artifacts or both binary maps'
    variants = mapped_variants(baseline_map, candidate_map) if candidate_map else combined_variants(candidate)
    trace_rows = json.loads(traces.read_text())['traces']
    splits = {row['split'] for row in trace_rows}
    assert len(splits) == 1 and splits <= {'train', 'heldout'}
    split, = splits
    assert len({row['path'] for row in trace_rows}) == len(trace_rows) > 0
    driver = binding(REPO / 'benchmarking/posets-matrix.py')
    template_path = HERE / 'pair0-baseline-spec.json'
    template = json.loads(template_path.read_text())
    runs, specs = [], []
    order = [(0, 'baseline'), (0, 'optimized'), (1, 'optimized'),
             (1, 'baseline'), (2, 'baseline'), (2, 'optimized')]
    for pair, variant_name in order:
        variant = variants[variant_name]
        run_id = f'{prefix}-pair{pair}-{variant_name}'
        output = REPO / '_bm-logs.posets' / prefix / run_id
        assert not output.exists(), f'Existing raw evidence must not be overwritten: {output}'
        spec = json.loads(json.dumps(template))
        command = spec['command']
        for option, value in [('--build-dir', variant['build']), ('--source-dir', variant['source']),
                              ('--trace-plan', str(traces)), ('--output', str(output)),
                              ('--seed', str(260617 + pair))]:
            command[command.index(option) + 1] = value
        if variant.get('binary_map'):
            command += ['--binary-map', variant['binary_map']['path']]
        spec.update({'pair': pair, 'variant': variant_name, 'driver': driver,
                     'trace_plan': binding(traces), 'source_revision': variant['revision'],
                     'production_base_revision': variant['production_base_revision'],
                     'frozen_build_source_manifest': variant['manifest']})
        if variant.get('binary_map'):
            spec['binary_map'] = variant['binary_map']
        spec_path = HERE / f'{run_id}-spec.json'
        assert not spec_path.exists(), f'Existing spec must not be overwritten: {spec_path}'
        specs.append((spec_path, spec))
        runs.append({'id': run_id, 'pair': pair, 'variant': variant_name, 'seed': 260617 + pair,
                     'output': str(output), 'record': str(HERE / f'{run_id}-record.json')})
    plan_path = HERE / f'{prefix}-plan.json'
    assert not plan_path.exists()
    for run, (path, spec) in zip(runs, specs):
        write_new(path, spec)
        run['spec'] = binding(path)
    treatment = 'per-backend selector with original scalar+SIMD vectors' if candidate_map else 'combined24'
    plan = {'schema_version': 1, 'scope': f"{split} paired replay; {treatment}; production bases {variants['baseline']['production_base_revision']} versus {variants['optimized']['production_base_revision']}",
            'compilation_treatment': treatment,
            'split': split, 'driver': driver, 'orchestrator': binding(HERE / 'run-paired-replay.py'),
            'trace_plan': binding(traces), 'variants': variants, 'runs': runs,
            'method': 'A/B,B/A,A/B; full declared universe shuffled before qualification; all failures retained',
            'plan_writer': binding(Path(__file__)), 'template': binding(template_path),
            'release_note': 'Writing this plan does not release held-out timing or the host lane.'}
    write_new(plan_path, plan)
    return plan_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-build', type=Path)
    parser.add_argument('--baseline-map', type=Path)
    parser.add_argument('--candidate-map', type=Path)
    parser.add_argument('--trace-plan', type=Path, required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    print(prepare(args.candidate_build, args.trace_plan.resolve(strict=True), args.prefix,
                  args.baseline_map, args.candidate_map))


if __name__ == '__main__':
    main()
