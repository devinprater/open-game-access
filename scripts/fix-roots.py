#!/usr/bin/env python3
"""fix-roots.py — make build scripts derive their repo root from their own location.

⛔ WHY: 49 scripts hard-coded ROOT="$HOME/pokemon-access-ios". That is the LOCAL
directory name; CI checks the repo out as `open-game-access`, so every path would
be wrong and the build would fail on its first line — with an error that reads like
a missing SDK rather than a missing repo root.

Deriving from ${BASH_SOURCE[0]} works everywhere: local, WSL, CI, and a fork under
a different name. scripts/build-core.sh already did this; the rest are brought in
line. Written as a Python file rather than a shell heredoc because the replacement
text contains both quote styles and `$()`.
"""
import glob
import os
import sys

OLD = 'ROOT="$HOME/pokemon-access-ios"'
NEW = 'ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"'

here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)

changed = []
for path in sorted(glob.glob("*.sh")):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    if OLD not in s:
        continue
    with open(path, "w", encoding="utf-8") as f:
        f.write(s.replace(OLD, NEW))
    changed.append(path)

print(f"== rewrote {len(changed)} script(s) to derive their root from BASH_SOURCE")
for p in changed:
    print("   ", p)

print()
print("== ROOT patterns remaining across scripts/*.sh ==")
from collections import Counter
c = Counter()
for path in glob.glob("*.sh"):
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("ROOT="):
            c[line.rstrip()] += 1
for k, v in c.most_common():
    print(f"  {v:3d}  {k}")

print()
print("== any script still pointing at the old local directory name? ==")
hits = 0
for path in glob.glob("*.sh"):
    for i, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        if "pokemon-access-ios" in line and "WIN_SRC" not in line and "mnt/c" not in line:
            print(f"  {path}:{i}: {line.rstrip()}")
            hits += 1
if not hits:
    print("  none")
sys.exit(0)
