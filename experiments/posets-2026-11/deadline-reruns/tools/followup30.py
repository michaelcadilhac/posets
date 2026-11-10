#!/usr/bin/env python3
"""Conditional 30-second follow-up of a completed two-arm 17-second campaign."""
import argparse
from collections import Counter
import csv
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CAMPAIGN = ROOT / 'work/acacia-campaign/campaign.py'
CAMPAIGN_SHA = '20724d11b3b364ccc416ef24927a641fdf706f45f6ec923f10b7864cdfab0a1e'
POLICY = HERE.parent / 'FOLLOWUP-30S-POLICY.json'
POLICY_SHA = '7de1a321de6576633230203b5384bd5184aced781888ff47a21f52fc4e92632a'
E167 = 'e167f43646b880c69046bab7110dbb327686c024'
DECISIVE = {'REALIZABLE', 'UNREALIZABLE'}
ORDINARY = DECISIVE | {'TIMEOUT', 'MEMOUT', 'UNKNOWN'}
MEMORY_KEYS = ('solver_label', 'instance', 'cap_s', 'run_index', 'binary_sha256',
               'scope_unit', 'seconds', 'exit_code', 'scope_memory_peak_bytes',
               'max_process_rss_bytes')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def binding(path):
    path = Path(path).resolve(strict=True)
    return {'path': str(path), 'sha256': digest(path)}


def verify(item):
    require(digest(item['path']) == item['sha256'], f"Changed input: {item['path']}")


