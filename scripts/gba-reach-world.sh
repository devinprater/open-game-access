#!/usr/bin/env bash
# gba-reach-world.sh — get the GBA reader STANDING IN THE WORLD, so in-world speech can be
# observed at all.
#
# ⛔ THE PROBLEM THIS SOLVES. Every GBA verification so far stopped at the title screen, and the
# doc blamed the intro ("the harness cannot currently arrange it"). That is a HARNESS cost
# problem, not a game problem: the real reader (pokemon.lua -> gba.lua) reads the whole 360-byte
# screen and runs ~38 predicates EVERY FRAME, so a measured run advanced ~1000 frames in ~5
# minutes. Emerald's intro is minutes of emulated time, so walking it with the reader loaded
# would take HOURS per attempt.
#
# ✅ THE TRICK: walk the intro with a NO-OP script, then replay the world with the REAL reader.
#   1. Point OGA_READER_DIR at a directory holding just mgba_compat.lua (the real shim, so
#      `emu` / `memory` / frameadvance work) and a 3-line pokemon.lua that only advances frames.
#      The core is byte-identical to the app's; only the script is a stand-in.
#   2. Drive the intro with the probe's button profiles and capture a savestate.
#      Measured: 120000 frames in ~20 s (vs hours), reaching the player's house.
#   3. Resume that state with the REAL reader: `--state-in <state>` with no OGA_READER_DIR.
#      The reader boots straight into the world with meaningful state.
#
# ⛔ SCREENSHOTS ARE PART OF THE PROOF, NOT DECORATION. The project has been misled more than
# once by inferring game state from RAM alone; `--shot-every` makes the probe write PPMs and
# scripts/ppm2png.py converts them so the frames can be LOOKED at. This is how the walk was
# confirmed to reach the house rather than assumed to.
#
# Usage: scripts/gba-reach-world.sh <rom.gba> [out-state] [walker-frames]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

ROM="${1:?usage: gba-reach-world.sh <rom.gba> [out-state] [frames]}"
OUT="${2:-$HOME/oga-world.state}"
FRAMES="${3:-140000}"
READER="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
WORK="${WORK:-$HOME/oga-walker}"

[ -f "$ROM" ] || { echo "!! no ROM at $ROM" >&2; exit 1; }
[ -x "$ROOT/Vendor/gba-probe" ] || { echo "!! build the probe first (see docs/research/gba-host-proof.md)" >&2; exit 1; }

echo "== building the no-op walker (real shim + frame-advancing pokemon.lua)"
mkdir -p "$WORK"
cp "$READER/mgba_compat.lua" "$WORK/"
cat > "$WORK/pokemon.lua" <<'LUA'
-- THROWAWAY stand-in for the real reader: advance frames only. The shim is real, so `emu` is
-- the BizHawk-shaped table and emu.frameadvance() runs a frame; the host presses buttons.
while true do emu.frameadvance() end
LUA

echo "== walking the intro to the world ($FRAMES frames, screenshots every 20000)"
mkdir -p "$HOME/oga-shots"
OGA_READER_DIR="$WORK" timeout 900 "$ROOT/Vendor/gba-probe" "$ROM" "$READER" "$FRAMES" \
  --profile 1 --state-out "$OUT" --shot-every 20000 --shot-out "$HOME/oga-shots/walk" > "$WORK/walk.log" 2>&1
rc=$?
grep -E "shot ->|frames=|saved state" "$WORK/walk.log" | tail -10
[ "$rc" -eq 0 ] || { echo "!! walk failed (rc=$rc); see $WORK/walk.log" >&2; exit 1; }
[ -s "$OUT" ] || { echo "!! no savestate written to $OUT" >&2; exit 1; }

echo
echo "savestate: $OUT ($(stat -c%s "$OUT") bytes)"
echo
echo "== LOOK AT THE SCREENSHOTS BEFORE TRUSTING THIS"
echo "   scripts/ppm2png.py is a converter; the walk is only proven to have reached the world"
echo "   if a picture shows the overworld rather than a title or a blank frame."
echo
echo "== next: resume with the REAL reader (no OGA_READER_DIR)"
echo "   Vendor/gba-probe \"$ROM\" \"$READER\" 1200 --state-in \"$OUT\""
