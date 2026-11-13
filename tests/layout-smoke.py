#!/usr/bin/env python3
"""Check allocation diagnostics and independent semantic checks on small shapes."""
import subprocess
import sys


def require(condition, fields):
    if not condition:
        raise RuntimeError(f'incorrect layout record: {fields}')


for shape in ('random', 'prefix', 'suffix'):
    for backend in ('kd', 'trie', 'comp', 'b1scalar', 'b4scalar', 'b4simd',
                    'b8scalar', 'b8simd', 'b16scalar', 'b16simd', 'b8nocache', 'lazy-fc', 'lazy-search'):
        result = subprocess.run([sys.argv[1], backend, shape, '33',
                                 '65' if backend.startswith('lazy') else '31', '24', '2', '1'],
                                capture_output=True, text=True, check=True, timeout=15)
        fields = dict(pair.split('=', 1) for pair in result.stdout.split())
        require(fields['heap_tracking'] == '1', fields)
        require(int(fields['build_allocations']) > 0, fields)
        require(fields['checked'] == '1' and fields['query_allocations'] == '0', fields)
        require(int(fields['build_heap_peak']) >= int(fields['heap_before']), fields)
        require(int(fields['rebuild_heap_peak']) >= int(fields['rebuild_heap_before']), fields)
        require(int(fields['reachable']) <= int(fields['capacity']), fields)
print('layout memory, rebuild peaks, allocation-free queries and shape oracles PASS')
