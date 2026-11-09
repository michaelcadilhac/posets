#!/usr/bin/env python3
"""Audit saved framing only; never evaluate an answer, dominance or CPre operation."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
REPO = HERE.parent / 'acacia-posets-eval'
CAPTURE = REPO / '_bm-logs.posets/capture-20261108-initial'
PLANS = ('initial-train-traces.json', 'initial-heldout-traces.json',
         'large-frontier-train-traces.json', 'large-frontier-heldout-traces.json')


def binding(path):
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def frame(data, kind, dimension):
    errors, facts = [], {'final_lf': data.endswith(b'\n'), 'dimension': dimension}
    if not facts['final_lf']:
        errors.append('missing final LF')
    try:
        lines = data.decode('ascii').splitlines()
        assert lines and lines[0].startswith('# '), 'missing schema header'
        header = dict(re.findall(r'(?:^|\s)(\w+)=([^\s]+)', lines[0]))
        assert int(header['schema_version']) == (2 if kind == 'cpre' else 1), 'unsupported schema'
        assert dimension > 0, 'missing positive metadata dimension'
        def vector(line):
            fields = line.split('\t')
            assert len(fields) == dimension, 'rank/query width differs from metadata'
            tuple(map(int, fields))
        if kind == 'contains':
            count = int(header['maxima'])
            assert count >= 0, 'negative generator count'
            assert lines[1:2] == ['[query]'], 'missing query section'
            vector(lines[2])
            assert lines[3:4] == ['[generators]'], 'missing generator section'
            assert '[end]' in lines[4:], 'missing end marker'
            end = lines.index('[end]', 4)
            facts.update({'declared_generators': count, 'observed_generators': end - 4})
            assert end - 4 == count, 'generator count mismatch'
            assert end == len(lines) - 1, 'trailing content after end marker'
            for line in lines[4:end]:
                vector(line)
        else:
            before, actions = int(header['before']), int(header['actions'])
            assert before >= 0 and actions >= 0, 'negative frame count'
            facts.update({'declared_before': before, 'declared_actions': actions})
            assert lines[1:2] == ['[before]'], 'missing before section'
            assert '[actions]' in lines[2:], 'missing actions section'
            begin = lines.index('[actions]', 2)
            facts['observed_before'] = begin - 2
            assert begin - 2 == before, 'before count mismatch'
            for line in lines[2:begin]:
                vector(line)
            after = next((i for i in range(begin + 1, len(lines))
                          if lines[i].startswith('[after]\t')), None)
            indices, edges = [], 0
            for line in lines[begin + 1:after]:
                fields = line.split('\t')
                if fields[0] == 'action':
                    assert len(fields) == 2, 'malformed action header'
                    indices.append(int(fields[1]))
                else:
                    assert indices and len(fields) == 3, 'malformed edge framing'
                    tuple(map(int, fields))
                    edges += 1
            facts.update({'observed_actions': len(indices), 'action_indices': indices,
                          'observed_edge_rows': edges, 'after_present': after is not None})
            if indices != list(range(actions)):
                errors.append('declared action count/order incomplete')
            assert after is not None, 'missing after section/count/rows'
            marker = lines[after].split('\t')
            assert len(marker) == 2, 'malformed after marker'
            count = int(marker[1])
            assert count >= 0, 'negative after count'
            facts.update({'declared_after': count, 'observed_after': len(lines) - after - 1})
            assert len(lines) - after - 1 == count, 'after count mismatch'
            for line in lines[after + 1:]:
                vector(line)
    except (AssertionError, IndexError, KeyError, ValueError, UnicodeError) as error:
        errors.append(str(error) or type(error).__name__)
    return {'complete_frame': not errors, 'reasons': errors, **facts}


def write_new(path, value):
    with path.open('x') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def main():
    # Structural fixture only: completed frames remain eligible regardless of capture timeout.
    good = b'# schema_version=2 before=1 actions=1\n[before]\n0\t-1\n[actions]\naction\t0\n0\t0\t1\n[after]\t1\n-1\t-1\n'
    assert frame(good, 'cpre', 2)['complete_frame']
    assert not frame(good[:-1], 'cpre', 2)['complete_frame']
    assert not frame(good.split(b'[after]')[0], 'cpre', 2)['complete_frame']
    query = b'# schema_version=1 maxima=1\n[query]\n0\t-1\n[generators]\n0\t-1\n[end]\n'
    assert frame(query, 'contains', 2)['complete_frame']
    output = HERE / 'saved-frame-audit-v1'
    output.mkdir()
    (output / 'plans').mkdir()
    index_path = CAPTURE / 'captures.json'
    index = json.loads(index_path.read_text())
    files, sidecars, captures = [], [], []
    for invocation in index['invocations']:
        manifest_path = Path(invocation['manifest'])
        assert binding(manifest_path)['sha256'] == invocation['sha256']
        manifest = json.loads(manifest_path.read_text())
        snapshot = Path(manifest['snapshot_directory'])
        assert {str(f.relative_to(snapshot)) for f in snapshot.rglob('*') if f.is_file()} == set(manifest['snapshot_files_sha256'])
        counts = Counter()
        for name, expected in manifest['snapshot_files_sha256'].items():
            path = snapshot / name
            data = path.read_bytes()
            assert hashlib.sha256(data).hexdigest() == expected, str(path)
            if path.name.endswith(('.partial', '.skipped')):
                kind = 'partial' if path.name.endswith('.partial') else 'skipped'
                counts[kind] += 1
                sidecars.append({'kind': kind, 'file': binding(path), 'capture_manifest': binding(manifest_path)})
            match = re.fullmatch(r'(cpre|contains)-\d+\.tsv', path.name)
            if match is None:
                continue
            with (path.parent / 'meta.tsv').open() as stream:
                meta, = csv.DictReader(stream, delimiter='\t')
            result = frame(data, match[1], int(meta['states']))
            counts['final_named_operations'] += 1
            counts['complete_frames' if result['complete_frame'] else 'incomplete_frames'] += 1
            files.append({'kind': match[1], 'file': binding(path), 'bytes': len(data),
                'capture_manifest': binding(manifest_path), 'capture_outcome': manifest['classification'],
                'capture_result': manifest['result'], 'meta': binding(path.parent / 'meta.tsv'), 'frame': result})
        captures.append({'instance': invocation['instance'], 'manifest': binding(manifest_path),
                         'outcome': manifest['classification'], 'counts': dict(counts)})
    by_path = {row['file']['path']: row for row in files}
    assert len(files) == len(by_path) == 191
    derivations, exclusions = [], []
    for name in PLANS:
        original_path = HERE / name
        original = json.loads(original_path.read_text())
        eligible, mapping = [], []
        for original_id, entry in enumerate(original['traces']):
            path = (original_path.parent / entry['path']).resolve()
            audit = by_path[str(path)]
            included = audit['frame']['complete_frame']
            new_id = len(eligible) if included else None
            item = {'original_trace_id': original_id, 'new_replay_trace_id': new_id,
                    'instance': entry['instance'], 'family': entry['family'], 'split': entry['split'],
                    'file': audit['file'], 'capture_manifest': audit['capture_manifest'],
                    'capture_outcome': audit['capture_outcome'], 'included': included,
                    'reason': 'structurally complete saved frame' if included else audit['frame']['reasons']}
            mapping.append(item)
            if included:
                eligible.append(entry)
            else:
                exclusions.append({'original_plan': binding(original_path), **item})
        derived_path = output / 'plans' / name.replace('-traces.json', '-complete-frame-traces.json')
        write_new(derived_path, {'schema_version': original['schema_version'], 'traces': eligible})
        derivations.append({'original_plan': binding(original_path), 'derived_plan': binding(derived_path),
            'original_count': len(original['traces']), 'eligible_count': len(eligible),
            'mapping': mapping, 'criterion': 'saved frame structure only; capture outcome is provenance, never an eligibility predicate'})
    write_new(output / 'inventory.json', {'capture_index': binding(index_path), 'auditor': binding(Path(__file__).resolve()),
        'captures': captures, 'operation_files': files, 'sidecars': sidecars,
        'counts': {'final_named_tsv': len(files), 'complete_frames': sum(row['frame']['complete_frame'] for row in files),
                   'incomplete_frames': sum(not row['frame']['complete_frame'] for row in files),
                   'partial': sum(row['kind'] == 'partial' for row in sidecars),
                   'skipped': sum(row['kind'] == 'skipped' for row in sidecars)},
        'limits': 'Only ASCII/schema/markers/counts/integer columns/final LF are checked. No rank ordering, maximality, membership, expected answer, CPre or timing is evaluated.'})
    write_new(output / 'plan-derivations.json', {'plans': derivations, 'note': 'Driver enumerates retained rows; replay IDs may change. Original IDs/path/hash retained here, not added to strict trace-plan row schema.'})
    write_new(output / 'excluded-inputs.json', {'exclusions': exclusions, 'replacement_inputs': 0,
        'repairs': 0, 'cap_changes': 0, 'semantic_or_performance_selection': False,
        'note': 'Original plans/files/reports, including stopped original larger TRAIN, are retained; new eligible larger TRAIN requires fresh3pairs after review.'})
    print(json.dumps({'output': str(output), 'operation_files': len(files),
        'complete_frames': sum(row['frame']['complete_frame'] for row in files),
        'partial_skipped': len(sidecars),
        'plans': [{k: row[k] for k in ('original_count', 'eligible_count')} for row in derivations]}))


if __name__ == '__main__':
    main()
