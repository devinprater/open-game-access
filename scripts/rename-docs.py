#!/usr/bin/env python3
"""rename-docs.py — update docs to the new product identity.

Only path/identifier references and OUR product name change. Attribution lines
naming Ola's project ("the Pokémon Access project", "Ola's `main.lua`") refer to a
DIFFERENT project and are left exactly as written — rewriting them would falsify
credit, which is worse than a stale name.
"""
import pathlib

PATH_SUBS = [
    ("Sources/PokemonAccess/Resources", "Sources/OpenGameAccess/Resources"),
    ("com.devinprater.pokemonaccess", "com.devinprater.opengameaccess"),
    ("PokemonAccess-simulator.zip", "OpenGameAccess-simulator.zip"),
    ("PokemonAccess.app", "OpenGameAccess.app"),
    ("PokemonAccess-App", "OpenGameAccess-App"),
    ("pokemon-access-ios", "open-game-access"),
    # OUR app referred to by the old name in prose.
    ("Pokémon Access Mobile", "Open Game Access"),
    ("Pokémon Access iOS", "Open Game Access"),
]

FILES = [
    "README.md", "STATUS-AND-TEST-PLAN.md", "TIER1-RESEARCH-STATUS.md",
    "NDS-GAME-FEASIBILITY.md",
    "docs/building.md", "docs/current-architecture.md",
    "docs/RELEASE-NOTES-v0.1.0.md", "docs/STATUS.md", "docs/device-access-wsl.md",
    "docs/fire-emblem-shadow-dragon-memory.md",
]

here = pathlib.Path(__file__).resolve().parent.parent
for name in FILES:
    p = here / name
    if not p.exists():
        continue
    s = p.read_text(encoding="utf-8")
    before = s
    for old, rep in PATH_SUBS:
        s = s.replace(old, rep)
    if s != before:
        p.write_text(s, encoding="utf-8")
        print("patched", name)
    else:
        print("unchanged", name)

print()
print("== attribution must still name Ola's project ==")
for name in ("README.md", "docs/RELEASE-NOTES-v0.1.0.md"):
    p = here / name
    if not p.exists():
        continue
    for i, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
        if "Pokémon Access project" in line or "Ola" in line:
            print(f"  {name}:{i}: {line.strip()[:110]}")
