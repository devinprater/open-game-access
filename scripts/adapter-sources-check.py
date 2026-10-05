#!/usr/bin/env python3
"""Every registered adapter's source must be in the shared build list.

⛔ THE BUG THIS EXISTS FOR: the iOS link died with `Undefined symbols: "oga::kNintendo64"`.
adapters.cpp referenced it, n64_adapter.cpp defined it, and the file was in the HOST build's
hand-written list but not in $OGA_GLUE -- the list build-sim.sh and build-core.sh actually
read. The host suite passed; the app did not link. A comment above the list did not prevent it,
so the check is mechanical.

WHAT IT CHECKS: for every `extern const Adapter kXxx;` in adapters.cpp, the Core/*.cpp that
DEFINES that symbol must appear in OGA_GLUE in scripts/core-sources.sh. That is the one list
the simulator and device builds share, so a miss there is an undefined symbol at the very end
of a long build.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ADAPTERS = ROOT / "Core" / "adapters.cpp"
SOURCES = ROOT / "scripts" / "core-sources.sh"
CORE = ROOT / "Core"


def glue_names():
    """The filenames inside the OGA_GLUE shell string (word-split, so quote-aware enough)."""
    txt = SOURCES.read_text()
    m = re.search(r'OGA_GLUE="(.*?)"', txt, re.S)
    if not m:
        return None
    names = set()
    for tok in m.group(1).split():
        tok = tok.strip().strip('"').strip("'")
        if tok.endswith(".cpp"):
            names.add(tok)
    return names


def main():
    glue = glue_names()
    if glue is None:
        print("adapter-sources: FAIL -- could not find OGA_GLUE in scripts/core-sources.sh")
        return 1

    # every adapter symbol registered
    reg = re.findall(r"extern\s+const\s+Adapter\s+(k\w+)\s*;", ADAPTERS.read_text())
    if not reg:
        print("adapter-sources: FAIL -- no adapters found in adapters.cpp (pattern drift?)")
        return 1

    # Which file defines each symbol. ⛔ EXCLUDE *_test.cpp: those define their own mock
    # adapters, and reading a fixture as a source produces false failures -- which is how a
    # gate earns being ignored.
    defines = {}
    for cpp in sorted(CORE.glob("*.cpp")):
        if cpp.name.endswith("_test.cpp"):
            continue
        body = cpp.read_text(errors="replace")
        for sym in reg:
            if re.search(r"const\s+Adapter\s+" + re.escape(sym) + r"\s*=", body):
                defines[sym] = cpp.name

    problems = []
    for sym in reg:
        src = defines.get(sym)
        if src is None:
            problems.append("%s: no Core/*.cpp defines it (renamed? moved?)" % sym)
        elif src not in glue:
            problems.append("%s: defined in %s, which is NOT in OGA_GLUE" % (sym, src))

    if problems:
        print("adapter-sources: FAIL (%d problem(s))" % len(problems))
        for p in problems:
            print("   x " + p)
        print("   -> the simulator/device build omits these; the app link will fail with")
        print("      'Undefined symbols' at the end of a long build.")
        return 1
    print("adapter-sources: PASS (%d adapter(s), all present in OGA_GLUE)" % len(reg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
