"""Project the exact approved saved eligibility inventory; never execute profiles."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

PHASE = Path(__file__).resolve().parent.parent
REPO = PHASE.parent / 'acacia-posets-eval'
INVENTORY = PHASE / 'profile-gate-results-v2/eligibility-inventory.json'
INVENTORY_SHA = '80b5b22324661bd0af8835f5f275326c4844ca188d2498239d2abb9cbe17c7b8'
CLOSURE = PHASE / 'profile-gate-results-v2/actual-artifact-closure.json'
CLOSURE_SHA = 'd097ccb5d0fc311293f2ebb91f2a5f942ec9241e8ce04848373247a7976e3a25'
APPROVAL = PHASE.parent / 'review-probes/profile-v2-eligibility-static-audit.json'
APPROVAL_SHA = 'd308dfd8f21dc75f1e3cfb8d59b1d8989491e0d9d4159beca7ee7c5273d2a9ba'
WRAPPER = PHASE.parent / 'acacia-campaign/scoped_task.py'
WRAPPER_SHA = '9fa7a6777374552f2b0ea95556896757f97cf5396689cdc2ef3026a795688e43'
ENVIRONMENT = PHASE / 'profile-gate-plan-v2/environments/shipping.json'
ENVIRONMENT_SHA = '434ff5292396348ef6bd0220cc00171076707a09ffe11b3c99389f3bcf5ca035'
PINS = {'baseline': '789bf99346d41a30f1b454ff58385b6f8a2961a9',
        'candidate': '30e66853832d98bcfaada296d892157b95a8150d'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binding(path):
    return {'path': str(path), 'sha256': sha(path)}


def load_bound(path, expected):
    require(sha(path) == expected, 'Changed approved input: ' + str(path))
    return json.loads(path.read_text())


PHASE_UNSIGNED = set("""
size generator_capacity entry_live exit_live extra_peak allocations requested_bytes
rank_started rank_completed rank_active_coordinates_planned rank_active_coordinates_completed
vector_copy_attempts vector_move_constructions_completed vector_move_assignments_completed
vector_base_adoptions_completed vector_partial_order_attempts vector_meet_attempts
intersections_started intersections_completed intersections_noop intersections_changed
last_intersection_left last_intersection_queries rank_contains_calls rank_partial_order_calls
rank_insert_attempts rank_insert_rejected_dominated rank_existing_elements_removed
rank_union_insertions rank_intersection_left_elements rank_intersection_short_circuits
rank_intersection_pair_meets rank_apply_elements rank_peak_backing_size
rank_bucket_rebuilds rank_bucket_rebuild_elements
""".split())
OPERATION_PHASES = {'constructor_only_caller_alive', 'contains', 'apply',
                    'union_rhs_still_alive', 'intersection_rhs_still_alive'}
ROI_SEQUENCES = (['build_normalized', 'contains', 'rebuild_normalized_first_alive'],
                 ['build_normalized', 'cpre_after_rhs_destruction_net', 'contains',
                  'rebuild_normalized_first_alive'])
REASONS = {'not_called', 'incomplete', 'no_query', 'queried', 'self', 'empty_left', 'empty_right'}
METADATA_UNSIGNED = {'dimension', 'numeric', 'before', 'actions', 'selected_lanes', 'element_bytes'}


def number(value, signed=False, positive=False):
    pattern = r'(?:0|-?[1-9][0-9]*)' if signed else r'(?:0|[1-9][0-9]*)'
    require(len(value) <= 20 and re.fullmatch(pattern, value), 'Noninteger diagnostic token')
    lower = -(1 << 63) if signed else int(positive)
    upper = (1 << 63) - 1 if signed else (1 << 64) - 1
    require(lower <= int(value) <= upper, 'Diagnostic integer outside emitted type range')


def accept_stdout(stdout, expected):
    """Existing executor calls this only after successful scope/process acceptance."""
    lines = stdout.splitlines()

    def fields(line, keys):
        words = [word.split('=', 1) for word in line.split()]
        require(words and all(len(word) == 2 for word in words), 'Malformed diagnostic output')
        parsed = dict(words)
        require(len(parsed) == len(words) and set(parsed) == keys,
                'Duplicate, missing or unexpected diagnostic field')
        return parsed

    require(len(lines) >= 4, 'Incomplete diagnostic stdout')
    metadata = fields(lines[0], METADATA_UNSIGNED |
                      {'instrumented', 'clocks_discarded', 'vector', 'mode', 'width_selector'})
    for key in METADATA_UNSIGNED:
        number(metadata[key])
    require(metadata['instrumented'] == metadata['clocks_discarded'] == '1' and
            metadata['width_selector'] in ('0', '1') and
            metadata['vector'] in ('vector_backed', 'simd_vector_backed') and
            metadata['mode'] in ('operations', 'roi'), 'Invalid diagnostic metadata token')
    require(metadata == {key: str(value) for key, value in expected['metadata'].items()},
            'Diagnostic metadata does not match projected tuple')
    layout = fields(lines[1], {'vector_bytes', 'vector_alignment', 'move_construct_noexcept',
                              'move_assign_noexcept'})
    number(layout['vector_bytes'], positive=True)
    number(layout['vector_alignment'], positive=True)
    require(layout['move_construct_noexcept'] == layout['move_assign_noexcept'] == '1',
            'Missing shipping trait/layout record')
    cleanup = fields(lines[-1], {'kernel_check', 'after_kernel_cleanup_live', 'cleanup_live_delta'})
    require(cleanup['kernel_check'] == 'pass', 'Missing completed kernel check')
    number(cleanup['after_kernel_cleanup_live'])
    number(cleanup['cleanup_live_delta'], signed=True)
    keys = PHASE_UNSIGNED | {'phase', 'success', 'has_result', 'live_delta',
                             'last_intersection_reason', 'last_intersection_first_failed_query'}
    phases = [fields(line, keys) for line in lines[2:-1]]
    require(phases, 'Missing diagnostic phases')
    for row in phases:
        for key in PHASE_UNSIGNED:
            number(row[key])
        number(row['live_delta'], signed=True)
        require(row['success'] == '1' and row['has_result'] in ('0', '1'),
                'Failed/incomplete diagnostic phase')
        require(row['last_intersection_reason'] in REASONS, 'Invalid intersection reason')
        if row['last_intersection_first_failed_query'] != 'null':
            number(row['last_intersection_first_failed_query'])
    if metadata['mode'] == 'roi':
        require(expected['roi_sequence'] in ROI_SEQUENCES and
                [row['phase'] for row in phases] == expected['roi_sequence'],
                'Diagnostic ROI sequence differs from original3/4 regions')
    else:
        require(expected['roi_sequence'] is None and
                all(row['phase'] in OPERATION_PHASES for row in phases), 'Wrong operation pass')
    for row in phases:
        if row['has_result'] == '0':
            row['size'] = row['generator_capacity'] = None
        if row['last_intersection_first_failed_query'] == 'null':
            row['last_intersection_first_failed_query'] = None
    return {'metadata': metadata, 'layout': layout, 'phases': phases, 'cleanup': cleanup}


def project(output):
    inventory = load_bound(INVENTORY, INVENTORY_SHA)
    closure = load_bound(CLOSURE, CLOSURE_SHA)
    load_bound(APPROVAL, APPROVAL_SHA)
    require(inventory['artifact_closure'] == binding(CLOSURE), 'Eligibility closure differs')
    require(sha(WRAPPER) == WRAPPER_SHA and sha(ENVIRONMENT) == ENVIRONMENT_SHA,
            'Original wrapper/environment changed')
    checked = dict(closure['checked_file_sha256'])
    for path, digest in inventory['checked_file_sha256'].items():
        require(path not in checked or checked[path] == digest, 'Conflicting checked identity')
        checked[path] = digest
    for path, digest in checked.items():
        require(sha(path) == digest, 'Checked source/trace/artifact/library changed: ' + path)
    for role, pin in PINS.items():
        source = Path(closure['source_roles'][role]['source'])
        require(closure['source_roles'][role]['revision'] == pin and
                source.resolve(strict=True).is_relative_to(REPO / '_deps'), 'Wrong source role')
        git = ['git', '--no-optional-locks', '-C', str(source)]
        require(subprocess.check_output(git + ['rev-parse', 'HEAD'], text=True).strip() == pin
                and not subprocess.check_output(git + ['status', '--porcelain'], text=True),
                'Source revision/cleanliness changed')
    artifacts = {(row['role'], row['mode']): row for row in closure['artifacts'] if row['unit'] == 'profile'}
    require(len(artifacts) == 4 and set(artifacts) == {(role, mode) for role in PINS
            for mode in ('compatible', 'native')}, 'Accepted four-profile-ELF inventory differs')
    for artifact in artifacts.values():
        require(sha(artifact['ELF']['path']) == artifact['ELF']['sha256'], 'Profile ELF changed')
        for row in artifact['dependencies'] + artifact['runtime_libraries']:
            require(str(Path(row['path']).resolve(strict=True)) == row['realpath']
                    and sha(row['path']) == row['sha256'], 'Accepted dependency/library changed')
    traces = {row['identity_sha256']: row for row in inventory['traces']}
    cells = {row['cell_index']: row for row in inventory['cells']}
    require(len(traces) == 17 and len(cells) == 102 and len(inventory['pass_inventory']) == 204,
            'Approved eligibility inventory differs')
    rows, specs = [], []
    for number, original in enumerate(inventory['pass_inventory']):
        row, cell = dict(original), cells[original['cell_index']]
        trace = traces[row['trace_identity_sha256']]
        require(all(row[key] == cell[key] for key in
                    ('role', 'mode', 'vector', 'eligible', 'trace_identity_sha256'))
                and cell['source_revision'] == PINS[row['role']]
                and cell['instrumented_ELF'] == artifacts[row['role'], row['mode']]['ELF']
                and row['pass'] in ('operations', 'roi'), 'Projection cell identity differs')
        require(sha(trace['path']) == trace['sha256'], 'Qualified trace changed')
        row.update(reason=cell['reason'], clean_measurement_statuses=cell['clean_measurement_statuses'],
                   source_revision=cell['source_revision'], cohort=cell['cohort'],
                   instrumented_ELF=cell['instrumented_ELF'],
                   trace={'path': trace['path'], 'sha256': trace['sha256']})
        if row['eligible']:
            require(len(cell['observations']) == 4 and all(obs['source_status'] == obs['check_status'] == 'pass'
                    for obs in cell['observations']), 'Cell lacks four passing qualifications')
            stem = f"{number:03d}-{row['role']}-{row['mode']}-{row['vector']}-{row['pass']}"
            path = output / 'specs' / (stem + '.json')
            sequence = ['build_normalized', 'contains', 'rebuild_normalized_first_alive']
            if trace['kind'] == 'cpre':
                sequence.insert(1, 'cpre_after_rhs_destruction_net')
            expected = {'metadata': {'instrumented': 1, 'clocks_discarded': 1, 'vector': row['vector'],
                'mode': row['pass'], 'dimension': trace['features']['dimension'],
                'numeric': trace['features']['numeric_prefix'], 'before': trace['features']['maxima'],
                'actions': int(trace['header']['actions']) if trace['kind'] == 'cpre' else 0,
                'selected_lanes': cell['width_lanes'], 'width_selector': 0 if row['mode'] == 'compatible' else 1,
                'element_bytes': 1}, 'roi_sequence': sequence if row['pass'] == 'roi' else None}
            spec = {'command': [cell['instrumented_ELF']['path'], trace['path'], row['vector'], row['pass']],
                    'cwd': str(REPO), 'environment_file': str(ENVIRONMENT), 'timeout': 30,
                    'expected_exit_codes': [0]}
            row.update(status='planned_instrumented_not_executed', record=str(output / 'results' / (stem + '.json')),
                       expected_stdout=expected)
            specs.append((path, spec, row))
        rows.append(row)
    require(sum(row['eligible'] for row in rows) == 192 and len(specs) == 192,
            'Eligible/unprofiled projection counts differ')
    require(output.is_absolute() and output.resolve().is_relative_to(PHASE) and
            not output.exists() and not output.is_symlink(), 'Fresh task-owned output required')
    output.mkdir()
    (output / 'specs').mkdir()
    for path, spec, row in specs:
        with path.open('x') as stream:
            stream.write(json.dumps(spec, indent=2) + '\n')
        row['spec'] = binding(path)
        row['wrapper_argv'] = ['/usr/bin/python3', '-B', str(WRAPPER), str(path), row['record']]
    plan = {'status': 'projected only; execution needs separate owner release',
            'eligibility_inventory': binding(INVENTORY), 'accepted_closure': binding(CLOSURE),
            'eligibility_approval': binding(APPROVAL), 'projector': binding(Path(__file__).resolve()),
            'wrapper': binding(WRAPPER), 'environment': binding(ENVIRONMENT),
            'profile_artifacts': [{'role': role, 'mode': mode, 'ELF': artifact['ELF'],
                                   'runtime_libraries': artifact['runtime_libraries']}
                                  for (role, mode), artifact in artifacts.items()],
            'rows': rows, 'counts': {'pass_rows': 204, 'runnable_specs': 192, 'unprofiled': 12},
            'interpretation': 'Instrumented counters/heap only; clean statuses/censors remain unchanged. has_result=0 descriptors are unavailable.'}
    with (output / 'plan.json').open('x') as stream:
        stream.write(json.dumps(plan, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('output', type=Path)
    project(parser.parse_args().output)
