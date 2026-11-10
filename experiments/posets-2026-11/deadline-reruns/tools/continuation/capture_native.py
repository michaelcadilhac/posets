#!/usr/bin/env python3
"""Capture completed RunResult streams without changing the native scoped call."""
import dataclasses
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


class Capture:
    def __init__(self, original, destination):
        self.original = original
        self.destination = destination
        self.calls = 0

    def __call__(self, *args, **kwargs):
        require(self.calls == 0 and not self.destination.exists(), 'Duplicate/partial capture; no new solver call')
        require(kwargs.get('capture_filter') is None and kwargs.get('capture_consumer') is None,
                'Native full-string capture mode changed')
        self.calls += 1
        result = self.original(*args, **kwargs)
        # The unchanged delegate has completed its timed/resource interval.
        self.destination.mkdir(parents=True, exist_ok=False)
        for name, text in [('stdout.txt', result.stdout), ('stderr.txt', result.stderr)]:
            with (self.destination / name).open('x') as stream:
                stream.write(text)
                stream.flush()
                os.fsync(stream.fileno())
        metadata = dataclasses.asdict(result)
        del metadata['stdout'], metadata['stderr']
        write(self.destination / 'result.json', metadata)
        write(self.destination / 'capture.json', {
            'args': args, 'kwargs': kwargs, 'calls': self.calls,
            'files': [{'path': str(self.destination / name), 'sha256': digest(self.destination / name)}
                      for name in ['stdout.txt', 'stderr.txt', 'result.json']],
            'timing': 'All capture writes occurred after the unchanged scoped delegate returned.'})
        return result


def invoke(native, argv, destination):
    original = native.run_systemd_scope
    capture = Capture(original, destination)
    native.run_systemd_scope = capture
    try:
        code = native.main(argv)
        require(capture.calls == 1, 'Expected exactly one original scoped invocation')
        return code
    finally:
        native.run_systemd_scope = original


def main():
    # Fixed positional boundary: adapter CAPTURE_DIR CLI_PATH CLI_SHA -- original CLI arguments.
    require(len(sys.argv) >= 6 and sys.argv[4] == '--', 'Malformed capture adapter argv')
    destination, source, expected = Path(sys.argv[1]), Path(sys.argv[2]).resolve(strict=True), sys.argv[3]
    require(digest(source) == expected and source.name == 'run-syntcomp26-coverage.py', 'Native CLI source changed')
    sys.path.insert(0, str(source.parent))
    spec = importlib.util.spec_from_file_location('continuation_native_coverage', source)
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    return invoke(native, sys.argv[5:], destination)


if __name__ == '__main__':
    raise SystemExit(main())
