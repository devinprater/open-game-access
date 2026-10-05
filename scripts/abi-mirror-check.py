#!/usr/bin/env python3
"""Extract both halves of the append-only ABI mirror and compare them.

Core/adapter.h's `enum class Command` and AdapterCommand.swift's `enum AdapterCommand`
must agree case-for-case and in order: the raw values ARE the C ABI. A drift here is the
bug this project keeps hitting silently (a new command rejected with no speech and no
error), so it is worth a real check rather than a grep.
"""
import re
import sys

# ⛔ DERIVED, NEVER HARD-CODED. A dev-machine path makes this script a different program
# in CI, where the checkout lives somewhere else entirely.
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- the C++ side: strip comments, take the enumerators in order
cpp = open(ROOT + "/Core/adapter.h", encoding="utf-8").read()
m = re.search(r'enum class Command \{(.*?)\n\};', cpp, re.S)
if not m:
    print("!! could not find enum class Command")
    sys.exit(1)
body = m.group(1)
body = re.sub(r'//[^\n]*', '', body)          # line comments
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
cpp_names = [n.strip() for n in re.findall(r'([A-Za-z_]\w*)\s*(?:=[^,]*)?,', body + ',')
             if n.strip()]
print("C++ Command enumerators: %d" % len(cpp_names))
for i, n in enumerate(cpp_names):
    print("   %2d %s" % (i, n))

# ---- the Swift side: name and any explicit raw value
sw = open(ROOT + "/Sources/OpenGameAccess/AdapterCommand.swift", encoding="utf-8").read()
m2 = re.search(r'enum AdapterCommand: Int32[^{]*\{(.*?)\n\s*/// The label', sw, re.S)
if not m2:
    print("!! could not find enum AdapterCommand")
    sys.exit(1)
swift_body = re.sub(r'///[^\n]*', '', m2.group(1))
swift_pairs = re.findall(r'case\s+([A-Za-z_]\w*)\s*(?:=\s*(\d+))?', swift_body)
print()
print("Swift AdapterCommand cases: %d" % len(swift_pairs))
for i, (n, v) in enumerate(swift_pairs):
    print("   %2d %-18s raw=%s" % (i, n, v if v else "(implicit %d)" % i))

# ---- compare
ok = True
if len(cpp_names) != len(swift_pairs):
    print("\n!! COUNT MISMATCH: C++ %d vs Swift %d" % (len(cpp_names), len(swift_pairs)))
    ok = False

for i, (sn, sv) in enumerate(swift_pairs):
    if i >= len(cpp_names):
        break
    cn = cpp_names[i]
    # C++ is CamelCase, Swift is lowerCamelCase
    expect = cn[0].lower() + cn[1:]
    if sn != expect:
        print("!! ORDER MISMATCH at %d: C++ %s vs Swift %s (expected %s)"
              % (i, cn, sn, expect))
        ok = False
    if sv is not None and int(sv) != i:
        print("!! RAW VALUE MISMATCH at %d: %s declared %s" % (i, sn, sv))
        ok = False

print()
if ok:
    print("PASS: the C++ Command enum and the Swift mirror agree case-for-case")
    sys.exit(0)
print("FAIL: the ABI mirror has drifted")
sys.exit(1)
