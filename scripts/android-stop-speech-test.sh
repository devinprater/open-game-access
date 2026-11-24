#!/usr/bin/env bash
# =============================================================================
# android-stop-speech-test.sh -- Stop speech never ends a game, on any Android
# session, and the gate that says so can fail.
#
# Runs android-stop-speech-test.py on the tree, then on copies with one known
# bug each; every mutant must turn the gate red. The first mutant IS the bug
# this gate exists for: Game Boy's Stop speech tearing the core down.
# Needs only python3.
# =============================================================================
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1
PY=scripts/android-stop-speech-test.py
KT=app/src/main/java/com/devin/opengameaccess
CPP=app/native-overlay/app/src/main/cpp
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0

echo "== the tree"
python3 "$PY" || fail=1

echo "== the gate can fail (each mutant must be caught)"
mutate() { # mutate <name> <file> <old> <new>
  local name="$1" file="$2" dir="$TMP/$1"
  mkdir -p "$dir/$KT" "$dir/$CPP" "$dir/Core"
  cp "$KT"/*.kt "$dir/$KT/"
  cp "$CPP"/*SessionJNI.cpp "$CPP"/PspAdapterHost.cpp "$dir/$CPP/"
  cp Core/adapter.h "$dir/Core/"
  if ! python3 - "$dir/$file" "$3" "$4" <<'PYM'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if old not in s:
    sys.exit("anchor not found: " + old[:60])
open(p, "w").write(s.replace(old, new, 1))
PYM
  then
    echo "  FAIL mutant '$name' could not be built (anchor moved; update this gate)"; fail=1; return
  fi
  if OGA_STOP_SPEECH_ROOT="$dir" python3 "$PY" >/dev/null 2>&1; then
    echo "  FAIL mutant '$name' SURVIVED"; fail=1
  else
    echo "  ok   mutant '$name' is caught"
  fi
}
# The original bug: Stop speech stops the Game Boy core.
mutate gb-stops-core "$KT/GbGameSession.kt" \
  'GbAccessibilityScript.stopSpeech()' 'GbAccessibilityScript.stop()'
# Stop speech calls the session's own stop().
mutate genesis-calls-stop "$KT/GenesisGameSession.kt" \
  'AccessibilityScript.speechBridge(context.applicationContext).stop()' \
  'AccessibilityScript.speechBridge(context.applicationContext).stop(); stop()'
# Only the voice stops; the core's queue speaks again.
mutate ps1-voice-only "$KT/Ps1GameSession.kt" \
  'Ps1AccessibilityScript.adapterCommand(Ps1AdapterCommands.STOP_SPEECH)' 'Unit'
# The command mirror drifts from adapter.h.
mutate mesen-wrong-value "$KT/MesenGameSession.kt" \
  'const val STOP_SPEECH = 37' 'const val STOP_SPEECH = 36'
# The native command tears the core down.
mutate native-destroys "$CPP/DsSessionJNI.cpp" \
  'return (core && poke_command(core, (int)cmd))' 'poke_destroy(core); return (core && poke_command(core, (int)cmd))'
# The chord forgets a console, so its game falls through to another's stop.
mutate chord-misses-ps1 "$KT/ControllerInput.kt" \
  'Ps1GameSession.stopSpeech(context)' 'Unit'
# The chord ends the game instead.
mutate chord-stops-psp "$KT/ControllerInput.kt" \
  'PspGameSession.stopSpeech(context)' 'PspGameSession.stop()'

if [ "$fail" -ne 0 ]; then echo "android-stop-speech-test: FAILED"; exit 1; fi
echo "android-stop-speech-test: all checks passed"
