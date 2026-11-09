#!/usr/bin/env python3
"""Read-only comparison against the original Acacia checkout checkpoint."""
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    before = json.loads((HERE / 'original-checkouts-before.json').read_text())
    rows = {}
    for repo, expected in before.items():
        def git(*args):
            return subprocess.check_output(['git', '--no-optional-locks', '-C', repo, *args])

        actual = {
            'head': git('rev-parse', 'HEAD').decode().strip(),
            'status': git('status', '--porcelain').decode(),
            'index_entries_sha256': hashlib.sha256(git('ls-files', '--stage', '-z')).hexdigest(),
            'diff_sha256': hashlib.sha256(git('diff', '--binary', 'HEAD')).hexdigest(),
        }
        content = hashlib.sha256()
        count = 0
        for name in git('ls-files', '-z').split(b'\0'):
            path = Path(repo) / name.decode()
            if name and path.is_file():
                content.update(name + b'\0' + hashlib.sha256(path.read_bytes()).digest())
                count += 1
        actual.update(tracked_content_sha256=content.hexdigest(), tracked_files_hashed=count)
        rows[repo] = {'actual': actual, 'matches': {key: value == expected[key]
                                                for key, value in actual.items()}}
    print(json.dumps({'scope': 'HEAD, status, NUL-delimited index entries, binary HEAD diff and tracked file bytes; '
                              'does not compare ignored/untracked file bytes', 'repositories': rows}, indent=2))
    return int(not all(all(row['matches'].values()) for row in rows.values()))


if __name__ == '__main__':
    raise SystemExit(main())
