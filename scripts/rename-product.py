#!/usr/bin/env python3
"""rename-product.py — rename the PRODUCT from OpenGameAccess to OpenGameAccess.

⛔ WHAT THIS DELIBERATELY DOES NOT TOUCH, and why:

  * `poke_*` C symbols (577 references). They are the ABI between the C++ core,
    the Swift app and the host harnesses. Renaming them buys nothing a user can see
    and risks breaking the interface that the Lua shim and every instrument links
    against. The product NAME is not the plumbing.
  * `CPokeCore` / `pokecore.h` — internal module and header names, likewise an
    interface rather than a brand.
  * `*.lua` — the reader scripts are Ola's work and their text is not ours to
    rewrite. A blanket substitution would also silently edit script content that
    talks about the game, not the app.
  * `*.md` prose — attribution lines ("Ola and the Open Game Access project") refer to
    a DIFFERENT project and must not be renamed. Docs are patched by hand, after
    this script, where the reference is to OUR app.

What it does change: everything a player or a reader of the repo actually sees —
package, product, target, source directory, bundle identifier, executable and app
name, window title, on-screen text, and the emulator version string.
"""
import os
import pathlib
import sys

ROOT = pathlib.Path(os.environ.get("REPO", "/mnt/c/Users/Devin Prater/open-game-access"))
APPLY = os.environ.get("APPLY") == "1"

SKIP_DIRS = {".git", "Vendor", ".build", "obj", "xtool", "xtool-sim",
             "rom-screen", "node_modules", ".swiftpm", "build", ".cxx"}

TEXT_EXT = {".swift", ".c", ".cc", ".cpp", ".h", ".hpp", ".m", ".mm", ".sh",
            ".py", ".yml", ".yaml", ".toml", ".plist", ".json", ".txt"}

# Longest / most specific first, so a shorter rule cannot clip a longer one.
SUBS = [
    # SwiftPM derives the resource bundle name from package_target, so the doubled
    # form must be rewritten before the single-token form.
    ("OpenGameAccess_OpenGameAccess.bundle", "OpenGameAccess_OpenGameAccess.bundle"),
    ("OpenGameAccess-App", "OpenGameAccess-App"),
    ("OpenGameAccess.app", "OpenGameAccess.app"),
    ("Sources/OpenGameAccess", "Sources/OpenGameAccess"),
    ("com.devinprater.opengameaccess", "com.devinprater.opengameaccess"),
    # The local directory name — scripts address the repo by full Windows path.
    ("open-game-access", "open-game-access"),
    ("OpenGameAccess", "OpenGameAccess"),
    # User-visible strings, ASCII and accented.
    ("Open Game Access", "Open Game Access"),
    ("Open Game Access", "Open Game Access"),
    ("Open Game Access", "Open Game Access"),
    ("Open Game Access", "Open Game Access"),
]


def wanted(p: pathlib.Path) -> bool:
    if any(part in SKIP_DIRS for part in p.parts):
        return False
    return p.suffix in TEXT_EXT


def main() -> int:
    files = sorted(p for p in ROOT.rglob("*") if p.is_file() and wanted(p))
    print(f"repo : {ROOT}")
    print(f"mode : {'APPLY' if APPLY else 'DRY RUN (set APPLY=1)'}")
    print(f"scanned {len(files)} text files (excluding .lua, .md, Vendor, .build)")

    changed = []
    for p in files:
        try:
            s = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        new = s
        for old, rep in SUBS:
            new = new.replace(old, rep)
        if new != s:
            changed.append(p)
            if APPLY:
                p.write_text(new, encoding="utf-8")

    print()
    print(f"{'changed' if APPLY else 'would change'}: {len(changed)} file(s)")
    for p in changed:
        print("   ", p.relative_to(ROOT))

    # The source directory itself must move for the target path to resolve.
    old_dir = ROOT / "Sources" / "OpenGameAccess"
    new_dir = ROOT / "Sources" / "OpenGameAccess"
    if old_dir.is_dir():
        print()
        print(f"{'moving' if APPLY else 'would move'}: Sources/OpenGameAccess -> Sources/OpenGameAccess")
        if APPLY:
            old_dir.rename(new_dir)

    print()
    print("== residual 'OpenGameAccess' outside excluded paths (.lua/.md/.git/Vendor) ==")
    left = 0
    for p in files:
        try:
            s = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if "OpenGameAccess" in s:
            print("  ", p.relative_to(ROOT))
            left += 1
    if not left:
        print("   none")

    print()
    print("== deliberately preserved (internal ABI / third-party) ==")
    print("   poke_* C symbols, CPokeCore module, pokecore.h/cpp, *.lua, *.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
