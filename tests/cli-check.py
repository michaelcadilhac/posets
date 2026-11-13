#!/usr/bin/env python3
"""Require the SIMD CLI's exact rejection status and diagnostic."""

import re
import subprocess
import sys


def program_stderr(result):
    # CI traces child processes with Valgrind. Ignore its PID-tagged chatter;
    # its error-exitcode=18 still fails the separate exact-status check.
    return re.sub(r'^==\d+==[^\n]*(?:\n|$)', '', result.stderr, flags=re.MULTILINE)


def main():
    binary = sys.argv[1]
    result = subprocess.run([binary, 'vector_backed', 'unknown'],
                            capture_output=True, text=True, timeout=10)
    expected = 'error: no such implem: posets::downsets::vector_backed<posets::vectors::unknown>\n'
    if result.returncode != 1 or result.stdout != '' or program_stderr(result) != expected:
        raise RuntimeError(f'incorrect rejection: {result.returncode}, {result.stdout!r}, {result.stderr!r}')
    for arguments, status in (([], 1), (['vector_backed'], 1),
                              (['vector_backed', 'unknown', 'extra'], 1), (['--help'], 0)):
        result = subprocess.run([binary] + arguments, capture_output=True, text=True, timeout=10)
        if result.returncode != status or not result.stdout.startswith(f'usage: {binary} SETTYPE VECTYPE\n') or program_stderr(result):
            raise RuntimeError(f'incorrect usage: {arguments}, {result.returncode}, {result.stdout!r}, {result.stderr!r}')
    print('SIMD CLI status and diagnostics PASS')


if __name__ == '__main__':
    main()
