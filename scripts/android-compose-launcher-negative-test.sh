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
           "$TMP/t/app/native-overlay/app/src/main/cpp" \
           "$TMP/t/scripts"
  cp app/src/main/java/com/devin/opengameaccess/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/"
  cp app/src/main/java/com/devin/opengameaccess/ui/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     "$TMP/t/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp \
     "$TMP/t/app/native-overlay/app/src/main/cpp/"
  cp "$TEST" "$TMP/t/scripts/"
}

case_key() {   # sabotage: swap the UP and DOWN bit values
  sed -i 's/const val UP = 64/const val UP = 128/; s/const val DOWN = 128/const val DOWN = 64/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/GbGameSession.kt"
}
case_announce() {  # sabotage: drop the announcement-view wiring (the bug we fixed)
  sed -i 's/AccessibilityScript.setAnnouncementView(view)/\/\/ removed/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
}
case_sideeffect() { # sabotage: wire during composition instead of after attach
  sed -i 's/^        SideEffect {/        run {/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/MainActivity.kt"
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

for s in key announce sideeffect jni webview stale; do
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
