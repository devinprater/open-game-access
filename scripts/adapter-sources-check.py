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

⛔ THE SAME BUG, ON ANDROID: the APK link died with `undefined symbol: oga::kDissidia012`.
The adapter was in OGA_GLUE, so the iOS builds linked, but Android does not read OGA_GLUE: it
stages Core TUs by hand in scripts/android-apply-overlay.sh (the DS glue loop, plus per-core
glue lists such as the PSP's OGA_PPSPP_GLUE), and the file was in none of them. The host diagnostic build
(scripts/build-host.sh) has its own hand list too. So the check covers all three: every
registered adapter's source must be in OGA_GLUE, in one of the two Android lists, and in the
host list.

⛔ AND ITS OWN DEPENDENCIES: the APK link died again with `undefined symbol:
oga::AlttpMap16Walkable` (and nine more). alttp_adapter.cpp was staged, but the two files it
had started calling into, alttp_graph.cpp and alttp_dungeon_graph.cpp, were added to OGA_GLUE
only. So the check follows each adapter's own `#include "x.h"` lines: wherever Core/x.cpp
exists, that file must be in every list too.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ADAPTERS = ROOT / "Core" / "adapters.cpp"
SOURCES = ROOT / "scripts" / "core-sources.sh"
CORE = ROOT / "Core"
OVERLAY = ROOT / "scripts" / "android-apply-overlay.sh"
HOST = ROOT / "scripts" / "build-host.sh"


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


def android_names():
    """Core TUs the Android overlay compiles: the DS glue `for f in ...; do` loop and the
    subproject glue lists it writes into CMake (glue/<name>.cpp lines)."""
    txt = OVERLAY.read_text()
    names = set()
    m = re.search(r'DS_GLUE="\$FRONTEND/app/ds-glue".*?\nfor f in (.*?); do', txt, re.S)
    if not m:
        return None
    for tok in m.group(1).replace("\\", " ").split():
        names.add(tok + ".cpp")
    # The subproject glue libraries (PSP's OGA_PPSPP_GLUE, oga-genesis-glue, ...) name each
    # TU as ${CMAKE_CURRENT_SOURCE_DIR}/glue/<name>.cpp in the CMake text the overlay writes.
    names.update(re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/glue/(\w+\.cpp)", txt))
    return names


def host_names():
    """Core TUs the host diagnostic build (build-host.sh) compiles."""
    return set(re.findall(r'"\$ROOT/Core/(\w+\.cpp)"', HOST.read_text()))


def companions(src):
    """Core/*.cpp files an adapter source calls into: each `#include "x.h"` in it for which
    Core/x.cpp exists. DIRECT includes only -- following them transitively walks into the
    backend cores (pokecore.h pulls in every console), which each build stages its own way."""
    body = (CORE / src).read_text(errors="replace")
    out = []
    for inc in re.findall(r'#\s*include\s+"(\w+)\.h"', body):
        cpp = inc + ".cpp"
        if cpp != src and cpp not in out and (CORE / cpp).exists() \
                and not cpp.endswith("_test.cpp"):
            out.append(cpp)
    return out


def main():
    glue = glue_names()
    if glue is None:
        print("adapter-sources: FAIL -- could not find OGA_GLUE in scripts/core-sources.sh")
        return 1
    android = android_names()
    if android is None:
        print("adapter-sources: FAIL -- could not find the DS glue loop "
              "in scripts/android-apply-overlay.sh (pattern drift?)")
        return 1
    host = host_names()

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
        else:
            for f in [src] + companions(src):
                what = src if f == src else "%s (which %s depends on)" % (f, src)
                if f not in glue:
                    problems.append("%s: %s is NOT in OGA_GLUE" % (sym, what))
                if f not in android:
                    problems.append("%s: %s is NOT compiled by the Android overlay "
                                    "(add it to the DS glue loop or a glue list such as "
                                    "OGA_PPSPP_GLUE in android-apply-overlay.sh)" % (sym, what))
                if f not in host:
                    problems.append("%s: %s is NOT in build-host.sh" % (sym, what))

    if problems:
        print("adapter-sources: FAIL (%d problem(s))" % len(problems))
        for p in problems:
            print("   x " + p)
        print("   -> a build that omits these fails its link ('Undefined symbols' on iOS,")
        print("      'undefined symbol' on Android) at the very end of a long build.")
        return 1
    print("adapter-sources: PASS (%d adapter(s), all in OGA_GLUE, the Android overlay and build-host.sh)" % len(reg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
