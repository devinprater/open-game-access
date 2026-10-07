#!/usr/bin/env python3
"""nes-reader-set-sabotage.py — perturb every CRC in kReaderSets by +1.

Called by nes-reader-set-test.sh between the real run and the mutation run. If the
identity check is load-bearing, a known dump STOPS matching and the core answers
"" (no reader). A test that still passes here was never testing the identity -- and
"the core boots with no reader" is precisely the state v0.6.0-nes shipped, so this
mutation is the release's own bug in a bottle.

Kept as a separate file rather than a heredoc: the pattern needs backslashes a shell
heredoc mangles (the first attempt silently patched 0 rows and reported a false
failure).

⛔ TWO THINGS THE PATTERN LEARNED THE HARD WAY:
  * the set names contain DIGITS ("Zelda1Access"), so [A-Za-z]+ matches only half the
    table and the mutation quietly under-sabotages;
  * the columns are ALIGNED, so there are runs of spaces between the fields.
It now counts its matches and refuses to proceed on a suspiciously small number.
"""
import os
import re
import sys

root = os.environ.get("ROOT")
if not root:
    sys.exit("!! ROOT is not set")

path = os.path.join(root, "Core/mesen_core.cpp")
src = open(path, encoding="utf-8").read()

# { 829226857u,   "Zelda1Access" },   ->   { 829226858u,   "Zelda1Access" },
pattern = re.compile(r'\{\s*(\d+)u\s*,\s*"([A-Za-z0-9_]+)"\s*\}')

def bump(m):
    return '{%du, "%s"}' % (int(m.group(1)) + 1, m.group(2))

out, n = pattern.subn(bump, src)
if n < 7:
    sys.exit(f"!! sabotage matched only {n} table rows; expected >= 7 "
             f"(has kReaderSets been reformatted, or are the names not [A-Za-z0-9_]?)")

open(path, "w", encoding="utf-8").write(out)
print(f"  sabotaged {n} CRC rows")
