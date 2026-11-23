#!/usr/bin/env bash
# snes-alttp-proof.sh — the SNES console's host proof.
#
# Modes, one per task in docs/plans/alttp-snes-console.md:
#   --identity --run N --input --reset --reads --resolve --states   (core probe)
#   --live --live-cases                                             (app path)
#
# The script BUILDS what it runs every time, so a stale archive cannot make a
# failed change look green.
#
# ⛔ The ROM is Devin's own dump, read from outside the repo. Nothing here copies
# it in, and scripts/check-no-roms.sh stays honest.
#
# MEASURED: a .smc with the 512-byte copier header reports the SAME payload CRC
# as the headerless file (777aac2f). Mesen hashed the whole file (f59f60d1) until
# the core began hashing the payload; see the note beside CrcTable() in
# Core/snes_core.cpp.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESEN="${MESEN_SRC:-$HOME/src/mesen}"
OUT="${OUT:-$HOME/oga-snes-proof}"
ROM="${ALTP_ROM:-/mnt/c/Users/Devin Prater/Dropbox/games/snes/Legend of Zelda, The - A Link to the Past (USA).sfc}"
OTHER_ROM="${ALTP_OTHER_ROM:-/mnt/c/Users/Devin Prater/Dropbox/games/snes/Chrono Trigger (USA).sfc}"
SAVE="${ALTP_SAVE:-$HOME/z3work/alttp-home}"
MODE="${1:---identity}"

[ -d "$MESEN/Core" ] || { echo "!! no Mesen source at $MESEN" >&2; exit 1; }
[ -f "$ROM" ]        || { echo "!! no ROM at $ROM (set ALTP_ROM)" >&2; exit 1; }
[ -d "$ROOT/Vendor/hostobj" ] || { echo "!! run scripts/build-host.sh first" >&2; exit 1; }

# ⛔ STALENESS GUARD (2026-11-22): the archive below is only as fresh as the
# host objects it packs, and nothing refreshed those objects when a Core
# source changed. The Phase C indoor guide sat finished in the sources
# while this script linked a pre-guide adapter object (its SpeakGuideIndoors
# was still refusal-only, and alttp_dungeon_graph.o did not exist yet) and
# reported the old object's refusal as the game's answer — the adapter unit
# tests, which compile from source, were green the whole time. If any Core
# source or header is newer than the archive, refresh the host objects
# first (build-host.sh is incremental and decides per file); the re-archive
# check below then packs whatever changed.
if [ -d "$HOME/src/melonds-lua" ] && \
   { [ ! -f "$OUT/libmesen.a" ] || \
     [ -n "$(find "$ROOT/Core" \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -newer "$OUT/libmesen.a" -print -quit)" ]; }; then
  "$ROOT/scripts/build-host.sh" || { echo "!! build-host.sh failed" >&2; exit 1; }
fi

mkdir -p "$OUT" "$SAVE"

