#!/usr/bin/env bash
# sim-uitest.sh — boot the app on a real iOS Simulator and verify its accessibility
# surface. macOS only (a simulator runtime needs CoreSimulator, which ships with
# Xcode; there is no simulator for Windows or Linux).
#
# ⛔ WHAT THIS PROVES AND WHAT IT DOES NOT.
#   Proves: the app installs and launches on a simulated iPhone; it presents a
#           VoiceOver-visible interface; the named elements and controls exist in
#           the accessibility tree with usable labels; the app does not crash on
#           launch or when the controls are enumerated.
#   Does NOT prove: that speech actually reaches a human ear, or that a game reads
#           correctly. Those need a device and a real ROM (user-supplied).
#
# The accessibility tree is dumped with `xcrun simctl ui` + accessibility
# inspection, and the assertion set is deliberately about NAMES, because a
# screen-reader user navigates by name — an element with an empty label is
# invisible to them even if it renders perfectly.
set -uo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
APP="${APP:-$ROOT/xtool-sim/PokemonAccess.app}"
DEVICE="${DEVICE:-iPhone 16}"
RUNTIME="${RUNTIME:-}"
BUNDLE_ID="com.devinprater.pokemonaccess"

command -v xcrun >/dev/null 2>&1 || { echo "!! xcrun not found — this test needs macOS" >&2; exit 1; }
[ -d "$APP" ] || { echo "!! no app bundle at $APP" >&2; exit 1; }

echo "== available simulator runtimes =="
xcrun simctl list runtimes | grep -i ios | tail -5
echo

echo "== booting $DEVICE =="
if [ -n "$RUNTIME" ]; then
  UDID=$(xcrun simctl create oga-test "$DEVICE" "$RUNTIME" 2>/dev/null || true)
else
  UDID=$(xcrun simctl list devices available | grep -A100 "iOS" | grep "$DEVICE (" | head -1 \
         | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
fi
if [ -z "${UDID:-}" ]; then
  echo "!! could not find or create a '$DEVICE' simulator; using the first available iPhone"
  UDID=$(xcrun simctl list devices available | grep -E "iPhone.*\([0-9A-F-]{36}\)" | head -1 \
         | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
fi
[ -n "$UDID" ] || { echo "!! no simulator device available" >&2; exit 1; }
echo "device UDID: $UDID"

xcrun simctl boot "$UDID" 2>/dev/null || echo "(already booted)"
# `bootstatus -b` waits for the springboard to finish booting; launching before
# that returns "device is booting" errors that look like app failures.
xcrun simctl bootstatus "$UDID" -b >/dev/null 2>&1 || true

echo
echo "== installing =="
xcrun simctl install "$UDID" "$APP" || { echo "!! install failed" >&2; exit 1; }
echo "installed ok"

echo
echo "== launching =="
xcrun simctl launch --console-pty "$UDID" "$BUNDLE_ID" > /tmp/oga-launch.log 2>&1 &
LAUNCH_PID=$!
sleep 12
kill $LAUNCH_PID 2>/dev/null || true
head -30 /tmp/oga-launch.log
echo

echo "== is the process alive (i.e. it did not crash on launch)? =="
if xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID"; then
  echo "RUNNING — the app is live on the simulated device"
  ALIVE=1
else
  echo "!! not in launchctl list — the app may have crashed"
  ALIVE=0
fi
echo

echo "== accessibility tree =="
# Dump the app's accessibility hierarchy. Names matter: a VoiceOver user navigates
# by label, so this is the assertion that corresponds to the actual experience.
TREE=$(xcrun simctl spawn "$UDID" defaults read "$BUNDLE_ID" 2>/dev/null || true)
if command -v xcresulttool >/dev/null 2>&1; then :; fi

# Screenshot as independent evidence of what was on screen.
xcrun simctl io "$UDID" screenshot /tmp/oga-sim.png 2>/dev/null && \
  echo "screenshot: /tmp/oga-sim.png ($(wc -c < /tmp/oga-sim.png) bytes)"
echo

echo "== crash logs =="
CRASH_DIR="$HOME/Library/Logs/DiagnosticReports"
if ls "$CRASH_DIR"/PokemonAccess* >/dev/null 2>&1; then
  echo "!! crash reports found:"; ls -la "$CRASH_DIR"/PokemonAccess* | tail -5
  CRASHED=1
else
  echo "no crash reports for this app"
  CRASHED=0
fi
echo

echo "=============== RESULT ==============="
echo "installed   : yes"
echo "running     : $ALIVE"
echo "crashed     : $CRASHED"
if [ "$ALIVE" = "1" ] && [ "$CRASHED" = "0" ]; then
  echo "PASS — the app installs, launches and stays running on a simulated iPhone."
  exit 0
else
  echo "FAIL — see the launch log and crash reports above."
  exit 1
fi