def write_new(path, data):
    with path.open('x') as stream:
        json.dump(data, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


def seconds(row):
    raw = row['seconds']
    require(isinstance(raw, str), 'Measured seconds must be a CSV token')
    value = float(raw)
    require(math.isfinite(value) and value >= 0, 'Invalid measured seconds')
    return value


def scored_result(row, cap):
    return 'TIMEOUT' if row['result'] in DECISIVE and seconds(row) > cap else row['result']


def observe_truth(truths, row):
    if row['result'] in DECISIVE:
        prior = truths.setdefault(row['instance'], row['result'])
        require(prior == row['result'], f"Opposite raw verdict: {row['instance']}")


def select_cohort(names, native, scored):
    require(len(names) == len(set(names)), 'Duplicate source instance')
    require(len(native) == 2 and set(native) == set(scored), 'Expected exactly two arms')
    truths = {}
    selected = set()
    for label, rows in native.items():
        require(len(rows) == len(names) and {r['instance'] for r in rows} == set(names),
                'Incomplete/duplicate native source panel')
        view = scored[label]
        require(len(view) == len(names) and {r['instance'] for r in view} == set(names),
                'Incomplete/duplicate scored source panel')
        by_name = {r['instance']: r for r in view}
        for row in rows:
            require(row['cap_s'] == '17', 'Wrong source cap')
            require(row['result'] in ORDINARY, f"Unsupported native outcome: {row['result']}")
            expected = dict(row, result=scored_result(row, 17))
            require(by_name[row['instance']] == expected, 'Scored view changes more than strict result')
            observe_truth(truths, row)  # Include decisive over-cap observations.
            if expected['result'] == 'TIMEOUT':
                selected.add(row['instance'])
    return [name for name in names if name in selected], truths


def resource_guard(rows):
    for row in rows:
        seconds(row)
        cpu = float(row['cpu_seconds'])
        require(math.isfinite(cpu) and cpu >= 0, 'Missing/invalid CPU observation')
        for key in ('max_process_rss_bytes', 'scope_memory_peak_bytes'):
            require(re.fullmatch(r'[0-9]+', row[key]) is not None, 'Missing/invalid memory observation')
        events = json.loads(row['scope_memory_events'])
        require(isinstance(events, dict), 'Invalid kernel event object')
        require(all(type(events.get(k)) is int and events[k] >= 0
                    for k in ('oom', 'oom_kill', 'oom_group_kill')), 'Missing/invalid kernel events')


def join_guard(rows, side_rows):
    def keys(items):
        result = [tuple(row[k] for k in MEMORY_KEYS) for row in items]
        require(len(result) == len(set(result)), 'Duplicate resource/native observation')
        return set(result)
    require(keys(rows) == keys(side_rows), 'Missing/extra/mismatched resource sidecar row')


def check_row30(row, arm, manifest, instance, tlsf):
    require(row['result'] in ORDINARY, f"Unsupported native outcome: {row['result']}")
    expected = {'solver_label': arm['label'], 'instance': instance, 'tlsf_file': tlsf,
                'cap_s': '30', 'binary_sha256': arm['binary_sha256'], 'acacia_sha': arm['acacia_sha'],
                'flags': arm['flags'], 'preset': arm['preset'], 'memory_max': '8G',
                'memory_swap_max': '0', 'collect_rusage': 'true',
                'allowed_cpus': manifest['allowed_cpus'] or '', 'cpu_quota': manifest['cpu_quota'] or ''}
    require(all(row[k] == v for k, v in expected.items()), '30-second identity/treatment mismatch')
    if row['result'] in DECISIVE:
        require(row['exit_code'] == ('0' if row['result'] == 'REALIZABLE' else '1'),
                'Decisive verdict/exit mismatch')
    resource_guard([row])


def commands(manifest, cohort, destination, command_builder):
    arms = {arm['label']: arm for arm in manifest['arms']}
    labels = [manifest['baseline'], next(k for k in arms if k != manifest['baseline'])]
    visits = []
    for i, name in enumerate(cohort):
        order = labels if i % 2 == 0 else list(reversed(labels))
        for label in order:
            leg = destination / 'legs' / f'{i:04d}' / label
            case_list = destination / 'lists' / f'{i:04d}.list'
            before = command_builder(manifest, arms[label], case_list, leg)
            require(before.count('--caps') == 1, 'Ambiguous native coverage cap')
            cap_pos = before.index('--caps') + 1
            require(before[cap_pos] == '17', 'Unexpected original command cap')
            after = list(before)
            after[cap_pos] = '30'
            visits.append({'instance': name, 'label': label, 'leg': str(leg),
                           'case_list': str(case_list), 'command': after,
                           'command_delta': {'index': cap_pos, 'before': '17', 'after': '30'}})
    return visits


def complete_gate(manifest, progress, summary, frozen_sha):
    require(manifest['cap_s'] == 17 and len(manifest['arms']) == 2,
            'Not the final two-arm 17-second campaign')
    arms = {a['label']: a for a in manifest['arms']}
    require(len(arms) == 2 and manifest['baseline'] in arms, 'Duplicate/wrong arm roles')
    require(progress['status'] == 'complete' and progress['completed'] == {a: 1524 for a in arms}
            and summary['usable'] is True and not summary['conflicts']
            and summary['frozen_manifest_sha256'] == frozen_sha,
            'Full 17-second campaign is incomplete/unusable or bound to another freeze')
    require(manifest['memory_max'] == '8G' and manifest['memory_swap_max'] == '0'
            and manifest['allowed_cpus'] is None and manifest['cpu_quota'] is None
            and manifest['collect_rusage'] is True, 'Wrong original resource policy')
    require(arms[manifest['baseline']]['source_pins']['posets'] == E167, 'Not the original default')
    require(all(a['flags'] == '' and a['preset'] == 'otf_sparse_formula_parity'
                and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', a['label']) for a in arms.values()),
            'Arm settings changed')
    require(Path(manifest['repo']).resolve().is_relative_to(ROOT / 'work/acacia-posets-eval')
            and manifest['host_lock'] == f'/tmp/acacia-posets-benchmark-{os.getuid()}.lock',
            'Wrong isolated checkout/host lock')


def campaign_module():
    require(digest(CAMPAIGN) == CAMPAIGN_SHA, 'Changed original campaign helper')
    spec = importlib.util.spec_from_file_location('followup_original_campaign', CAMPAIGN)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_parent(path):
    campaign = campaign_module()
    frozen = path / 'frozen.json'
    manifest = json.loads(frozen.read_text())
    progress = json.loads((path / 'progress.json').read_text())
    summary = json.loads((path / 'summary.json').read_text())
    complete_gate(manifest, progress, summary, digest(frozen))
    arms = {a['label']: a for a in manifest['arms']}
    campaign.verify_files(manifest)
    benchlib, coverage, _reporter = campaign.load_support(Path(manifest['repo']))
    names = coverage.read_instance_list(Path(manifest['inputs']['list']['path']))
    require(len(names) == len(set(names)) == 1524, 'Wrong full source universe')
    native, scored, files = {}, {}, [binding(path / n) for n in ('frozen.json', 'progress.json', 'summary.json')]
    for label, arm in arms.items():
        raw = path / 'raw' / f'{label}.tsv'
        view = path / 'strict-wall-scoring' / f'{label}.tsv'
        native[label] = campaign.read_rows(coverage, raw, arm, set(names), manifest)  # CAP17 only here.
        scored[label] = coverage.load_output(view)
        conflicts = raw.with_name(f'{label}-conflicts.tsv')
        if conflicts.exists():
            require(not coverage.load_conflicts(conflicts), 'Source collected truth conflict')
            files.append(binding(conflicts))
        for p, rows in ((raw, native[label]), (view, scored[label])):
            with Path(str(p) + '.memory.tsv').open() as stream:
                join_guard(rows, list(csv.DictReader(stream, delimiter='\t')))
            resource_guard(rows)
            files.extend([binding(p), binding(Path(str(p) + '.memory.tsv'))])
    cohort, truths = select_cohort(names, native, scored)
    mapping = coverage.read_tlsf_map(Path(manifest['inputs']['tlsf_map']['path']))
    targets = coverage.resolve_targets(names, mapping, Path(manifest['tlsf_corpus']))
    corpus = hashlib.sha256()
    for name in names:
        tlsf, target = targets[name]
        corpus.update(json.dumps([name, tlsf, digest(target)], separators=(',', ':')).encode() + b'\n')
    require(corpus.hexdigest() == manifest['corpus_sha256'], 'Source corpus changed')
    for name in cohort:
        files.append(binding(targets[name][1]))
    # Reuse each actual frozen runtime closure; do not select another library stack.
    for arm in arms.values():
        proof = json.loads(Path(arm['provenance']['path']).read_text())
        if proof.get('postbuild_proof'):
            verify(proof['postbuild_proof']); files.append(proof['postbuild_proof'])
            proof = json.loads(Path(proof['postbuild_proof']['path']).read_text())
        libraries = proof.get('runtime_libraries_unique', proof.get('runtime_libraries', []))
        require(libraries, 'Missing frozen runtime-library closure')
        for item in libraries:
            verify(item); files.append(item)
    return campaign, benchlib, coverage, manifest, cohort, truths, scored, mapping, files


def prepare(parent, output):
    require(digest(POLICY) == POLICY_SHA, 'Changed 30-second policy')
    require(output.is_relative_to(HERE.parent) and not output.is_relative_to(parent)
            and not parent.is_relative_to(output), 'Output must be fresh and separate from source evidence')
    campaign, _benchlib, _coverage, manifest, cohort, truths, scored, mapping, files = load_parent(parent)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'lists').mkdir()
    for i, name in enumerate(cohort):
        with (output / 'lists' / f'{i:04d}.list').open('x') as stream:
            stream.write(name + '\n')
    plan = {'status': 'prepared; execution requires owner lane release', 'parent17': str(parent),
            'policy': binding(POLICY), 'writer': binding(Path(__file__)), 'interpreter': binding(sys.executable),
            'manifest17': manifest, 'cohort': cohort, 'truth17': truths,
            'scored17': {label: {r['instance']: r['result'] for r in rows if r['instance'] in cohort}
                         for label, rows in scored.items()},
            'mapping': {name: mapping[name] for name in cohort}, 'bindings': files,
            'visits': commands(manifest, cohort, output, campaign.command),
            'scope': 'Conditional union of strict-scored TIMEOUTs at 17 seconds; not a full 1,524-case 30-second campaign',
            'cap_s': 30, 'unsolved_PAR2_s': 60}
    write_new(output / 'plan.json', plan)


def environment(manifest):
    hazards = [k for k in os.environ if k.startswith('ACACIA_') and k != 'ACACIA_CAMPAIGN_SCOPE_GUARD']
    require(not hazards, f'Inherited diagnostic/runtime switches: {hazards}')
    actual = {k: v for k, v in os.environ.items() if k.startswith(('ACACIA_', 'LD_', 'MALLOC_', 'GLIBC_', 'OMP_', 'SPOT_'))
              and k != 'ACACIA_CAMPAIGN_SCOPE_GUARD'}
    require(actual == manifest['environment'], 'Frozen runtime environment differs')
    return dict(os.environ, ACACIA_CAMPAIGN_SCOPE_GUARD=os.environ.get('ACACIA_CAMPAIGN_SCOPE_GUARD')
                or f'followup30:{os.getpid()}', PYTHONDONTWRITEBYTECODE='1')


def report(plan, observed):
    require(set(observed) == {a['label'] for a in plan['manifest17']['arms']}, 'Wrong follow-up arm set')
    result = {}
    for arm in plan['manifest17']['arms']:
        label = arm['label']; rows = observed[label]
        require(len(rows) == len(plan['cohort']) and {r['instance'] for r in rows} == set(plan['cohort']),
                'Incomplete/duplicate follow-up panel')
        solved = [r for r in rows if scored_result(r, 30) in DECISIVE]
        result[label] = {'cohort_cases': len(rows), 'solved30': len(solved), 'unsolved30': len(rows) - len(solved),
                         'resolved17_TIMEOUT': sum(plan['scored17'][label][r['instance']] == 'TIMEOUT' for r in solved),
                         'conditional_PAR2_s': sum(seconds(r) if scored_result(r, 30) in DECISIVE else 60 for r in rows),
                         'native_outcomes': dict(Counter(r['result'] for r in rows)),
                         'oom_kill_invocations': sum(json.loads(r['scope_memory_events'])['oom_kill'] > 0 for r in rows),
                         'oom_kill_counter_sum': sum(json.loads(r['scope_memory_events'])['oom_kill'] for r in rows)}
    return {'status': 'complete', 'scope': plan['scope'], 'selected_cases': len(plan['cohort']),
            'strict_wall_cap_s': 30, 'unsolved_PAR2_s': 60, 'results': result,
            'limits': 'One fresh attempt per frozen arm/case; conditional cohort only. 17-second results unchanged.'}


def execute(plan_path):
    plan = json.loads(plan_path.read_text()); output = plan_path.parent
    require(plan['writer'] == binding(Path(__file__)) and plan['policy'] == binding(POLICY)
            and digest(POLICY) == POLICY_SHA and plan['interpreter'] == binding(sys.executable),
            'Changed writer, policy, or interpreter')
    for item in plan['bindings']: verify(item)
    campaign, benchlib, coverage, manifest, cohort, truths, scored, mapping, _files = load_parent(Path(plan['parent17']))
    require(plan['bindings'] == _files and plan['manifest17'] == manifest
            and plan['cohort'] == cohort and plan['truth17'] == truths
            and plan['scored17'] == {label: {r['instance']: r['result'] for r in rows if r['instance'] in cohort}
                                     for label, rows in scored.items()}, 'Source/cohort treatment changed')
    require(plan['mapping'] == {name: mapping[name] for name in cohort}
            and plan['visits'] == commands(manifest, cohort, output, campaign.command)
            and plan['cap_s'] == 30 and plan['unsolved_PAR2_s'] == 60, 'Commands/cap changed')
    child_env = environment(manifest)
    with Path(manifest['host_lock']).open('a') as host, (output / 'driver.lock').open('a') as local:
        try:
            fcntl.flock(host, fcntl.LOCK_EX | fcntl.LOCK_NB); fcntl.flock(local, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return 75
        frozen = {'plan': binding(plan_path), 'invocation': sys.argv, 'environment': manifest['environment']}
        if (output / 'frozen30.json').exists():
            require(json.loads((output / 'frozen30.json').read_text())['plan'] == frozen['plan'], 'Changed resume plan')
        else: write_new(output / 'frozen30.json', frozen)
        arms = {a['label']: a for a in manifest['arms']}; observed = {label: [] for label in arms}
        with benchlib.HostSampler(output / 'thermal.tsv', interval=30) as sampler:
            for visit in plan['visits']:
                leg = Path(visit['leg']); record = leg / 'record.json'; arm = arms[visit['label']]
                require(Path(visit['case_list']).read_text() == visit['instance'] + '\n', 'Changed case list')
                if record.exists():
                    saved = json.loads(record.read_text()); require(saved['plan'] == frozen['plan'] and saved['visit'] == visit, 'Changed completed attempt')
                    for item in saved['files']: verify(item)
                else:
                    require(not leg.exists(), f'Interrupted attempt preserved; no automatic retry: {leg}')
                    if campaign.running_scopes(): return 75
                    campaign.verify_files(manifest, arm)
                    for item in plan['bindings']: verify(item)
                    leg.mkdir(parents=True); (leg / 'raw').mkdir()
                    write_new(leg / 'started.json', {'plan': frozen['plan'], 'visit': visit})
                    sampler.set_leg(f"{visit['instance']}:{visit['label']}", campaign.raw_path(leg, arm))
                    with (leg / 'stdout.log').open('x') as stream:
                        child = subprocess.Popen(visit['command'], cwd=manifest['repo'], env=child_env,
                                                 stdout=stream, stderr=subprocess.STDOUT)
                        try: code = child.wait()
                        except BaseException:
                            child.terminate(); child.wait(timeout=20); raise
                        stream.flush(); os.fsync(stream.fileno())
                    saved = {'plan': frozen['plan'], 'visit': visit, 'exit_code': code,
                             'files': [binding(p) for p in sorted(leg.rglob('*')) if p.is_file()]}
                    write_new(record, saved)
                require(saved['exit_code'] == 0, f'Native CLI failed; raw evidence retained: {record}')
                raw = campaign.raw_path(leg, arm); rows = coverage.load_output(raw)  # No CAP17 reader here.
                require(len(rows) == 1, 'Expected exactly one fresh 30-second row')
                with Path(str(raw) + '.memory.tsv').open() as stream:
                    join_guard(rows, list(csv.DictReader(stream, delimiter='\t')))
                row = rows[0]; check_row30(row, arm, manifest, visit['instance'], mapping[visit['instance']])
                conflicts = raw.with_name(f"{arm['label']}-conflicts.tsv")
                require(not conflicts.exists() or not coverage.load_conflicts(conflicts), 'Known truth conflict')
                observe_truth(truths, row)  # Stop cross-phase/arm conflicts, even beyond 30 seconds.
                observed[arm['label']].append(row)
        campaign.verify_files(manifest)
        for item in plan['bindings']: verify(item)
        summary = report(plan, observed)
        if (output / 'summary30.json').exists(): require(json.loads((output / 'summary30.json').read_text()) == summary, 'Changed completed summary')
        else: write_new(output / 'summary30.json', summary)
    return 0


def interrupted(signum, _frame):
    raise SystemExit(128 + signum)


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--prepare', type=Path, help='Completed final 17-second campaign; emits no target calls')
    mode.add_argument('--run', type=Path, help='Frozen prepared plan.json; existing native coverage owner')
    parser.add_argument('--output', type=Path, help='Fresh follow-up directory for preparation')
    args = parser.parse_args()
    if args.prepare:
        require(args.output is not None, '--output required with --prepare')
        prepare(args.prepare.resolve(), args.output.resolve()); return 0
    require(args.output is None, '--output is only for --prepare')
    try: return execute(args.run.resolve())
    except BaseException as error:
        write_new(args.run.resolve().parent / f'STOP-{time.time_ns()}.json', {'error': str(error), 'type': type(error).__name__})
        raise


if __name__ == '__main__':
    for handled in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(handled, interrupted)
    raise SystemExit(main())