# The Mesen object set is shared with every other host harness. Re-archive only
# when a build is newer than the archive.
if [ ! -f "$OUT/libmesen.a" ] || \
   [ -n "$(find "$ROOT/Vendor/hostobj" -name '*.o' -newer "$OUT/libmesen.a" -print -quit)" ]; then
  rm -f "$OUT/libmesen.a"
  ar rcs "$OUT/libmesen.a" "$ROOT"/Vendor/hostobj/*.o || { echo "!! ar failed" >&2; exit 1; }
fi

CXXFLAGS=(-O1 -g -std=c++17 -w -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1)
FFMPEG="$HOME/ffmpeg-host/lib"

link() { # <objects...> -o <out>
  g++ "$@" -lz -lpthread -ldl -lm \
    "$FFMPEG/libavformat.a" "$FFMPEG/libavcodec.a" "$FFMPEG/libswresample.a" \
    "$FFMPEG/libswscale.a" "$FFMPEG/libavutil.a"
}

build_core() {
  g++ "${CXXFLAGS[@]}" \
      -I"$ROOT/Core" -I"$MESEN" -I"$MESEN/Core" -I"$MESEN/Utilities" \
      -include "$MESEN/Core/pch.h" \
      -c "$ROOT/Core/snes_core.cpp" -o "$OUT/snes_core.o" || { echo "!! compile snes_core.cpp" >&2; exit 1; }
}

build_probe() {
  build_core
  # The probe is plain C++ against the core's public headers: no Mesen includes,
  # so a leak of Mesen types into it is a compile error rather than a habit.
  g++ "${CXXFLAGS[@]}" -I"$ROOT/Core" \
      -c "$ROOT/Core/snes_probe.cpp" -o "$OUT/snes_probe.o" || { echo "!! compile snes_probe.cpp" >&2; exit 1; }
  link "$OUT/snes_probe.o" "$OUT/snes_core.o" "$OUT/libmesen.a" -o "$OUT/snes-probe" \
      > "$OUT/link.log" 2>&1 || { echo "!! link snes-probe" >&2; tail -n 8 "$OUT/link.log" >&2; exit 1; }
}

build_live() {
  # The probe binary carries snes_core.o from THIS source, so the proof never
  # links a stale archive member after a core change.
  build_core
  g++ "${CXXFLAGS[@]}" -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
      -c "$ROOT/scripts/snes-live-proof.cpp" -o "$OUT/snes_live_proof.o" \
      || { echo "!! compile snes-live-proof.cpp" >&2; exit 1; }
  link "$OUT/snes_live_proof.o" "$OUT/snes_core.o" "$OUT/libmesen.a" -o "$OUT/snes-live-proof" \
      > "$OUT/live-link.log" 2>&1 || { echo "!! link snes-live-proof" >&2; tail -n 8 "$OUT/live-link.log" >&2; exit 1; }
}

echo "== $MODE"
case "$MODE" in
  --live|--live-cases|--teardown|--menus|--files|--walk|--autowalk|--dialogue|--darkobj) build_live ;;
  *)                   build_probe ;;
esac

case "$MODE" in
  --identity|--run|--input|--reset|--reads|--resolve|--states|--after-stop|--memory|--drive|--gameplay|--explore|--leave|--wander|--resume|--measure|--text|--keytest|--file|--walkmap|--walklease|--walkdiag|--msgdiag|--deathdiag|--spritedump|--guardmap)
    exec "$OUT/snes-probe" "$MODE" "$ROM" "$SAVE" "${@:2}" ;;
  --headered)
    HEADERED="$HOME/z3work/alttp-headered.smc"
    dd if=/dev/zero bs=512 count=1 status=none > "$HEADERED"
    cat "$ROM" >> "$HEADERED"
    exec "$OUT/snes-probe" --headered "$ROM" "$SAVE" "$HEADERED" ;;
  --live)
    exec "$OUT/snes-live-proof" "$ROM" "$SAVE" "crc:777aac2f" ;;
  --menus)
    exec "$OUT/snes-live-proof" --menus "$ROM" "$SAVE" ;;
  --files)
    exec "$OUT/snes-live-proof" --files "$ROM" "$SAVE" ;;
  --walk)
    exec "$OUT/snes-live-proof" --walk "$ROM" "$SAVE" ;;
  --autowalk)
    exec "$OUT/snes-live-proof" --autowalk "$ROM" "$SAVE" ;;
  --darkobj)
    exec "$OUT/snes-live-proof" --darkobj "$ROM" "$SAVE" ;;
  --dungeon)
    # The probe steers Link into the Eastern Palace and saves a state at
    # the door; the live proof then loads it through the app path.
    DSTATE="$OUT/ep-entrance.state"
    "$OUT/snes-probe" --dungeon "$ROM" "$SAVE" "$DSTATE" || { echo "!! dungeon walk failed" >&2; exit 1; }
    build_live
    exec "$OUT/snes-live-proof" --dungeon "$ROM" "$SAVE" "$DSTATE" ;;
  --dialogue)
    # ⛔ A NEW FILE, NOT THE PUBLIC SAVE. The intro's telepathy box is the one
    # story message this harness can raise without steering; the save has long
    # passed it. The temp dir keeps the real Saves/ untouched.
    NOSAVE="$OUT/nosave-dialogue"
    rm -rf "$NOSAVE" && mkdir -p "$NOSAVE"
    exec "$OUT/snes-live-proof" --dialogue "$ROM" "$NOSAVE" ;;
  --teardown)
    [ -f "$OTHER_ROM" ] || { echo "!! no second SNES ROM at $OTHER_ROM" >&2; exit 1; }
    exec "$OUT/snes-live-proof" --teardown "$ROM" "$OTHER_ROM" "$SAVE" ;;
  --live-cases)
    set -e
    cp "$ROM" "$HOME/z3work/alttp-renamed.sfc"
    echo "-- renamed copy"
    "$OUT/snes-live-proof" "$HOME/z3work/alttp-renamed.sfc" "$SAVE" "crc:777aac2f" || exit 1
    echo "-- unknown dump (a different SNES game)"
    [ -f "$OTHER_ROM" ] || { echo "!! no second SNES ROM at $OTHER_ROM (set ALTP_OTHER_ROM)" >&2; exit 1; }
    "$OUT/snes-live-proof" "$OTHER_ROM" "$SAVE" any || exit 1
    echo "-- second load in one process"
    "$OUT/snes-live-proof" --reload "$OTHER_ROM" "$ROM" "$SAVE" || exit 1
    echo "live-cases=YES" ;;
  *)
    echo "!! unknown mode $MODE" >&2
    exit 2 ;;
esac
