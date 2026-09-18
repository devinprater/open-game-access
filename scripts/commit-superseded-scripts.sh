#!/bin/bash
# Mark the two superseded naive-scanner scripts, commit+push, verify.
set -eu
R=/home/devin/oga-work
WD="/mnt/c/Users/Devin Prater/open-game-access"

# syntax-check both edited scripts before shipping them
python3 - <<'PY'
import ast
for p in [r"/mnt/c/Users/Devin Prater/open-game-access/scripts/psp-ram-utf16.py",
          r"/mnt/c/Users/Devin Prater/open-game-access/scripts/psp-dissidia-ramtext.py"]:
    src = open(p, encoding="utf-8").read()
    ast.parse(src)
    print("OK  %s  banner=%s" % (p.rsplit("/", 1)[-1], "SUPERSEDED" in src))
PY

for f in psp-ram-utf16.py psp-dissidia-ramtext.py; do
  cp -f "$WD/scripts/$f" "$R/scripts/"
done

git -C "$R" add -A scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "Dissidia: mark the two superseded naive-scanner scripts so their addresses cannot be reused

Two committed scripts still carried the chunk scanner that produced WRONG addresses:
psp-ram-utf16.py and psp-dissidia-ramtext.py. Both step a counter across a HARD-CODED region
of 0x08800000+0x08000000, which runs PAST the mapped end -- Dissidia's user RAM ends at
0x0A000000. A memory.read crossing the mapped end returns 0 bytes WHOLE rather than
truncating, so the counter advances while reads are empty and later hits are attributed to a
wrong base. Evidence in the original scan output itself: the same byte pattern reported at
both 0x09EEB917 and 0x0A2C3547.

Neither script is referenced by the doc; the working replacements are
psp-ppsspp-client.py (--find with a --region) and psp-dissidia-textdump.py, both of which
enumerate mapped spans first and surface the debugger's Invalid-address error.

Annotated both in place with a SUPERSEDED banner explaining the bug and naming the
replacements. Kept as a record rather than deleted, consistent with the treatment of the
stale Ghidra reports -- a documented wrong result is a warning; a deleted one is an unmarked
trap."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
