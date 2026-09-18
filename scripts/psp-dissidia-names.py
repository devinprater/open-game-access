#!/usr/bin/env python3
"""psp-dissidia-names.py -- dump EVERY entry name across all 388 MPK archives to a file.

WHY
    The archive tables are the game's own manifest. Listing all ~1747 distinct entry names once turns
    "where is the text?" into a grep, instead of guessing which of 388 archives to open.
"""
import os, sys, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sv", os.path.join(HERE, "psp-dissidia-survey.py"))
sv = importlib.util.module_from_spec(spec); spec.loader.exec_module(sv)

pkg = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else "names.txt"

hits = sv.find_magics(pkg, b"MPK ")
f = open(pkg, "rb")
rows = []
for at in hits:
    m = sv.read_mpk(f, at)
    if not m:
        continue
    for e in m["entries"]:
        rows.append((e["name"], m["at"], e["size"], e["offset"]))
f.close()

with open(out, "w", encoding="utf-8") as o:
    for name, at, size, off in sorted(rows):
        o.write("%-52s arch=%-11d size=%-9d data=%d\n" % (name, at, size, off))

print("archives: %d, entries: %d, distinct: %d" % (len(hits), len(rows), len({r[0] for r in rows})))
print("written -> %s" % out)
