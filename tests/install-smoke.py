#!/usr/bin/env python3
"""Check a fresh staged install without any source include or lib paths."""

import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    build = Path(sys.argv[1])
    compiler = sys.argv[2:]
    options = json.loads(subprocess.check_output(['meson', 'introspect', '--buildoptions', str(build)], text=True))
    options = {option['name']: option['value'] for option in options}
    source = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='posets-install-') as directory:
        subprocess.run(['meson', 'install', '-C', str(build), '--no-rebuild', '--destdir', directory], check=True)
        includedir = Path(options['includedir'])
        if not includedir.is_absolute():
            includedir = Path(options['prefix']) / includedir
        staged = Path(directory) / includedir.relative_to('/')
        subprocess.run([sys.executable, str(source / 'header-check.py'),
                        '--include-dir', str(staged), '--reference-dir', str(source.parent / 'include'),
                        '--compiler'] + compiler, check=True)
        # A real parent project must receive both include roots from posets_dep.
        parent = Path(directory) / 'parent'
        (parent / 'subprojects').mkdir(parents=True)
        (parent / 'subprojects' / 'posets').symlink_to(source.parent, target_is_directory=True)
        (parent / 'example.cc').write_bytes((source / 'header-consumer.cc').read_bytes())
        (parent / 'meson.build').write_text(
            "project('posets consumer', 'cpp', default_options: ['cpp_std=c++20'])\n"
            "posets = subproject('posets')\n"
            "dep = posets.get_variable('posets_dep')\n"
            "consumer = executable('consumer', 'example.cc', dependencies: dep)\n"
            "test('consumer', consumer)\n")
        environment = dict(os.environ, CXX=shlex.join(compiler))
        nested_build = parent / 'build'
        subprocess.run(['meson', 'setup', str(nested_build), str(parent)], env=environment, check=True)
        subprocess.run(['meson', 'compile', '-C', str(nested_build), '-j', '1'], check=True)
        subprocess.run(['meson', 'test', '-C', str(nested_build), '--no-rebuild',
                        '--num-processes', '1', '--print-errorlogs'], check=True)


if __name__ == '__main__':
    main()
