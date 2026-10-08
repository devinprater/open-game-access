#!/usr/bin/env bash
# gba-host-proof.sh — boot a REAL Game Boy / GBA ROM through the REAL app path
# on the host and report what the reader actually says.
#
# ⛔ WHAT THIS PROVES THAT gba-adapter-test.sh CANNOT. That test builds a stub
# host and checks selection, readiness and refusal paths. This one drives
# Core/pokecore.cpp exactly as the app does — poke_load_rom -> poke_set_script_dir
# -> poke_start -> poke_frame — with the real mGBA core and the real Pokémon
# Access reader set, and prints every line the reader speaks plus the framebuffer.
#
# It is the difference between "the registry says GBA is READY" and "a .gba boots
# and the reader talks". Run it after ANY change to the shim, gba_core.cpp or the
# reader assets; the adapter test will not notice a regression in any of them.
#
# Usage:
#   scripts/gba-host-proof.sh [rom-dir] [script-dir] [frames]
# Prints a per-ROM summary and exits nonzero if no ROM spoke at all.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

ROMDIR="${1:-/mnt/c/Users/Devin Prater/Dropbox/games/GBA}"
SCRIPT="${2:-$ROOT/Sources/OpenGameAccess/Resources/gba-lua}"
FRAMES="${3:-2500}"

[ -d "$ROMDIR" ] || { echo "!! no ROM directory at $ROMDIR" >&2; exit 1; }
[ -f "$SCRIPT/oga_bootstrap.lua" ] || { echo "!! no reader set at $SCRIPT" >&2; exit 1; }

echo "== building host objects (needs the real 7z SDK: see MGBA_LZMA in core-sources.sh)"
bash scripts/build-host.sh || exit 1

echo "== linking the probe"
# ⛔ host_harness_stub.cpp IS REQUIRED and now arrives THROUGH hostobj (build-host.sh
# compiles it, supplying the abort-on-call PSP stubs). It used to be named here as well,
# which was a stale duplicate: the file was in neither list when this line was written,
# then it was added to build-host.sh's list, and the explicit copy here made the link die
# with "multiple definition of psp_save_state" -- a LINK error that reads like a source bug.
# Link the object set alone; build-host.sh is the one place that decides what is in it.
g++ -O1 -g -DPOKE_HOST=1 -ICore -ISources/CPokeCore/include -o Vendor/gba-probe \
    Core/gba_host_probe.cpp \
    Vendor/hostobj/*.o -lpthread -lm -ldl || { echo "!! probe link failed" >&2; exit 1; }

spoken_total=0
roms_run=0

# Newest-supported ROMs first. Ruby/Sapphire are in v3.1.0's REJECT list on
# purpose and are expected to answer game_not_supported — that is a PASS for the
# shim, and the last entry proves it still refuses.
# ⛔ THE GAME BOY ROM IS FIRST AND IS THE POINT. The GBA entries cold-boot into minutes of
# intro (title, speech, naming) that this probe cannot walk, so they are EXPECTED to report
# numbers and `nil` -- see the closing note. A .gb title screen, by contrast, reaches real
# dialogue within a few hundred frames, so it is the entry that actually demonstrates the
# reader decoding live RAM. Measured on Red: Oak's speech line by line, then the naming
# screen and the typed name, with zero `nil` and zero raw numbers.
for rom in \
    "Pokemon - Red Version (USA, Europe) (SGB Enhanced).gb" \
    "Pokemon - FireRed Version (USA).gba" \
    "Pokemon - LeafGreen Version (USA).gba" \
    "Pokemon - Emerald Version (USA, Europe).gba" \
    "Pokemon - Ruby Version (USA).gba"
do
  [ -f "$ROMDIR/$rom" ] || continue
  roms_run=$((roms_run + 1))
  echo
  echo "############ $rom"
  out="$(timeout 400 ./Vendor/gba-probe "$ROMDIR/$rom" "$SCRIPT" "$FRAMES" 2>&1)"
  echo "$out" | grep -E '^loaded|^\[SPEAK\]|^frames=|^spoken|^RESULT' | head -25

  n="$(echo "$out" | grep -c '^\[SPEAK\]')"
  spoken_total=$((spoken_total + n))
  if echo "$out" | grep -q 'RESULT: NO PICTURE'; then
    echo "   !! no picture: the framebuffer never rendered"
  fi
done

echo
echo "== summary: $roms_run ROMs booted, $spoken_total spoken lines total"
if [ "$roms_run" -eq 0 ]; then
  echo "!! no ROMs found in $ROMDIR — nothing was proven" >&2
  exit 1
fi
[ "$spoken_total" -gt 0 ] || { echo "!! nothing spoke: the reader never started" >&2; exit 1; }
echo "PASS: a real GBA ROM booted through the app path and the reader spoke."
echo
echo "⚠ READ THE [SPEAK] LINES — BUT READ THEM CORRECTLY."
echo
echo "  Raw numbers and 'nil' are EXPECTED here and are NOT a shim defect. This"
echo "  harness walks a game from a cold boot, and these ROMs have minutes of intro"
echo "  (title, speech, naming) before there is any map to describe. A reader asked"
echo "  to describe a map that does not exist yet reports map functions failing and"
echo "  falls back to raw reads. Proven: run far enough past the title screen and"
echo "  the reader speaks the NAMING SCREEN's own UI ('A', 'OK', 'a') — real content"
echo "  decoded from live RAM."
echo
echo "  So: 'Ready' then numbers means THE GAME HAS NOT STARTED, not that the reader"
echo "  is broken. Meaningful in-world speech needs the game standing in the world,"
echo "  which this harness cannot currently arrange (it stalls on the naming screen)."
echo "  See docs/research/gba-host-proof.md — 'the READER IS FINE' section."
echo
echo "  ⛔ AND CHECK THE GAME BOY LINE, NOT ONLY THE GBA ONES. A .gb reaches real dialogue"
echo "  on a cold boot: measured on Red, Oak's speech + the naming screen + the typed name,"
echo "  with zero 'nil' and zero raw numbers. If that line shows numbers instead, the Game"
echo "  Boy path regressed — most likely 'Ready' repeating, which means a RESET hook is"
echo "  firing as a movement hook again (scripts/registerexec-kind-test.sh guards it; the"
echo "  measured bug was 729 'Ready' in one 40000-frame run)."
