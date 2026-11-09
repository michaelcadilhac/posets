#!/usr/bin/env python3
"""Summarize six frozen paired reports; never launches a measurement."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def key(row):
    return (row['trace'], row['mode'], row['backend'], row['vector'])


def summarize():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('--output-prefix', type=Path)
    args = parser.parse_args()
    PLAN = args.plan.resolve(strict=True)
    plan_prefix = PLAN.with_name(PLAN.stem.removesuffix('-plan'))
    prefix = args.output_prefix.resolve() if args.output_prefix else plan_prefix
    plan = json.loads(PLAN.read_text())
    data, evidence, failures, coverage = {}, [], [], []
    traces = None
    for run in plan['runs']:
        output = Path(run['output'])
        report_path = output / 'report.json'
        report = json.loads(report_path.read_text())
        binding_path = output / 'build-source-binding.json'
        binding = json.loads(binding_path.read_text())
        assert binding['report_sha256'] == sha(report_path)
        assert binding['paired_plan']['sha256'] == sha(PLAN)
        assert binding['source_revision'] == plan['variants'][run['variant']]['revision']
        assert binding['driver']['sha256'] == plan['driver']['sha256']
        variant = plan['variants'][run['variant']]
        if variant.get('binary_map'):
            assert binding['binary_map'] == report['provenance']['binary_map'] == variant['binary_map']
            assert binding['production_base_revision'] == report['provenance']['base_revision'] == variant['production_base_revision']
        assert all(trace['split'] == plan.get('split', 'train') for trace in report['traces'])
        if traces is None:
            traces = report['traces']
        assert report['traces'] == traces
        measurements = [row for row in report['rows'] if row['phase'] == 'measure']
        assert len(measurements) == len({key(row) for row in measurements})
        data[run['pair'], run['variant']] = {
            'measure': {key(row): row for row in measurements},
            'check': {key(row): row for row in report['rows'] if row['phase'] == 'check'},
        }
        phase_counts = {}
        for phase in ('source_oracle', 'check', 'measure'):
            phase_counts[phase] = dict(collections.Counter(
                row['status'] for row in report['rows'] if row['phase'] == phase))
        invoked = [row for row in report['rows'] if 'process' in row]
        peaks = [row['process'].get('memory_peak_bytes') for row in invoked]
        oom = []
        for row in invoked:
            events = json.loads(row['process'].get('scope_memory_events') or '{}')
            if any(events.get(name, 0) for name in ('oom', 'oom_kill', 'oom_group_kill')):
                oom.append(row['log_prefix'])
        coverage.append({
            'run': run['id'], 'phase_counts': phase_counts,
            'executed_invocations': len(invoked),
            'valid_scope_memory_peaks': sum(value is not None for value in peaks),
            'max_scope_memory_peak_bytes': max((value for value in peaks if value is not None), default=None),
            'oom_invocations': oom,
        })
        failures.extend({'run': run['id'], 'row': row} for row in report['rows']
                        if row['status'] != 'pass')
        evidence.append({'run': run['id'], 'report': str(report_path),
                         'report_sha256': sha(report_path), 'binding': str(binding_path),
                         'binding_sha256': sha(binding_path)})
    audits_path = plan_prefix.with_name(plan_prefix.name + '-order-audit.json')
    audits = json.loads(audits_path.read_text())
    assert len(audits) == 3
    assert all(row['common_relative_order_equal'] for row in audits)
    backends = plan.get('backends') or sorted({k[2] for k in data[0, 'baseline']['check']})
    assert len(backends) == 24
    cells = []
    for trace in traces:
        metrics = ('cpre', 'build', 'rebuild') if trace['kind'] == 'cpre' else ('build', 'rebuild')
        for mode in ('compatible', 'native'):
            for backend in backends:
                cell_key = (trace['id'], mode, backend, 'simd_vector_backed')
                checks = [data[pair, variant]['check'][cell_key]['status']
                          for pair in range(3) for variant in ('baseline', 'optimized')]
                measurements = [data[pair, variant]['measure'].get(cell_key)
                                for pair in range(3) for variant in ('baseline', 'optimized')]
                qualified = all(row is not None and row['status'] == 'pass' for row in measurements)
                assert not qualified or all(status == 'pass' for status in checks)
                for metric in metrics:
                    pairs = []
                    for pair in range(3):
                        base = data[pair, 'baseline']['measure'].get(cell_key)
                        opt = data[pair, 'optimized']['measure'].get(cell_key)
                        base_seconds = (base.get('metrics') or {}).get(metric) if base else None
                        opt_seconds = (opt.get('metrics') or {}).get(metric) if opt else None
                        pairs.append({'pair': pair, 'baseline_seconds': base_seconds,
                                      'optimized_seconds': opt_seconds,
                                      'optimized_over_baseline': opt_seconds / base_seconds
                                      if base_seconds is not None and base_seconds > 0
                                      and opt_seconds is not None else None})
                    ratios = [row['optimized_over_baseline'] for row in pairs]
                    cell = {'trace': trace['id'], 'instance': trace['instance'],
                            'kind': trace['kind'], 'maxima': trace['features']['maxima'],
                            'dimension': trace['features']['dimension'], 'mode': mode,
                            'backend': backend, 'vector': 'simd_vector_backed', 'metric': metric,
                            'status': 'paired' if qualified else 'censored',
                            'qualification_statuses': checks,
                            'measurement_statuses': [row['status'] if row else 'not_qualified'
                                                     for row in measurements], 'pairs': pairs}
                    if qualified:
                        assert all(value is not None for value in ratios)
                        cell.update({
                            'baseline_median_seconds': statistics.median(row['baseline_seconds'] for row in pairs),
                            'optimized_median_seconds': statistics.median(row['optimized_seconds'] for row in pairs),
                            'median_paired_optimized_over_baseline': statistics.median(ratios),
                            'paired_ratio_min': min(ratios), 'paired_ratio_max': max(ratios),
                            'pairs_optimized_faster': sum(value < 1 for value in ratios),
                        })
                    cells.append(cell)
    assert len(cells) == sum(3 if trace['kind'] == 'cpre' else 2 for trace in traces) * 2 * 24
    counts = collections.Counter(row['status'] for row in cells)
    result = {
        'scope': plan['scope'],
        'ratio_definition': 'optimized operation seconds / baseline operation seconds, matched by pair and exact trace/mode/backend/vector/metric; below 1 is faster',
        'treatment': 'three external paired repetitions in A/B,B/A,A/B order; rounds=1; seeds=260617,260618,260619; 8G/no swap; no allocation/statistics instrumentation',
        'limits': ['Three observations per cell, no significance claim or generalization beyond these frozen traces.',
                   'A zero baseline operation clock has no defined ratio; summary fails closed rather than inventing one.',
                   'Single-query contains clocks excluded; only whole-CPre, build and rebuild are summarized.',
                   'Process wall time includes parsing/checks/launch/cleanup and is not an operation clock.',
                   'Scope peak memory includes parsing/checks and is not retained storage or an allocation count.',
                   'Source-oracle timeout is not a backend correctness failure; unverified source cells remain censored.',
                   'Each cell represents a fresh replay operation, not continuous captured deletion history.'],
        'paired_plan': {'path': str(PLAN), 'sha256': sha(PLAN)},
        'summarizer_sha256': sha(Path(__file__)), 'evidence': evidence,
        'order_audit': {'path': str(audits_path), 'sha256': sha(audits_path), 'rows': audits},
        'coverage': coverage, 'failure_rows': failures, 'cells': cells,
    }
    prefix.with_name(prefix.name + '-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    columns = ['trace', 'instance', 'kind', 'maxima', 'dimension', 'mode', 'backend', 'metric', 'status']
    for pair in range(3):
        columns += [f'pair{pair}_baseline_seconds', f'pair{pair}_optimized_seconds', f'pair{pair}_optimized_over_baseline']
    columns += ['baseline_median_seconds', 'optimized_median_seconds', 'median_paired_optimized_over_baseline']
    with prefix.with_name(prefix.name + '-cells.tsv').open('w') as stream:
        writer = csv.DictWriter(stream, columns, delimiter='\t', lineterminator='\n', extrasaction='ignore')
        writer.writeheader()
        for cell in cells:
            flat = dict(cell)
            for pair in cell['pairs']:
                flat.update({f"pair{pair['pair']}_{field}": pair[field] for field in
                             ('baseline_seconds', 'optimized_seconds', 'optimized_over_baseline')})
            writer.writerow(flat)
    print(json.dumps({'cells': len(cells), 'paired_metrics': counts['paired'], 'censored_metrics': counts['censored'],
                      'failure_rows': len(failures), 'clean_timing_calls': sum(
                          row['phase_counts']['measure'].get('pass', 0) for row in coverage),
                      'executed_invocations': sum(row['executed_invocations'] for row in coverage),
                      'valid_memory_peaks': sum(row['valid_scope_memory_peaks'] for row in coverage),
                      'oom_invocations': sum(len(row['oom_invocations']) for row in coverage)}))


if __name__ == '__main__':
    summarize()
