#!/usr/bin/env python3
"""audit-adapter-speech.py — what does every adapter actually SAY, and at what priority?

WHY THIS EXISTS: the mod research (docs/research/how-mods-make-mechanics-accessible.md)
found that the reviewed mods' announcement vocabularies are reviewable because they keep a
string table (SF6Access/lang/en.txt has 1,004 entries). OGA's spoken lines are inline
snprintf literals scattered across five adapters, so "what will the app say for a DS
game?" is unanswerable without reading C++.

This prints the inventory. Read it to catch two things:
  * a line spoken High that is NOT a player request (it would jump the queue needlessly);
  * a sentence that reads badly aloud (abbreviations, symbol soup, no unit).

Usage:  python3 scripts/audit-adapter-speech.py [Core dir]
Exit 0 always -- this is a report, not a gate, until the counts settle.
"""
import os
import re
import sys
from collections import Counter, defaultdict

CORE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "Core")
CORE = os.path.normpath(CORE)

ADAPTERS = ["fe_adapter.cpp", "fe_access.cpp", "dq9_adapter.cpp", "dbz_adapter.cpp",
            "gba_adapter.cpp", "dissidia_adapter.cpp", "osk_echo.cpp",
            "nes_adapter.cpp", "n64_adapter.cpp"]

# a spoken literal: text >= 6 chars with a letter, that is not a path/log line
SKIP = re.compile(r'(\.cpp|\.h"|%s|%d|%u|%ld|%08X|\[|\]|adapter attached|'
                  r'pokecore\.h|^\W*$)', re.I)


def is_speech(s):
    if len(s) < 6:
        return False
    if not re.search(r'[A-Za-z]{3}', s):
        return False
    if SKIP.search(s):
        return False
    if s.startswith("[") or s.endswith(".h"):
        return False
    return True


def audit(name):
    p = os.path.join(CORE, name)
    if not os.path.exists(p):
        return None
    t = open(p, encoding="utf-8", errors="replace").read()

    # every Say(...) call with an explicit literal priority
    sites = []
    for m in re.finditer(
            r'Say\(((?:[^()]|\([^()]*\))*?),\s*("(?:[^"\\]|\\.)*")\s*,\s*'
            r'oga::Priority::(\w+)\s*\)', t, re.S):
        text = " ".join(m.group(1).split())
        sites.append((m.group(3), m.group(2).strip('"'), text))

    # Announcement{...} literals, for adapters that build the struct directly
    structs = re.findall(r'Announcement\{([^}]*)\}', t, re.S)

    # the spoken vocabulary: string literals that look like sentences
    lits = [s for s in re.findall(r'"((?:[^"\\]|\\.){6,120})"', t) if is_speech(s)]

    return {
        "bytes": len(t),
        "sites": sites,
        "structs": len(structs),
        "lits": lits,
        "announce_calls": t.count("announce("),
        "prios": Counter(re.findall(r'oga::Priority::(\w+)', t)),
    }


def main():
    total_sites = 0
    print("=" * 78)
    print("ADAPTER SPEECH INVENTORY")
    print("=" * 78)
    for name in ADAPTERS:
        r = audit(name)
        if r is None:
            continue
        print()
        print("%-26s %6d bytes   announce() sites: %2d   Announcement{}: %2d"
              % (name, r["bytes"], r["announce_calls"], r["structs"]))
        print("   priorities: %s" % (dict(r["prios"]) or "-"))

        # group by group-name so a screen reader hears it in order
        by_group = defaultdict(list)
        for pri, group, text in r["sites"]:
            by_group[group].append((pri, text))
        for group in sorted(by_group):
            prios = Counter(p for p, _ in by_group[group])
            tag = ",".join(sorted(prios))
            print("   [%s] %s (%d)" % (tag, group, len(by_group[group])))
            for pri, text in by_group[group][:6]:
                print("        %-6s %s" % (pri, text[:74]))
        total_sites += len(r["sites"])

        # ⛔ the thing this tool is FOR: a High line that is not a player request
        suspicious = [t for p, g, t in r["sites"]
                      if p == "High" and re.search(
                          r'\b(watch|alert|warning|hint|reminder|auto)\b', t, re.I)]
        if suspicious:
            print("   !! High-priority lines that may not be player requests:")
            for s in suspicious:
                print("        %s" % s[:74])

    print()
    print("total explicit Say(.., group, priority) sites: %d" % total_sites)
    print()
    print("⛔ REMINDER: a line spoken High jumps dedup and rate limits. That is right for a")
    print("   player request and wrong for an automatic one. Automatic lines belong at")
    print("   Normal (event) or Low (ambient). See docs/design/announcement-queue.md.")


if __name__ == "__main__":
    main()
