#!/usr/bin/env python3
"""fix-jobs.py — make sure JOBS is always defined where it is used.

portable-toolchain.py replaced the inline `$(nproc)` with `$JOBS` but then removed
the original `JOBS="${JOBS:-$(nproc)}"` line, and its insert-if-missing guard ran
BEFORE that removal — so it saw "JOBS=" present and inserted nothing. Net effect:
`JOBS: unbound variable` under `set -u`, i.e. the portability fix broke the host
build. Caught by scripts/verify-portable.sh, which runs the real builds rather
than just checking syntax.

This ensures every script that references $JOBS defines it portably.
"""
import glob
import os
import re

BLOCK = '''# ---- parallelism, GNU or BSD ----
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi
'''

here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)

fixed = []
for path in sorted(glob.glob("*.sh")):
    s = open(path, encoding="utf-8", errors="replace").read()
    if "$JOBS" not in s and "${JOBS" not in s:
        continue
    if re.search(r'^JOBS=', s, re.M):
        continue  # already defined
    # Insert after the `set -...` line, before first use.
    m = re.search(r'^set -[a-z]+ *\n', s, re.M)
    if m:
        s = s[:m.end()] + BLOCK + s[m.end():]
    else:
        s = s.replace("\n", "\n" + BLOCK, 1)
    open(path, "w", encoding="utf-8").write(s)
    fixed.append(path)

print(f"== defined JOBS in {len(fixed)} script(s)")
for p in fixed:
    print("   ", p)

print()
print("== verification: every script using $JOBS defines it ==")
bad = 0
for path in glob.glob("*.sh"):
    s = open(path, encoding="utf-8", errors="replace").read()
    if ("$JOBS" in s or "${JOBS" in s) and not re.search(r'^JOBS=', s, re.M):
        print(f"  MISSING: {path}")
        bad += 1
print("  all good" if not bad else f"  {bad} missing")
