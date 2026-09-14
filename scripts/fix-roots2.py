#!/usr/bin/env python3
"""fix-roots2.py — second pass: the remaining hard-coded local paths.

The first pass fixed the `ROOT=...` line. These are the scripts that used the
literal path inline instead (`cd "$HOME/open-game-access"`, `OBJ=".../Vendor/..."`),
which breaks identically in CI under a different checkout name.

Every occurrence of the literal is replaced with "$ROOT", and a ROOT definition is
inserted after the shebang when the script does not already define one. That is a
mechanical, reviewable transformation — not a rewrite of anyone's logic.
"""
import glob
import os
import re

LITERAL = "$HOME/open-game-access"
ROOT_DEF = 'ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"'

here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)

changed = []
for path in sorted(glob.glob("*.sh")):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    body = "".join(lines)
    if LITERAL not in body:
        continue

    # Replace the literal everywhere. Inside a double-quoted string
    # "$HOME/open-game-access" becomes "$ROOT"; bare paths likewise.
    new = body.replace('"' + LITERAL + '"', '"$ROOT"')
    new = new.replace(LITERAL, "$ROOT")

    # Ensure ROOT is defined. Insert after the shebang and any leading comment
    # block, so it is set before first use but stays out of the header comments.
    if not re.search(r'^ROOT=', new, re.M):
        out = []
        inserted = False
        for i, line in enumerate(new.splitlines(keepends=True)):
            out.append(line)
            if not inserted and i > 0 and line.startswith("set "):
                out.append(ROOT_DEF + "\n")
                inserted = True
        if not inserted:
            # no `set` line: put it right after the shebang
            out = []
            for i, line in enumerate(new.splitlines(keepends=True)):
                out.append(line)
                if i == 0 and line.startswith("#!"):
                    out.append(ROOT_DEF + "\n")
        new = "".join(out)

    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    changed.append(path)

print(f"== second pass: rewrote {len(changed)} script(s)")
for p in changed:
    print("   ", p)

print()
print("== any script still naming the old local directory? ==")
hits = 0
for path in glob.glob("*.sh"):
    for i, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        if "open-game-access" in line and "WIN_SRC" not in line:
            print(f"  {path}:{i}: {line.rstrip()}")
            hits += 1
print("  none" if not hits else f"  {hits} remaining")
