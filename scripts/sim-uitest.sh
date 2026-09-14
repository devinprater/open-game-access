#!/usr/bin/env bash
# sim-uitest.sh — boot a real iOS Simulator, install the app, launch it, and verify
# it stays running. macOS only: a simulator runtime is CoreSimulator + launchd_sim +
# an iOS runtime sysroot, all of which ship with Xcode. There is no iOS Simulator
# for Windows or Linux.
#
# ⛔ WHAT THIS PROVES, AND WHAT IT DOES NOT.
#   Proves: the bundle installs on a simulated iPhone; it launches; it does not
#           crash; it stays alive and keeps its process; something is drawn to the
#           screen (screenshot captured for inspection).
#   Does NOT prove: that the readers narrate correctly, or that speech reaches a
#           human ear. Those need a physical device AND a game the user supplies —
#           this repo never ships or downloads one.
#
# A "the app launched and survived" test is the right level here. A UI test that
# asserted on control labels would be testing the setup screen, because with no game
# loaded the app shows SetupPanel by design, not the Controls a player would hear.
set -uo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
APP="${APP:-$ROOT/xtool-sim/OpenGameAccess.app}"
DEVICE="${DEVICE:-iPhone 16}"
BUNDLE_ID="com.devinprater.opengameaccess"
EVID="${EVID:-/tmp}"

command -v xcrun >/dev/null 2>&1 || { echo "!! xcrun not found — this test needs macOS" >&2; exit 1; }
[ -d "$APP" ] || { echo "!! no app bundle at $APP" >&2; exit 1; }

echo "== available iOS runtimes =="
xcrun simctl list runtimes 2>/dev/null | grep -i ios | tail -5
echo

echo "== picking a simulator: $DEVICE =="
UDID=$(xcrun simctl list devices available 2>/dev/null \
       | grep -E "^\s+$DEVICE \([0-9A-F-]{36}\)" | head -1 \
       | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
if [ -z "$UDID" ]; then
  echo "  '$DEVICE' not present; taking the first available iPhone"
  UDID=$(xcrun simctl list devices available 2>/dev/null \
         | grep -E "^\s+iPhone .*\([0-9A-F-]{36}\)" | head -1 \
         | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
fi
[ -n "$UDID" ] || { echo "!! no simulator device available" >&2; exit 1; }
echo "  UDID: $UDID"
xcrun simctl list devices | grep "$UDID" | head -1
echo

echo "== booting =="
xcrun simctl boot "$UDID" 2>&1 | head -2 || true
# bootstatus -b blocks until springboard is up. Installing/launching before that
# produces "device is booting" errors that read like app failures.
xcrun simctl bootstatus "$UDID" -b >/dev/null 2>&1 || true
echo "  state: $(xcrun simctl list devices | grep "$UDID" | grep -oE '\((Booted|Shutdown)\)' | head -1)"
echo

echo "== installing =="
xcrun simctl install "$UDID" "$APP" 2>&1 | head -5
if ! xcrun simctl listapps "$UDID" 2>/dev/null | grep -q "$BUNDLE_ID"; then
  echo "!! the bundle does not appear in the installed-app list" >&2
  exit 1
fi
echo "  installed and registered: $BUNDLE_ID"

# Warm the runtime the first time, then do the launch we actually assert on.
echo
echo "== launching =="
xcrun simctl launch "$UDID" "$BUNDLE_ID" >/tmp/oga-launch.txt 2>&1 || true
cat /tmp/oga-launch.txt
sleep 10

# Liveness. simctl launch prints "<bundle id>: <pid>"; use that pid directly rather
# than trusting a grep over launchctl output, which varies between iOS versions.
PID=$(sed -nE 's/.*: ([0-9]+)$/\1/p' /tmp/oga-launch.txt | head -1)
ALIVE=0
if [ -n "$PID" ]; then
  if xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID"; then
    ALIVE=1
  elif xcrun simctl spawn "$UDID" ps -A 2>/dev/null | grep -q " $PID "; then
    ALIVE=1
  fi
fi
echo "  pid: ${PID:-<none>}   alive: $ALIVE"

echo
echo "== second launch (a first-run crash would show here rather than being masked) =="
xcrun simctl terminate "$UDID" "$BUNDLE_ID" >/dev/null 2>&1 || true
sleep 2
xcrun simctl launch "$UDID" "$BUNDLE_ID" >/tmp/oga-launch2.txt 2>&1 || true
cat /tmp/oga-launch2.txt
sleep 6
if xcrun simctl spawn "$UDID" launchctl list 2>/dev/null | grep -q "$BUNDLE_ID"; then
  ALIVE2=1
else
  ALIVE2=0
fi
echo "  alive after relaunch: $ALIVE2"

echo
echo "== screenshot =="
xcrun simctl io "$UDID" screenshot "$EVID/oga-sim.png" 2>&1 | head -2
if [ -f "$EVID/oga-sim.png" ]; then
  echo "  $EVID/oga-sim.png ($(wc -c < "$EVID/oga-sim.png") bytes)"
fi

echo
echo "== crash reports =="
CRASH_DIR="$HOME/Library/Logs/DiagnosticReports"
CRASHED=0
# shellcheck disable=SC2086
if ls "$CRASH_DIR"/OpenGameAccess* >/dev/null 2>&1; then
  echo "!! crash reports present:"
  ls -la "$CRASH_DIR"/OpenGameAccess* | tail -5
  CRASHED=1
else
  echo "  none on the host"
fi

# ⛔ Match on the APP, not on "is this directory non-empty". Every simulator has
# Library/Logs/CrashReporter/{Assistant,DiagnosticLogs} from the moment it boots, so
# a bare `ls -A` test reported a crash on a run where the app launched cleanly and
# stayed alive — a false negative on the whole job. Only files naming our app count.
SIMCRASH="$HOME/Library/Developer/CoreSimulator/Devices/$UDID/data/Library/Logs/CrashReporter"
if [ -d "$SIMCRASH" ]; then
  HITS="$(find "$SIMCRASH" -type f \( -iname 'OpenGameAccess*' -o -iname '*pokemonaccess*' \) 2>/dev/null)"
  if [ -n "$HITS" ]; then
    echo "!! simulator crash logs naming the app:"
    echo "$HITS" | sed 's/^/     /'
    CRASHED=1
  else
    echo "  none in the simulator's CrashReporter (its standard"
    echo "  Assistant/DiagnosticLogs folders are not crash reports)"
  fi
fi

echo
echo "=============== RESULT ==============="
echo "installed : yes"
echo "launched  : ${PID:+yes}"
echo "running   : $ALIVE"
echo "relaunch  : $ALIVE2"
echo "crashed   : $CRASHED"
if [ "${ALIVE:-0}" = "1" ] && [ "${ALIVE2:-0}" = "1" ] && [ "$CRASHED" = "0" ]; then
  echo "PASS — installs, launches, survives a relaunch, no crash on a simulated iPhone."
  exit 0
fi
echo "FAIL — see the launch output and crash reports above."
exit 1
