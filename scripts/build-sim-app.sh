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
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
export PATH=/usr/local/swift/bin:/usr/local/bin:$PATH
TRIPLE="${TRIPLE:-arm64-apple-ios-simulator}"

SIMLIB="$ROOT/Vendor/sim/libpokecore-sim.a"
# Always (re)build: build-sim.sh's object cache makes an up-to-date archive a
# few seconds, and building only when the archive was MISSING linked the app
# against a stale core (undefined poke_* symbols added since it was made).
echo "== simulator core (cached rebuild)"
bash "$ROOT/scripts/build-sim.sh" || exit 1
[ -f "$SIMLIB" ] || { echo "!! still no $SIMLIB" >&2; exit 1; }
# llvm-nm, never GNU nm: GNU nm cannot read Mach-O and reports 0.
LLVM_NM="$(command -v llvm-nm || echo /usr/local/swift/bin/llvm-nm)"
echo "== simulator core: $(ls -la "$SIMLIB" | awk '{print $5}') bytes, $("$LLVM_NM" -g "$SIMLIB" | grep -c ' T _poke_') poke symbols"

export POKECORE_LIB="$SIMLIB"
echo "== POKECORE_LIB=$POKECORE_LIB"

# ⛔ xtool writes ./xtool/OpenGameAccess.app for BOTH triples — the earlier
# comment claiming "--triple keeps the two apart" was wrong, and this script
# silently overwrote the DEVICE bundle with a simulator binary (a device app
# wearing the wrong platform, which `verify-device.sh` now rejects). Remove
# xtool-sim before stashing, not before building.
#
# The device bundle is therefore moved aside, the sim build runs, its output is
# claimed for xtool-sim, and the device bundle is put back where it belongs.
DEVICE_APP="$ROOT/xtool/OpenGameAccess.app"
DEVICE_STASH=""
rm -rf "$ROOT/xtool-sim"
if [ -d "$DEVICE_APP" ]; then
  DEVICE_STASH="$(mktemp -d)/OpenGameAccess.app"
  mv "$DEVICE_APP" "$DEVICE_STASH" || { echo "!! could not stash the device bundle" >&2; exit 1; }
  echo "== stashed the device bundle; the sim build would otherwise overwrite it"
fi
restore_device() {
  [ -n "$DEVICE_STASH" ] || return 0
  rm -rf "$DEVICE_APP"
  mv "$DEVICE_STASH" "$DEVICE_APP" 2>/dev/null || true
}
trap restore_device EXIT

timeout 900 xtool dev build --triple "$TRIPLE" 2>&1 | tail -30
BUILD_EXIT="${PIPESTATUS[0]}"
echo "EXIT=$BUILD_EXIT"

# Claim whatever xtool produced for the simulator before the device bundle goes
# back: package-sim-app.sh reads the linked binary, and this is the only chance
# to keep the two apart.
mkdir -p "$ROOT/xtool-sim"
if [ -d "$DEVICE_APP" ]; then
  cp -a "$DEVICE_APP" "$ROOT/xtool-sim/OpenGameAccess.app"
fi
restore_device
trap - EXIT

# xtool's packaging writes the .app only for the DEVICE triple, so the simulator
# bundle is assembled by package-sim-app.sh from the linked binary + resources.
bash "$ROOT/scripts/package-sim-app.sh"
