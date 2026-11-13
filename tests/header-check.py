#!/usr/bin/env python3
"""Parse each public header alone and run a consumer with only the given paths."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-dir', type=Path, required=True)
    parser.add_argument('--boost-dir', type=Path)
    parser.add_argument('--reference-dir', type=Path, required=True)
    parser.add_argument('--compiler', nargs='+', default=['c++'])
    args = parser.parse_args()
    args.include_dir = args.include_dir.resolve()
    args.reference_dir = args.reference_dir.resolve()
    if args.boost_dir:
        args.boost_dir = args.boost_dir.resolve()
    expected = {p.relative_to(args.reference_dir) for p in
                (args.reference_dir / 'posets').rglob('*.hh')}
    actual = {p.relative_to(args.include_dir) for p in
              (args.include_dir / 'posets').rglob('*.hh')}
    if not expected or actual != expected:
        raise RuntimeError(f'header inventory differs: missing={expected - actual}, extra={actual - expected}')
    command = args.compiler + ['-std=c++20', '-I' + str(args.include_dir)]
    boost_root = args.boost_dir or args.include_dir
    if args.boost_dir:
        command += ['-I' + str(args.boost_dir)]
    with tempfile.TemporaryDirectory(prefix='posets-headers-') as directory:
        root = Path(directory)
        dependencies = root / 'headers.d'
        for header in sorted(expected):
            subprocess.run(command + ['-MD', '-MF', str(dependencies), '-fsyntax-only', '-x', 'c++', '-'],
                           input=f'#include <{header.as_posix()}>\n', text=True, check=True)
            # A host Boost install must not hide holes in the bundled subset.
            for path in dependencies.read_text().replace('\\\n', ' ').split():
                if '/boost/' in path and not Path(path).is_relative_to(boost_root):
                    raise RuntimeError(f'header used Boost outside the supplied tree: {path}')
        output = root / 'consumer'
        subprocess.run(command + [str(Path(__file__).with_name('header-consumer.cc')),
                                  '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)
    print(f'{len(expected)} independent headers and consumer PASS')


if __name__ == '__main__':
    main()
