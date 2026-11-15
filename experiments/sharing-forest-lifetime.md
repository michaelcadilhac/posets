# Sharing-forest lifetime gate (#62, L2)

Baseline: forest header from `6405e4f85743d0724c19b26e431d7447bdc82204`.
Candidate: the four-line duplicate-tail-span rewind in `add_node`, with the
same harness and other headers as the baseline. Both use the **same unmodified
harness**, compiled on x86-64 Linux with GCC 15.3.1 (Red Hat 15.3.1-1),
`-std=c++20 -O2 -DNDEBUG`, default ISA, with no `-march` flag. Only the forest
header's include-path precedence differs.

Each fixture runs 4,096 union/intersection pairs (8,192 operations), with scalar
and SIMD vectors at dimensions 4, 17 and 65. The subject has 8–16 maximal
generators, plus an unchanged 8-generator witness. Repeated inputs reuse a
fixed frontier; changing inputs advance a bounded window while keeping the
same forest alive. Samples are taken after rounds 1, 2, 4, ..., 4,096; peak is
cumulative over the entire fixture, including transient allocation peaks.

The allocation hook counts requested payload for all scalar, array and aligned
C++ allocations: forest nodes, child-buffer capacities, maps, caches, vector
storage and temporaries, including iteration/size/membership work. Oracle
fixtures and probes are prepared before the accounting baseline, as is the
fixed expired weak-map entry/control block. Direct `malloc`, allocator metadata,
hook headers/alignment, mapped pages, stacks and RSS are excluded. Every
fixture returns to its accounting baseline after its last owner dies. These
are requested C++ heap measurements; no RSS, timing or instruction gain is
claimed.

Paired results, in bytes at round 4,096:

| Dimension | Vector | Inputs | Retained baseline | Retained candidate | Peak baseline | Peak candidate |
|---:|---|---|---:|---:|---:|---:|
| 4 | scalar | repeated | 8,207,628 | 1,039,628 | 12,304,204 | 1,552,204 |
| 4 | SIMD | repeated | 8,207,372 | 1,039,372 | 12,303,820 | 1,551,820 |
| 4 | scalar | changing | 28,395,004 | 24,299,004 | 28,514,676 | 24,418,676 |
| 4 | SIMD | changing | 28,394,748 | 24,298,748 | 28,514,292 | 24,418,292 |
| 17 | scalar | repeated | 16,436,184 | 1,076,184 | 24,629,272 | 1,589,272 |
| 17 | SIMD | repeated | 16,435,928 | 1,075,928 | 24,628,888 | 1,588,888 |
| 17 | scalar | changing | 53,007,080 | 24,335,080 | 64,760,280 | 24,455,264 |
| 17 | SIMD | changing | 53,006,824 | 24,334,824 | 64,759,768 | 24,454,880 |
| 65 | scalar | repeated | 65,727,768 | 1,215,768 | 98,496,096 | 1,730,392 |
| 65 | SIMD | repeated | 65,727,512 | 1,215,512 | 98,495,824 | 1,730,008 |
| 65 | scalar | changing | 151,449,896 | 24,473,896 | 214,027,128 | 24,595,616 |
| 65 | SIMD | changing | 151,449,640 | 24,473,640 | 214,026,616 | 24,595,232 |

Both metrics strictly improve in all 12 final pairs and never increase at any
of the 156 paired checkpoints (312 samples). Every operation's independent
oracle and surviving-root checks pass, and all fixtures report `released=all`.
This establishes the issue's whole-operation memory gate separately from the
fixed growth regression bound, so the implementation is retained.

**Remaining historical growth:** distinct nodes, maps, query stamps, algebra
caches and unreclaimed non-tail/partially occupied draft spans still live until
the last forest owner dies. This reuses unreachable duplicate spans; it does
not collect historical nodes or guarantee memory bounded by the live frontier.
For example, candidate scalar retained bytes at dimension 17 still grow:

| Rounds | Repeated inputs | Changing inputs |
|---:|---:|---:|
| 1,024 | 308,184 | 6,136,984 |
| 2,048 | 564,184 | 12,166,456 |
| 4,096 | 1,076,184 | 24,335,080 |

## Source identity

Measured on 2026-11-15, using the following SHA-256 identities:

| Source | SHA-256 |
|---|---|
| `tests/sharing-forest-lifetime.cc` | `9e96284a6884c9239c8767a3f6c4ba69513832c2859831836ffdd60e7263bee1` |
| Baseline `sharingforest.hh` | `b40f7fe97549c810192fc478b98bcb6ac91003ea655ee9853bbfee0e30f2ec34` |
| Candidate `sharingforest.hh` | `d56b7c2b74c9085d7c87a1b8f04adb1e968a197290f86a0e9e6c23e2fc61b8b3` |

## Reproduce the gate

Run this from the repository root with GCC and the existing Boost headers.
It builds serially and runs one fixture process at a time in the required
5 GB scope. No file under `/tmp/posets-62-L2` is required. The baseline's exit
status **1** is expected only after all four fixtures report; its stderr must
contain the growth-bound error. The candidate must exit **0**.

```bash
systemd-run --user --scope -p MemoryMax=5G bash <<'SH'
set -eu
export evidence=$(mktemp -d)
mkdir -p "$evidence/baseline/posets/utils"
git show 6405e4f85743d0724c19b26e431d7447bdc82204:include/posets/utils/sharingforest.hh > "$evidence/baseline/posets/utils/sharingforest.hh"
g++ --version
g++ -std=c++20 -O2 -DNDEBUG -I"$evidence/baseline" -Iinclude -Ilib tests/sharing-forest-lifetime.cc -o "$evidence/before"
g++ -std=c++20 -O2 -DNDEBUG -Iinclude -Ilib tests/sharing-forest-lifetime.cc -o "$evidence/after"
for dim in 4 17 65; do
  status=0
  "$evidence/before" 4096 "$dim" > "$evidence/before-$dim.txt" 2> "$evidence/before-$dim.err" || status=$?
  test "$status" -eq 1
  grep -Fx 'duplicate forest spans retained across repeated operations' "$evidence/before-$dim.err"
  "$evidence/after" 4096 "$dim" > "$evidence/after-$dim.txt"
done
python3 - <<'PY_CHECK'
import os
from pathlib import Path
root = Path(os.environ['evidence'])
total = 0
for dim in (4, 17, 65):
    before, after = [
        [dict(field.split('=') for field in line.split()) for line in
         (root / f'{side}-{dim}.txt').read_text().splitlines()]
        for side in ('before', 'after')
    ]
    assert len(before) == len(after) == 52
    for a, b in zip(before, after):
        for key in ('vector', 'changing', 'dim', 'rounds', 'released', 'oracle'):
            assert a[key] == b[key], (a, b)
        assert a['released'] == 'all' and a['oracle'] == 'PASS'
        for key in ('retained', 'peak'):
            assert int(b[key]) <= int(a[key]), (key, a, b)
            if a['rounds'] == '4096':
                assert int(b[key]) < int(a[key]), (key, a, b)
        if a['rounds'] == '4096':
            print(dim, a['vector'], a['changing'], a['retained'],
                  b['retained'], a['peak'], b['peak'])
        total += 1
assert total == 156
print('156 paired checkpoints PASS; all 12 final retained/peak pairs improve')
PY_CHECK
printf 'Raw samples: %s\n' "$evidence"
SH
```

