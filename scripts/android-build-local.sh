#!/usr/bin/env bash
# android-build-local.sh — build the OGA Android APK LOCALLY, no CI round trip.
#
# ⛔ WHY. The APK was built on GitHub runners, so every check cost a push and ~20
# minutes. This session produced three CI failures in a row, each avoidable in under
# a minute here: a wrong Compose dependency, a hardcoded dev path, and a JNI call
# before the native library existed. Build locally first; CI then only confirms.
#
# ⛔ IT MIRRORS .github/workflows/android-apk.yml STEP FOR STEP. A local build that
# takes a different path proves nothing about the CI one, so the step order and the
# environment variables are copied deliberately, including MGBA_SRC/LUA_SRC.
#
# Usage: bash scripts/android-build-local.sh [--no-frontend-refresh]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# ⛔ JDK 21, NOT 17: the app module compiles for Java 21 and JDK 17 dies with
# "error: invalid source release: 21". CI sets up JDK 21 for the same reason.
if [ -z "${JAVA_HOME:-}" ]; then
  for cand in /usr/lib/jvm/java-21-openjdk-amd64 /usr/lib/jvm/java-17-openjdk-amd64; do
    [ -d "$cand" ] && export JAVA_HOME="$cand" && break
  done
fi
case "$JAVA_HOME" in
  *java-17*) echo "!! JAVA_HOME is JDK 17; the app needs 21 (invalid source release)" >&2 ;;
esac
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

FRONTEND="${OGA_FRONTEND:-$HOME/frontend}"
MGBA_SRC="${MGBA_SRC:-$HOME/src/mgba}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"

echo "================= OGA Android local build ================="
echo "repo      : $ROOT"
echo "frontend  : $FRONTEND"
echo "JAVA_HOME : $JAVA_HOME"
echo "ANDROID_HOME: $ANDROID_HOME"
echo "MGBA_SRC  : $MGBA_SRC"
echo "LUA_SRC   : $LUA_SRC"
echo

fail() { echo; echo "!! $1" >&2; exit 1; }

# ---- step 0: the cheap gates FIRST, exactly as CI orders them ----------------
# ⛔ CHEAPEST FIRST. A syntax error in a tracked script breaks the workflow minutes
# later with a message pointing at the wrong place.
echo "== step 1: shell scripts must parse"
bash scripts/check-shell-syntax.sh || fail "shell scripts do not parse"

echo "== step 2: the core is configured for the JIT"
bash scripts/android-jit-check.sh "$ROOT" || fail "JIT configuration check failed"

# ---- step 3: the frontend --------------------------------------------------
if [ ! -d "$FRONTEND/app" ]; then
  echo "== step 3: cloning the Android frontend"
  git clone --depth 1 https://github.com/rafaelvcaetano/melonDS-android.git "$FRONTEND" \
    || fail "frontend clone failed"
else
  echo "== step 3: frontend present at $FRONTEND"
fi

# ⛔ INIT THE SUBMODULES EVERY TIME, NOT ONLY AFTER A FRESH CLONE.
# An existing checkout can still have EMPTY submodule dirs (faad2, enet, oboe), and
# the symptom is CMake failing with "does not contain a CMakeLists.txt" for a
# directory that exists. CI inits unconditionally, so a local build that skips it is
# a different program. Measured: the first local run died on exactly this.
echo "== step 3b: initializing submodules"
git -C "$FRONTEND" submodule update --init --recursive --depth 1 2>&1 | tail -4 || true
for d in app/src/main/cpp/faad2 app/src/main/cpp/enet app/src/main/cpp/oboe; do
  if [ ! -f "$FRONTEND/$d/CMakeLists.txt" ]; then
    fail "submodule $d is still empty (no CMakeLists.txt); CMake will fail on it"
  fi
  echo "   ok: $d"
done
# melonDS-android-lib may legitimately refuse to check out: step 4 replaces it with
# the Lua core, so a re-run sees our own edits there. Not a failure.
if [ ! -d "$FRONTEND/melonDS-android-lib/src" ]; then
  echo "   note: melonDS-android-lib not checked out; step 4 supplies it"
fi

# ---- step 4: the Lua-enabled core -----------------------------------------
echo "== step 4: swapping in the Lua-enabled core"
bash scripts/bootstrap-deps.sh "$HOME/src" || fail "bootstrap-deps failed"
MELONDS_LUA="$HOME/src/melonds-lua"
[ -d "$MELONDS_LUA" ] || fail "no melonds-lua at $MELONDS_LUA"
rm -rf "$FRONTEND/melonDS-android-lib"
cp -r "$MELONDS_LUA" "$FRONTEND/melonDS-android-lib" || fail "core swap failed"

# ---- step 5: the overlay ---------------------------------------------------
echo "== step 5: applying the Open Game Access overlay"
MGBA_SRC="$MGBA_SRC" LUA_SRC="$LUA_SRC" \
  bash scripts/android-apply-overlay.sh "$FRONTEND" "$ROOT" || fail "overlay failed"

# ---- step 6: retarget the shell to our core's API --------------------------
echo "== step 6: retargeting the shell to the core's API"
bash scripts/android-port-core-api.sh "$FRONTEND" || fail "core API retarget failed"

# ---- step 7: the build -----------------------------------------------------
echo "== step 7: gradle assembleGitHubProdDebug"
cd "$FRONTEND" || fail "no frontend dir"
echo "sdk.dir=$ANDROID_HOME" > local.properties
chmod +x ./gradlew
./gradlew :app:assembleGitHubProdDebug --no-daemon --console=plain 2>&1 | tail -30
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] || fail "gradle build failed (rc=$rc)"

APK=$(find "$FRONTEND/app/build/outputs/apk" -name '*.apk' | head -1)
[ -n "$APK" ] || fail "no APK produced"
echo
echo "== APK: $APK ($(stat -c%s "$APK") bytes)"

# ---- step 8: the same verifications CI does --------------------------------
echo "== step 8: verifying the APK"
unzip -l "$APK" > /tmp/apk-list.txt 2>/dev/null || true
# ⛔ `grep -qi lua` WAS NOT A GATE. It matches main.lua (the DS reader), any
# vintage of the reader tree, and the core's own Lua engine -- so it passed while
# the APK was shipping a reader set with no bootstrap and no mGBA shim. Assert
# the files that exist ONLY when the feature works. Keep this list identical to
# .github/workflows/android-apk.yml; both are checked against the same set.
missing=0
for f in oga_bootstrap.lua mgba_compat.lua gb.lua gba.lua pokemon.lua; do
  if grep -q "assets/lua/gb/$f" /tmp/apk-list.txt; then
    echo "   ok: assets/lua/gb/$f"
  else
    echo "   !! MISSING from the APK: assets/lua/gb/$f" >&2
    missing=1
  fi
done
[ "$missing" -eq 0 ] || fail "the APK's Game Boy / GBA reader set is incomplete"
echo "   ok: the Game Boy / GBA reader set is present and current"

cd "$ROOT" || exit 1
bash scripts/check-no-roms.sh . --skip-build-output || fail "ROM guard failed"
! find "$FRONTEND/app/build" -type f \
    \( -iname '*.nds' -o -iname '*.gba' -o -iname '*.gb' -o -iname '*.gbc' \
    -o -iname '*.sav' -o -iname '*.srm' -o -iname 'bios*.bin' \
    -o -iname 'firmware*.bin' \) | grep -q . || fail "game data in the build tree"
echo "   ok: no game data in the build tree"

echo
echo "================= DONE ================="
echo "$APK"
