#!/usr/bin/env python3
"""audit-adapter-speech.py — what does every adapter actually SAY, at what priority, and
does it follow the design rules?

WHY THIS EXISTS: the mod research (docs/research/how-mods-make-mechanics-accessible.md)
found that the reviewed mods' announcement vocabularies are reviewable because they keep a
string table (SF6Access/lang/en.txt has 1,004 entries). OGA's spoken lines are inline
snprintf literals scattered across the adapters, so "what will the app say for a DS game?"
is unanswerable without reading C++.

THREE MODES:
  (default)     print the inventory — every spoken literal with its group and priority.
  --check       exit non-zero on a MECHANICAL rule violation, so CI can gate it:
                  * an empty spoken literal (says nothing; reads as a broken key);
                  * a High-priority line whose wording sounds automatic (a High line
                    jumps dedup and rate limits — right for a player request, wrong for
                    an ambient event);
                  * a spoken literal carrying a format specifier the call cannot supply.
  --coverage    print which of the design rules from the research each adapter follows.
                The inventory alone cannot say whether refusals are named or whether
                arrival is confirmed from the game's own prompt.

⛔ WHAT THIS DELIBERATELY DOES NOT DO: judge whether a sentence reads WELL aloud. That
needs ears. It proves mechanical rules only.

Usage:  python3 scripts/audit-adapter-speech.py [--check|--coverage] [Core dir]
"""
import argparse
import os
import re
import sys
from collections import Counter, defaultdict

ADAPTERS = ["fe_adapter.cpp", "fe_access.cpp", "dq9_adapter.cpp", "dbz_adapter.cpp",
            "gba_adapter.cpp", "dissidia_adapter.cpp", "osk_echo.cpp",
            "nes_adapter.cpp", "n64_adapter.cpp"]

# A literal that is a path, a log line, or a format-only stub is not speech.
SKIP = re.compile(r'(\.cpp|\.h"|\.lua|adapter attached|pokecore\.h|^\W*$)', re.I)

# Wording that suggests the line is automatic rather than a player request.
AUTOMATIC = re.compile(
    r'\b(watch|alert|warning|hint|reminder|auto|detected|appeared|vanished)\b', re.I)

SAY_RX = re.compile(
    r'Say\(((?:[^()]|\([^()]*\))*?),\s*("(?:[^"\\]|\\.)*")\s*,\s*'
    r'oga::Priority::(\w+)\s*\)', re.S)


def is_speech(s):
    if len(s) < 6:
        return False
    if not re.search(r'[A-Za-z]{3}', s):
        return False
    if SKIP.search(s):
        return False
    return not (s.startswith("[") or s.endswith(".h"))


def say_sites(text):
    """Every Say(<expr>, "<group>", Priority::<P>)."""
    out = []
    for m in SAY_RX.finditer(text):
        out.append((m.group(3), m.group(2).strip('"'), " ".join(m.group(1).split())))
    return out


def check_file(name, text):
    bad = []
    for pri, group, expr in say_sites(text):
        lit = re.match(r'^"(.*)"$', expr, re.S)
        spoken = lit.group(1) if lit else None

        if spoken is not None:
            if spoken.strip() == "":
                bad.append("%s [%s] an EMPTY spoken string" % (name, group))
            # A specifier in the literal must be fed by the call's other arguments. If the
            # call has no other argument and no buffer variable, it prints a raw token.
            if re.search(r'%[sdu]', spoken):
                rest = expr[lit.end():] if lit else ""
                if not rest.strip():
                    bad.append("%s [%s] format specifier with no argument: %r"
                               % (name, group, spoken[:50]))
        if pri == "High" and spoken and AUTOMATIC.search(spoken):
            bad.append("%s [%s] High but sounds automatic: %r"
                       % (name, group, spoken[:60]))
    return bad


def coverage(text):
    lits = " ".join(re.findall(r'"([^"]*)"', text))
    low = lits.lower()
    return [
        ("R3 arrival from the game's own prompt",
         bool(re.search(r'\b(arriv|on the spot|in front of you|facing)\b', low))),
        ("R6 the player's own unit (step/tile/clock)",
         bool(re.search(r"\b(o'clock|oclock|tile|square|step|paces?|metre|meter)\b", low))),
        ("R8 refusals named",
         bool(re.search(r'\b(no route|unreachable|not reachable|nothing|no menu|unknown|'
                        r"cannot|can't|not tracked|no board|unreadable|not ready|not open|"
                        r'position unknown)\b', low))),
        ("R7 re-read / repeat wording",
         bool(re.search(r'\b(again|repeat|recap|summary)\b', low))),
        ("R9 spatial direction vocabulary",
         bool(re.search(r'\b(north|south|east|west|left|right|ahead|behind|up|down|'
                        r'open|blocked)\b', low))),
        ("R4 speaks discrete values, not timers",
         bool(re.search(r'\b(of \d|row \d|item|slot|\d of \d)\b', low))),
    ]


def main():
    ap = argparse.ArgumentParser(description="audit adapter speech")
    ap.add_argument("core", nargs="?", default=None, help="Core dir (default: ../Core)")
    ap.add_argument("--check", action="store_true", help="gate: exit 1 on a violation")
    ap.add_argument("--coverage", action="store_true", help="print design-rule coverage")
    args = ap.parse_args()

    core = args.core or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Core")
    core = os.path.normpath(core)

    violations = []
    total = 0
    present = []

    for name in ADAPTERS:
        p = os.path.join(core, name)
        if not os.path.exists(p):
            continue
        present.append(name)
        text = open(p, encoding="utf-8", errors="replace").read()

        if args.coverage:
            print("%-26s" % name)
            for label, ok in coverage(text):
                print("   %-38s %s" % (label, "yes" if ok else "-"))
            continue

        if args.check:
            violations += check_file(name, text)
            continue

        sites = say_sites(text)
        total += len(sites)
        print()
        print("%-26s %6d bytes   Say sites: %2d" % (name, len(text), len(sites)))
        by_group = defaultdict(list)
        for pri, group, expr in sites:
            by_group[group].append((pri, expr))
        for group in sorted(by_group):
            prios = Counter(q for q, _ in by_group[group])
            print("   [%s] %s (%d)" % (",".join(sorted(prios)), group,
                                       len(by_group[group])))
            for pri, expr in by_group[group][:6]:
                print("        %-6s %s" % (pri, expr[:70]))

    if args.coverage:
        print()
        print("Read '-' as 'not shown by any string', NOT as 'definitely missing': the")
        print("rule may be satisfied in code that speaks a variable rather than a literal.")
        return 0

    if args.check:
        print("adapter speech check: %d file(s), %d violation(s)"
              % (len(present), len(violations)))
        for v in violations:
            print("   %s" % v)
        if violations:
            return 1
        print("PASS: no mechanical speech-rule violations")
        return 0

    print()
    print("total explicit Say(.., group, priority) sites: %d" % total)
    print()
    print("⛔ A line spoken High jumps dedup and rate limits. Right for a player request,")
    print("   wrong for an automatic one. Use --check to gate that, --coverage for the")
    print("   design rules each adapter follows.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
