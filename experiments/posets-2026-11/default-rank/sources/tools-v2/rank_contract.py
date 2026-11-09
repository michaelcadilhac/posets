"""Experiment-specific inventory/source rules; no measurement or mutation."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
WORK = HERE.parent.parent
REPO = WORK / 'acacia-posets-eval'
SUPPORT = WORK / 'acacia-campaign'
DRIVER = HERE / 'posets-matrix.py'
RANK = 'rank_bucketed_vector_backed'
INVENTORY = 'rank-compatible-native'
BASE = '789bf99346d41a30f1b454ff58385b6f8a2961a9'
PRODUCTION = 'cc058ca96ebc667f727ea2f0c2360b77d60192d3'
APPROVED_MANIFEST = '93e77732baec145529cd48fece48647db7e4312ee3c296751aa440536d260f02'
APPROVED_PATCH = '889f94fef69e5dcb263e2370c4b9b5b96cd542df6cea4294cab997affc97461d'
CHANGES = {'include/posets/downsets/rank_bucketed_vector_backed.hh',
           'tests/rank-stats.cc', 'tests/meson.build', 'tests/rank-bucketed.cc'}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binding(path):
    path = Path(path).resolve(strict=True)
    return {'path': str(path), 'sha256': sha(path)}


def backends(document, rank_only, complete):
    if rank_only:
        require(document.get('inventory') == INVENTORY, 'Explicit rank inventory required')
        return [RANK]
    require(document.get('inventory') in (None, 'full48'), 'Rank map requires --rank-only')
    return sorted(complete)


def check_cells(document, key, rank_only, complete):
    order = backends(document, rank_only, complete)
    require([(r['mode'], r['backend']) for r in document[key]] ==
            [(mode, backend) for mode in ('compatible', 'native') for backend in order],
            'Missing/extra/duplicate/wrong-width/backend inventory')
    return order


def source_fields(document):
    if document.get('inventory') == INVENTORY:
        return {'inventory': INVENTORY, 'rank_source_role': document['rank_source_role'],
                'rank_source_manifest_sha256': document['rank_source_manifest']['sha256'],
                'rank_contract_sha256': document['rank_contract']['sha256']}
    return {'selector_overlay_sha256': document['selector_overlay']['sha256']}


def validate_source(source, base, revision, role, manifest):
    source, manifest = Path(source).resolve(strict=True), Path(manifest)
    require(source.is_relative_to(REPO / '_deps'), 'Rank source must stay inside isolated _deps')
    require(base == BASE and role in ('baseline', 'candidate') and
            re.fullmatch(r'[0-9a-f]{40}', revision), 'Invalid ranked source role/base/revision')
    require(manifest.is_absolute() and manifest.is_file() and not manifest.is_symlink() and
            sha(manifest) == APPROVED_MANIFEST, 'Unapproved ranked source manifest')
    approved = json.loads(manifest.read_text())
    require(approved['baseline_commit'] == BASE and approved['patch_sha256'] == APPROVED_PATCH,
            'Rank approval base/patch changed')

    def git(*args):
        return subprocess.check_output(['git', '--no-optional-locks', *args], cwd=source)

    require(git('rev-parse', 'HEAD').decode().strip() == revision and
            not git('status', '--porcelain'), 'Rank source pin/cleanliness mismatch')
    git_dir = Path(git('rev-parse', '--absolute-git-dir').decode().strip()).resolve(strict=True)
    common = Path(git('rev-parse', '--git-common-dir').decode().strip())
    common = (common if common.is_absolute() else source / common).resolve(strict=True)
    require(git_dir.is_relative_to(REPO / '_deps') and common.is_relative_to(REPO / '_deps'),
            'Rank Git/common directories escape isolated _deps')
    require(not git('diff', '--name-only', PRODUCTION, BASE, '--', 'include'),
            'Public789 headers are not cc-equivalent')
    changes = set(git('diff', '--name-only', BASE, revision).decode().splitlines())
    if role == 'baseline':
        require(revision == BASE and not changes, 'Rank baseline must be exact public789')
    else:
        require(changes == CHANGES, 'Rank candidate changes more than the reviewed overlay')
        expected = {row['path']: row['sha256'] for row in approved['files'] if row['path'] in CHANGES}
        require(set(expected) == CHANGES, 'Incomplete rank approval source inventory')
        for name, digest in expected.items():
            path = source / name
            require(path.is_file() and not path.is_symlink() and sha(path) == digest,
                    'Rank candidate bytes/type differ from approval')
    for name in ('tests/downset-bm.cc', 'tests/matrix-cpre.hh', 'tests/matrix-check.hh',
                 'tests/test_maker.hh'):
        path = source / name
        require(path.is_file() and not path.is_symlink() and path.read_bytes() == git('show', BASE + ':' + name),
                'Original replay kernel/CLI/vector registration changed')
    return {'production_reference_revision': PRODUCTION, 'baseline_headers_cc_equivalent': True}


def check_command(command, driver=DRIVER):
    """Accept one canonical argv; argparse aliases/last-value overrides cannot hide."""
    require(len(command) > 2 and command[1] == '-B' and Path(command[2]) == driver,
            'Selected replay invokes the wrong driver')
    fixed = {'--source-timeout': '30', '--check-timeout': '2', '--case-timeout': '2',
             '--rounds': '1', '--repetitions': '1', '--memory-max': '8G', '--backends': RANK}
    valued = set(fixed) | {'--binary-map', '--source-dir', '--build-dir', '--trace-plan',
                           '--output', '--seed'}
    flags = {'--rank-only', '--measure'}
    values, index = {}, 3
    while index < len(command):
        option = command[index]
        require(option in valued | flags, 'Noncanonical selected replay option: ' + option)
        require(option not in values, 'Duplicate selected replay option: ' + option)
        if option in flags:
            values[option] = True
            index += 1
        else:
            require(index + 1 < len(command) and command[index + 1] and
                    not command[index + 1].startswith('-'),
                    'Missing selected replay value: ' + option)
            values[option] = command[index + 1]
            index += 2
    require(set(values) == valued | flags, 'Missing selected replay argument')
    for option, value in fixed.items():
        require(values[option] == value, 'Selected replay protocol changed: ' + option)
    require(re.fullmatch(r'[0-9]+', values['--seed']), 'Noncanonical selected replay seed')
    return values


def check_report_seed(report, run):
    """Bind the driver's actual reported shuffle seed, without coercing JSON types."""
    seed = report.get('treatment', {}).get('seed')
    require(type(seed) is int and type(run['seed']) is int and seed == run['seed'],
            'Selected report seed differs from declared run')


