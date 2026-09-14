#!/usr/bin/env bash
# build-sim-app.sh — produce a complete iOS-Simulator .app bundle.
#
# Two stages, because they fail for different reasons and the distinction
# matters when one of them breaks:
#   1. the core archive for arm64-apple-ios17.0-simulator (build-sim.sh)
#   2. the SwiftUI app linked against it    (xtool dev build --triple ...)
#
# An .app built this way is what a macOS host installs with
# `xcrun simctl install booted <app>` — xtool's own simulator path is macOS-only
# (`#if os(macOS)` around the simctl call), which is why the artifact is produced
# here and handed over rather than run here.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
cd "$ROOT" || exit 1
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
TRIPLE="${TRIPLE:-arm64-apple-ios-simulator}"

SIMLIB="$ROOT/Vendor/sim/libpokecore-sim.a"
[ -f "$SIMLIB" ] || { echo "== simulator core archive missing, building it"; bash "$ROOT/scripts/build-sim.sh" || exit 1; }
[ -f "$SIMLIB" ] || { echo "!! still no $SIMLIB" >&2; exit 1; }
echo "== simulator core: $(ls -la "$SIMLIB" | awk '{print $5}') bytes, $(/usr/local/swift/bin/llvm-nm -g "$SIMLIB" | grep -c ' T _poke_') poke symbols"

export POKECORE_LIB="$SIMLIB"
echo "== POKECORE_LIB=$POKECORE_LIB"

# xtool writes to ./xtool; keep the device bundle so the two do not clobber.
rm -rf "$ROOT/xtool-sim"
timeout 900 xtool dev build --triple "$TRIPLE" 2>&1 | tail -30
echo "EXIT=${PIPESTATUS[0]}"

# xtool's packaging writes the .app only for the DEVICE triple, so the simulator
# bundle is assembled by package-sim-app.sh from the linked binary + resources.
bash "$ROOT/scripts/package-sim-app.sh"
