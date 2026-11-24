#!/usr/bin/env bash
# =============================================================================
# android-gb-audio-test.sh -- Game Boy games make their own sound on Android.
#
# They were SILENT: MGBARunner ran mGBA, mGBA filled its audio ring every frame,
# and nothing read it. The fix has three parts and this gate covers each:
#
#   1. BEHAVIOUR (scripts/android-gb-audio-test.cpp). Compiles the exact header the
#      APK links, GbAudioQueue.h, and measures real streams: 131072 Hz in, 32768 Hz
#      out, no click at frame edges, harmonics not folded back, latency bounded,
#      the Game sound switch honoured.
#   2. THE GATE CAN FAIL. Four mutations of the header, each a bug this path could
#      plausibly ship with; every one must turn the behaviour test red.
#   3. THE WIRING. A queue nobody fills is the original bug again. The frame
#      thread must drain after it runs a frame, the session must start a drain
#      and stop it before the core is torn down, and the clock must be paced to
#      the console. (JNI symbol parity is android-compose-launcher-test.py's job.)
#
# Needs only g++. No ROM, no NDK, no phone -- so it always runs, never SKIPs.
# =============================================================================
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1

HDR=app/native-overlay/app/src/main/cpp/GbAudioQueue.h
TEST=scripts/android-gb-audio-test.cpp
JNI=app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp
RUNNER=app/native-overlay/app/src/main/cpp/MGBARunner.cpp
SESSION=app/src/main/java/com/devin/opengameaccess/GbGameSession.kt
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0
ok()  { printf '  ok   %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*"; fail=1; }

command -v g++ >/dev/null || { echo "FAIL: g++ is required for this gate"; exit 1; }

build() { # build <header-dir> <out>
  g++ -std=c++17 -O1 -Wall -Wextra -I "$1" "$TEST" -o "$2" 2>"$2.log"
}

echo "== 1. behaviour"
mkdir -p "$TMP/real"
cp "$HDR" "$TMP/real/"
if build "$TMP/real" "$TMP/real/t" && "$TMP/real/t"; then
  ok "the Android Game Boy audio path behaves"
else
  cat "$TMP/real/t.log" 2>/dev/null
  bad "the Android Game Boy audio path behaves"
fi

echo "== 2. the gate can fail (each mutant must be caught)"
mutate() { # mutate <name> <python replace expression on s>
  local name="$1" expr="$2" dir="$TMP/m-$1"
  mkdir -p "$dir"
  python3 - "$HDR" "$dir/GbAudioQueue.h" "$expr" <<'PY'
import sys
src, dst, expr = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(src).read()
old, new = expr.split("\n=>\n")
if old not in s:
    sys.exit("mutation anchor not found: " + old[:60])
open(dst, "w").write(s.replace(old, new, 1))
PY
  if [ $? -ne 0 ]; then bad "mutant '$name' could not be built (anchor moved; update this gate)"; return; fi
  if build "$dir" "$dir/t" && "$dir/t" >/dev/null 2>&1; then
    bad "mutant '$name' SURVIVED -- the behaviour test does not guard it"
  else
    ok "mutant '$name' is caught"
  fi
}
# Phase restarts every call: the 60 Hz click.
mutate phase-restart $'        const size_t before = out.size();\n=>\n        reset();\n        const size_t before = out.size();'
# Pick a sample instead of averaging the window: aliasing, and a quartered level.
mutate pick-not-average $'                    accL += l; accR += r; accN += 1.0;\n=>\n                    accL = l * step; accR = r * step; accN += 1.0;'
# No backlog cap: latency grows without bound.
mutate no-backlog-cap $'        if (have > kMaxBacklogFrames)\n=>\n        if (false)'
# The Game sound switch ignored by push.
mutate switch-ignored $'        if (!enabled) return;\n=>\n'

echo "== 3. the wiring"
python3 - "$JNI" "$RUNNER" "$SESSION" <<'PY' || fail=1
import re, sys
jni, runner, session = (open(p).read() for p in sys.argv[1:4])
bad = 0
def ok(c, what):
    global bad
    print(("  ok   " if c else "  FAIL ") + what)
    if not c: bad = 1

def body(src, head):
    i = src.find(head)
    if i < 0: return ""
    j = src.find("{", i); depth = 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0: return src[j:k + 1]
    return ""

frame = body(jni, "GbAccessibilityScript_runGbFrame(")
ok("runFrame()" in frame and "drainAudio(gAudio)" in frame
   and frame.find("runFrame()") < frame.find("drainAudio(gAudio)"),
   "the frame thread drains mGBA's ring right after it runs the frame")
read = body(jni, "GbAccessibilityScript_readGbAudio(")
ok("gAudio.read(" in read and "gRunner" not in read,
   "the audio thread's read touches the queue only, never the core")
ok("mAudioBufferRead" in body(runner, "size_t MGBARunner::drainAudio("),
   "MGBARunner::drainAudio really reads mGBA's audio ring")
ok("core->opts.volume = 0x100" in runner,
   "the master volume is set, not left to whatever the struct held")

start = body(session, "fun start(activity: Activity, uri: Uri)")
ok("startAudio()" in start, "starting a game starts the audio drain")
stop = body(session, "fun stop()")
ok("stopAudio()" in stop and "GbAccessibilityScript.stop()" in stop
   and stop.find("stopAudio()") < stop.find("GbAccessibilityScript.stop()"),
   "stopping a game joins the drain BEFORE the core is torn down")
loop = body(session, "private fun audioLoop(")
ok("GbAccessibilityScript.readAudio(" in loop and "track.write(" in loop,
   "the drain moves the game's audio into the AudioTrack")
ok("applyGameSoundSwitch()" in body(session, "fun runFrame()"),
   "the Game sound switch is applied every frame")
clock = body(session, "fun rememberFrameClock(")
ok("nextFrameAt" in clock and "GB_FRAMES_PER_SECOND" in clock,
   "the frame clock is paced to the console (59.7275 Hz), not the screen")
sys.exit(bad)
PY

if [ "$fail" -ne 0 ]; then echo "android-gb-audio-test: FAILED"; exit 1; fi
echo "android-gb-audio-test: all checks passed"
