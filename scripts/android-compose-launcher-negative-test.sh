#!/usr/bin/env bash
# NEGATIVE TEST: every rule must FAIL when its defect is present.
# A check that cannot fail proves nothing.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
TEST=scripts/android-compose-launcher-test.py
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
bad=0

stage() {
  rm -rf "$TMP/t"
  mkdir -p "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui" \
           "$TMP/t/app/src/main/java/me/magnum/melonds/accessibility" \
           "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility" \
           "$TMP/t/app/native-overlay/app/src/main/cpp" \
           "$TMP/t/scripts" \
           "$TMP/t/Core"
  cp Core/adapters.cpp "$TMP/t/Core/"
  cp app/src/main/java/com/devin/opengameaccess/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/"
  cp app/src/main/java/com/devin/opengameaccess/ui/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     "$TMP/t/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/AccessibilityScript.kt \
     "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp \
     "$TMP/t/app/native-overlay/app/src/main/cpp/"
  cp "$TEST" "$TMP/t/scripts/"
}

case_key() {   # sabotage: swap the UP and DOWN bit values
  sed -i 's/const val UP = 64/const val UP = 128/; s/const val DOWN = 128/const val DOWN = 64/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/GbGameSession.kt"
}
case_announce() {  # sabotage: drop the announcement-view wiring (the bug we fixed)
  # The forwarding calls live in the session now, not the Activity (the chain
  # moved when the launch crash was fixed); remove both so the chain is broken.
  sed -i 's/AccessibilityScript\.setAnnouncementView(/\/\/ removed(/g' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/GbGameSession.kt"
}
case_sideeffect() { # sabotage: wire during composition instead of after attach
  sed -i 's/^        SideEffect {/        run {/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
}
case_bridgelate() {  # sabotage: build the speech bridge before a game (the launch crash)
  python3 - "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s = s.replace("        setContent {",
              "        me.magnum.melonds.accessibility.AccessibilityScript.initialize(applicationContext)\n        setContent {", 1)
open(p, "w", encoding="utf-8").write(s)
PYEOF
}
case_jni() {  # sabotage: rename a JNI symbol so it no longer resolves
  sed -i 's/Java_me_magnum_melonds_accessibility_GbAccessibilityScript_loadGbRom/Java_me_magnum_melonds_accessibility_GbAccessibilityScript_loadGbRomRenamed/' \
     "$TMP/t/app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp"
}
case_webview() {  # sabotage: a second WebView launcher returns
  printf '\n// loadUrl("https://appassets.androidplatform.net/assets/index.html")\n' \
     >> "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
  sed -i 's|// loadUrl|loadUrl|' "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
}
case_stale() {  # sabotage: the stale capability claim comes back into CODE
  printf '\nval msg = "not compiled into this test build"\n' \
     >> "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui/LauncherScreen.kt"
}
case_library() {  # sabotage: a registry game vanishes from the library data
  sed -i '/id = "alttp"/,+6d' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/AdapterLibraryData.kt"
}
case_dsguard() {  # sabotage: the DS-native guards come out of AccessibilityScript
  sed -i '/catch (e: UnsatisfiedLinkError)/d' \
     "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/AccessibilityScript.kt"
}

# CONTROL: the staged tree, unmutated, must PASS. Without this, a staging gap
# makes every case below "fail as required" vacuously and the suite proves
# nothing (this bit us: GbAccessibilityScript.kt was staged at a path the test
# never reads, so the sanity check failed before any rule ran).
stage
if python3 "$TMP/t/scripts/android-compose-launcher-test.py" "$TMP/t" >/dev/null 2>&1; then
  printf '  ok %-12s staged tree passes unmutated\n' "control"
else
  printf '  !! %-12s staged tree FAILS unmutated; every case below is vacuous\n' "control"
  bad=1
fi

for s in key announce sideeffect jni webview stale bridgelate library dsguard; do
  stage
  "case_$s"
  if python3 "$TMP/t/scripts/android-compose-launcher-test.py" "$TMP/t" >/dev/null 2>&1; then
    printf '  !! %-12s test PASSED with the defect present (not covered)\n' "$s"
    bad=1
  else
    printf '  ok %-12s test fails as required\n' "$s"
  fi
done

echo
if [ "$bad" -ne 0 ]; then
  echo "FAIL: the contract test does not guard everything it claims." >&2
  exit 1
fi
echo "PASS: every rule is proved by mutation."