def clean_flags(arguments):
    require(not any(arg.startswith('-DPOSETS_RANK_STATS') or
                    (arg == '-D' and i + 1 < len(arguments) and
                     arguments[i + 1].startswith('POSETS_RANK_STATS'))
                    for i, arg in enumerate(arguments)), 'Clean replay must leave rank statistics undefined')


def check_plan_run(plan, run, spec, variant):
    if plan.get('inventory') != INVENTORY:
        require(plan.get('inventory') in (None, 'full48') and spec.get('inventory') is None and
                '--rank-only' not in spec['command'], 'Rank invocation requires an explicit rank plan')
        if variant.get('binary_map'):
            metadata = json.loads(Path(variant['binary_map']['path']).read_text())
            require(metadata.get('inventory') != INVENTORY, 'Rank map requires an explicit rank plan')
        return
    require(plan['rank_contract'] == binding(Path(__file__).resolve()) and
            plan['driver'] == binding(DRIVER) and spec['driver'] == plan['driver'] and
            spec.get('inventory') == INVENTORY, 'Selected plan/invocation/helper mismatch')
    require(plan['backends'] == [RANK] and set(plan['variants']) == {'baseline', 'optimized'} and
            plan['source_revisions'] == {name: value['revision'] for name, value in plan['variants'].items()},
            'Selected paired inventory/revisions differ')
    effective = check_command(spec['command'])
    require(variant == plan['variants'][run['variant']], 'Selected current variant differs')
    for label, declared in plan['variants'].items():
        role = 'baseline' if label == 'baseline' else 'candidate'
        require(declared['rank_source_role'] == role, 'Selected pair source role differs')
        require(binding(declared['binary_map']['path']) == declared['binary_map'], 'Selected map changed')
        metadata = json.loads(Path(declared['binary_map']['path']).read_text())
        check_cells(metadata, 'binaries', True, ())
        require(metadata['rank_contract'] == binding(Path(__file__).resolve()) and
                binding(metadata['rank_source_manifest']['path']) == metadata['rank_source_manifest'],
                'Selected map contract/approval binding differs')
        require(metadata['rank_source_role'] == role and metadata['source_revision'] == declared['revision'] and
                metadata['base_revision'] == BASE and metadata['source_dir'] == declared['source'],
                'Selected variant/map source mismatch')
        validate_source(Path(declared['source']), BASE, declared['revision'], role,
                        Path(metadata['rank_source_manifest']['path']))
    expected = {'--binary-map': variant['binary_map']['path'], '--source-dir': variant['source'],
                '--build-dir': variant['build'], '--trace-plan': plan['trace_plan']['path'],
                '--output': run['output'], '--seed': str(260617 + run['pair'])}
    require(run['seed'] == 260617 + run['pair'], 'Selected pair seed changed')
    for option, value in expected.items():
        require(effective[option] == value,
                'Selected invocation/plan differs: ' + option)
