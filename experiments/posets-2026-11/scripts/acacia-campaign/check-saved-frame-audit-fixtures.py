#!/usr/bin/env python3
"""Bounded frame-only fixtures with complete saved-schema header fields."""
if not __debug__:
    raise SystemExit('Validation requires Python without -O/PYTHONOPTIMIZE')
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parent / 'audit-saved-frame-completeness.py'
spec = importlib.util.spec_from_file_location('saved_frames', path)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
cpre = (b'# schema_version=2 loop=1 k=2 actions=1 before=1 input=1\n'
        b'[before]\n0\t-1\n[actions]\naction\t0\n0\t0\t1\n[after]\t1\n-1\t-1\n')
contains = (b'# schema_version=1 k=2 loop=1 maxima=1 expected=1 bin=0 ordinal=2 hits=1 origin=picker\n'
            b'[query]\n0\t-1\n[generators]\n0\t-1\n[end]\n')
assert audit.frame(cpre, 'cpre', 2)['complete_frame']
assert audit.frame(contains, 'contains', 2)['complete_frame']
assert not audit.frame(cpre[:-1], 'cpre', 2)['complete_frame']
assert not audit.frame(cpre.split(b'[after]')[0], 'cpre', 2)['complete_frame']
assert not audit.frame(cpre.replace(b'actions=1', b'actions=2'), 'cpre', 2)['complete_frame']
assert not audit.frame(contains.replace(b'[end]\n', b''), 'contains', 2)['complete_frame']
print('PASS: schema-complete-header frame fixtures and LF/after/action-count/end negatives. No full parser, semantic oracle, answer or timing evaluation.')
