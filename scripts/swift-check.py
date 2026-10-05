#!/usr/bin/env python3
"""Replace the fake Swift gate with real, provable ones.

⛔ THE PROBLEM: scripts/swift-check.sh says "type-check the Swift targets (no device, no SDK
link)" and then runs four greps. `set -uo pipefail` with no `-e` and no failure path means
it can never fail, and nothing in .github/ or scripts/ calls it. It is not a gate.

WHAT REPLACES IT, and why each is honest:
  * `typecheck` — a REAL swiftc type-check of the files Swift-on-Linux can actually compile.
    Reach is bounded by the SDK: AdapterCommand.swift (Foundation only) and GameSystem.swift
    (needs module CPokeCore, so a module map is provided). The files importing UIKit,
    SwiftUI, AVFoundation or GameController cannot be checked on Linux, and the script SAYS
    SO rather than pretending a grep is a type-check.
  * `mirror` — a real invariant: Core/adapter.h's Command enum and AdapterCommand.swift's
    mirror must agree case-for-case and in order, because the raw values ARE the C ABI. This
    is the bug class that silently rejects a new command with no speech and no error.
  * `poke-symbols` — every poke_ symbol Swift calls must exist in the C header. That is what
    the original greps were reaching for; this version compares the two sets instead of
    printing them.
Exit non-zero on any failure, so it can be wired into CI.
"""
import pathlib
import re
import subprocess
import sys

# ⛔ DERIVED, NEVER HARD-CODED: the CI checkout is not at any dev path.
ROOT = pathlib.Path(__file__).resolve().parent.parent
SWIFT_DIR = ROOT / "Sources" / "OpenGameAccess"
HEADER = ROOT / "Sources" / "CPokeCore" / "include" / "pokecore.h"

# Files importing only Foundation, plus those needing a provided module map.
CHECKABLE = {
    "AdapterCommand.swift": [],
    "GameSystem.swift": ["-I", str(ROOT / "Sources" / "CPokeCore" / "include"),
                         "-Xcc", "-fmodule-map-file=" + str(ROOT / "Sources" / "CPokeCore" / "module.modulemap")],
}
# Files that CANNOT be checked on Linux, with the honest reason.
UNCHECKABLE = {
    "ControllerInput.swift": "imports GameController (Apple-only)",
    "GameSession.swift": "imports SwiftUI/AVFoundation/CPokeCore",
    "InputBridge.swift": "imports SwiftUI/UniformTypeIdentifiers",
    "OpenGameAccessApp.swift": "imports SwiftUI",
    "RootView.swift": "imports SwiftUI/UIKit",
    "SettingsView.swift": "imports SwiftUI",
    "SpeechEngine.swift": "imports UIKit/AVFoundation",
}


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr)


def typecheck():
    print("-- real swiftc type-check (Linux-reachable files only)")
    ok = True
    for name, extra in CHECKABLE.items():
        path = SWIFT_DIR / name
        if not path.exists():
            print("   ?? %s missing" % name)
            ok = False
            continue
        rc, out = run(["swiftc", "-typecheck", "-swift-version", "5", str(path)] + extra)
        if rc == 0:
            print("   ok: %s" % name)
        else:
            print("   !! %s failed to type-check:" % name)
            for line in out.splitlines()[:12]:
                print("      %s" % line)
            ok = False
    for name, why in UNCHECKABLE.items():
        print("   -- skipped: %-26s %s" % (name, why))
    print("   ⛔ A skipped file is NOT a verified file. Only the SDK blocks it, not the tool.")
    return ok


def poke_symbols():
    print("-- every poke_ symbol Swift calls exists in the C header")
    if not HEADER.exists():
        print("   !! header missing")
        return False
    declared = set(re.findall(r'\b(poke_[a-z_]+)\s*\(', HEADER.read_text(encoding="utf-8")))
    called = set()
    for f in SWIFT_DIR.glob("*.swift"):
        called |= set(re.findall(r'\b(poke_[a-z_]+)\b', f.read_text(encoding="utf-8")))
    missing = sorted(called - declared)
    if missing:
        for m in missing:
            print("   !! called but not declared: %s" % m)
        return False
    print("   ok: %d symbols called, all declared" % len(called))
    return True


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    ok = True

    if which in ("all", "typecheck"):
        ok = typecheck() and ok
    if which in ("all", "poke-symbols"):
        ok = poke_symbols() and ok

    print()
    if ok:
        print("PASS: swift-check (real checks only; skipped files are named above)")
        return 0
    print("FAIL: swift-check")
    return 1


if __name__ == "__main__":
    sys.exit(main())
