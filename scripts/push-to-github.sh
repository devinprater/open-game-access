#!/usr/bin/env bash
# push-to-github.sh — create the repo and push the staged tree.
#
# ⛔ Runs the no-ROMs guard immediately before pushing, on the exact tree being
# pushed. A clean check earlier is not a clean check now: a file could have been
# added in between, and this is the last moment it can be stopped.
set -euo pipefail
cd "$HOME/open-game-access" || exit 2
WIN_SRC="/mnt/c/Users/Devin Prater/open-game-access"

echo "== no-ROMs guard on the tree about to be pushed =="
bash "$WIN_SRC/scripts/check-no-roms.sh" "$PWD"

echo
echo "== git state =="
git config user.name "devinprater"
git config user.email "devinprater@live.com"
git add -A
echo "staged files: $(git diff --cached --name-only | wc -l)"

# Refuse to push if anything game-shaped slipped in at any depth.
BAD=$(git diff --cached --name-only | grep -iE '\.(nds|dsi|gba|gbc|gb|sav|srm|dsv|state|apk|ipa|a|o|so)$|bios[0-9]?\.bin|firmware\.bin' || true)
if [ -n "$BAD" ]; then
  echo "!! refusing to push — these must not be published:"; echo "$BAD"; exit 1
fi
echo "  no game data or build output staged"

git commit -q -m "$(cat <<'EOF'
Open Game Access: semantic game-state accessibility framework

Reframes pokemon-access-mobile as a general accessibility framework: read what
the game already knows instead of modelling the screen.

Kept working and unchanged: the melonDS + Lua core integration, the memory/speech/
input layers, the VoiceOver-first iOS app, and Ola's Pokémon Black/White reader
(bundled byte-identical).

New in this commit:
- Core/adapter.h + adapters.cpp — the adapter seam, keyed on ROM game code.
- Fire Emblem: Shadow Dragon adapter. Milestone 1: tactical cursor and unit array
  read from the game's own structures, character identity from its own identifier
  strings (PID_MARS / JID_LORD). Verified against a running emulator; what is
  unverified is documented as unverified.
- Reverse-engineering instruments: probe (scripted input + paired RAM snapshots and
  screenshots), ramscan, ramwatch, ramdiff, cursorstruct.
- docs/: current architecture, the FE11 memory map with per-field verification
  method, build instructions, device access on Windows/WSL, release notes.
- CI for both platforms, and scripts/check-no-roms.sh as a hard guard.

No ROMs, BIOS, firmware, saves or emulator source are included; the core is
fetched at a pinned revision by scripts/bootstrap-deps.sh.
EOF
)"
echo "committed: $(git log --oneline -1)"
