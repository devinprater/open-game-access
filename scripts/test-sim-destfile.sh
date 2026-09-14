#!/usr/bin/env bash
# test-sim-destfile.sh — prove the SwiftPM destination-file cross-compile works,
# on THIS machine, before spending another CI cycle on it.
#
# Forcing the path that CI takes: a pre-linked binary exists here, so the packaging
# script would skip the swift build entirely and never exercise the fix. This hides
# the binary, runs the real packaging script, and reports whether it produced a
# simulator binary.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SDK="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk"
BIN_DIR="$ROOT/.build/arm64-apple-ios-simulator/debug"
BIN="$BIN_DIR/PokemonAccess-App"
HIDE="$BIN_DIR/PokemonAccess-App.hidden"

[ -d "$SDK" ] || { echo "!! no SDK at $SDK" >&2; exit 1; }

restore() {
  [ -f "$HIDE" ] && mv "$HIDE" "$BIN" && echo "(restored the pre-linked binary)"
}
trap restore EXIT

if [ -f "$BIN" ]; then
  mv "$BIN" "$HIDE"
  echo "== hid the pre-linked binary to force the swift-build path =="
fi

echo
echo "== running the real packaging script with SDKROOT set =="
cd "$ROOT" || exit 1
SDKROOT="$SDK" bash scripts/package-sim-app.sh 2>&1 | tail -40
echo
echo "== exit: $? =="
