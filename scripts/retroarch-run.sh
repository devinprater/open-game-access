#!/usr/bin/env bash
# RetroArch as an Open Game Access harness — the VERIFIED invocation.
#
# Usage:  retroarch-run.sh <core> <rom> <frames> [screenshot.png]
#   core  short name, e.g. snes9x, genesis_plus_gx, mupen64plus_next,
#         mednafen_psx_hw, flycast, melonds, mgba
#   rom   path to the ROM (player-supplied; never committed to this repo)
#
# ---------------------------------------------------------------------------
# WHY THIS SCRIPT EXISTS — the three traps it encodes
# ---------------------------------------------------------------------------
# 1. ⛔ PATHS MUST BE NATIVE WINDOWS PATHS. `-L /c/Users/.../core.dll` (MSYS style,
#    which is what a bash variable naturally gives you) fails with exit 1 and an
#    EMPTY LOG. It looks like "the core was refused". It is not — it is a path
#    format problem. Always pass C:/... .
# 2. ⛔ `--headless` DOES NOT EXIST on RetroArch 1.22.2. The frame driver is
#    `--max-frames=N`. Do not reach for --headless.
# 3. ⛔ `--max-frames=N` IS REAL-TIME PACED — roughly N/60 seconds of wall clock.
#    120 frames took ~40s here. 600 frames blew a 120s timeout. Budget accordingly.
#    This also means a long scripted run is slow: tune video driver / vsync first.
#
# Also: `--verbose` is what makes any of this debuggable. Without it a failure is
# an empty log and an exit code.
# ---------------------------------------------------------------------------
set -uo pipefail

CORE_NAME="${1:?usage: retroarch-run.sh <core> <rom> <frames> [screenshot.png]}"
ROM_IN="${2:?usage: retroarch-run.sh <core> <rom> <frames> [screenshot.png]}"
FRAMES="${3:-300}"
SHOT_OUT="${4:-}"

# scoop is a WINDOWS install; this script must run from the Windows side (git-bash),
# NOT from inside WSL — inside WSL $HOME/scoop is a different, empty directory.
SCOOP="${SCOOP:-$HOME/scoop}"
RA_DIR="$SCOOP/apps/retroarch/current"
RA="$RA_DIR/retroarch.exe"
# follow the symlink to the real persist dir; native binaries need a real path
CORES="$SCOOP/persist/retroarch/cores"

if [ ! -x "$RA" ]; then
  echo "!! retroarch.exe not found at $RA" >&2
  echo "   (running inside WSL? scoop lives on the Windows side — see the skill)" >&2
  exit 1
fi

CORE="$CORES/${CORE_NAME}_libretro.dll"
if [ ! -f "$CORE" ]; then
  echo "!! core not installed: $CORE" >&2
  echo "   install with:" >&2
  echo "     curl -sSL -o /tmp/c.zip \\" >&2
  echo "       https://buildbot.libretro.com/nightly/windows/x86_64/latest/${CORE_NAME}_libretro.dll.zip" >&2
  echo "     unzip -o /tmp/c.zip -d \"$CORES\"" >&2
  exit 1
fi

# Convert an MSYS path (/c/Users/...) to a native one (C:/Users/...).
# Trap 1: passing the MSYS form makes RetroArch exit 1 with an empty log.
native() {
  case "$1" in
    /[a-zA-Z]/*) printf '%s:/%s' "$(echo "${1:1:1}" | tr 'a-z' 'A-Z')" "${1:3}" ;;
    /*)          printf '%s' "$1" ;;
    *)           printf '%s' "$1" ;;
  esac
}

CORE_N="$(native "$CORE")"
ROM_N="$(native "$ROM_IN")"
[ -f "$ROM_N" ] || { echo "!! rom not found: $ROM_N" >&2; exit 1; }

ARGS=(--max-frames="$FRAMES" --verbose -L "$CORE_N" "$ROM_N")
if [ -n "$SHOT_OUT" ]; then
  SHOT_N="$(native "$SHOT_OUT")"
  rm -f "$SHOT_N"
  ARGS=(--max-frames="$FRAMES" --max-frames-ss "--max-frames-ss-path=$SHOT_N" --verbose -L "$CORE_N" "$ROM_N")
fi

echo "== core:  $CORE_N"
echo "== rom:   $ROM_N"
echo "== frames: $FRAMES   (expect roughly $((FRAMES / 60))s wall clock, trap 3)"
[ -n "$SHOT_OUT" ] && echo "== shot:  $SHOT_N"
echo

# run from the RetroArch dir so its relative default paths resolve
cd "$RA_DIR" || exit 1
START=$(date +%s)
timeout $(( (FRAMES / 60) * 4 + 60 )) ./retroarch.exe "${ARGS[@]}" 2>&1 \
  | grep -viE "^\[INFO\] \[(XInput|Input|Environ|SRAM|Video|D3D11|Font|Autodetect)" \
  | tail -25
RC=${PIPESTATUS[0]}
echo
echo "== exit=$RC  elapsed=$(( $(date +%s) - START ))s"

if [ "$RC" -eq 124 ]; then
  echo "!! TIMED OUT — trap 3: --max-frames is real-time paced. Lower the frame count" >&2
  echo "   or disable vsync / switch video driver before asking for long runs." >&2
fi
if [ -n "$SHOT_OUT" ]; then
  [ -f "$SHOT_N" ] && echo "== screenshot: $(stat -c %s "$SHOT_N") bytes -> $SHOT_N" \
                   || echo "!! no screenshot written" >&2
fi
exit "$RC"
