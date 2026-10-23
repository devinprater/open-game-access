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
           "$TMP/t/Sources/OpenGameAccess" \
           "$TMP/t/Core"
  cp Core/adapters.cpp "$TMP/t/Core/"
  cp Core/psp_core.h "$TMP/t/Core/"
  cp Core/adapter.h "$TMP/t/Core/"
  cp Core/dissidia_adapter.cpp Core/dbzar_adapter.cpp Core/mku_adapter.cpp \
     Core/khbbs_adapter.cpp "$TMP/t/Core/"
  cp Sources/OpenGameAccess/AdapterCommand.swift "$TMP/t/Sources/OpenGameAccess/"
  cp app/src/main/java/com/devin/opengameaccess/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/"
  cp app/src/main/java/com/devin/opengameaccess/ui/*.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     "$TMP/t/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/AccessibilityScript.kt \
     app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/PspAccessibilityScript.kt \
     "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp \
     app/native-overlay/app/src/main/cpp/PspSessionJNI.cpp \
     app/native-overlay/app/src/main/cpp/PspAdapterHost.cpp \
     "$TMP/t/app/native-overlay/app/src/main/cpp/"
  cp scripts/android-apply-overlay.sh "$TMP/t/scripts/"
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
case_pspkeys() {  # sabotage: swap the CROSS and CIRCLE ids (a wrong button, silently)
  sed -i 's/const val CIRCLE = 7/const val CIRCLE = 8/; s/const val CROSS = 8/const val CROSS = 7/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/PspGameSession.kt"
}
case_pspjni() {  # sabotage: rename a PSP JNI symbol so it no longer resolves
  sed -i 's/Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspLoadRom/Java_me_magnum_melonds_accessibility_PspAccessibilityScript_nativePspLoadRomRenamed/' \
     "$TMP/t/app/native-overlay/app/src/main/cpp/PspSessionJNI.cpp"
}
case_pspline() {  # sabotage: the library page claims there is no PSP core again
  printf '\nval stalePspLine = "there is no PSP core in it"\n' \
     >> "$TMP/t/app/src/main/java/com/devin/opengameaccess/ui/AdapterGamesScreen.kt"
}
case_pspbridge() {  # sabotage: the PSP session never builds the speech bridge
  sed -i '/AccessibilityScript.speechBridge(app)/d' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/PspGameSession.kt"
}
case_pspcmd() {  # sabotage: a command value no longer matches adapter.h's enum
  sed -i 's/const val GUIDE = 41/const val GUIDE = 40/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/PspGameSession.kt"
}
case_pspregistry() {  # sabotage: the host's dispatch table loses an adapter
  sed -i 's/kKingdomHeartsBirthBySleep/kKingdomHeartsBirthBySleepDropped/' \
     "$TMP/t/app/native-overlay/app/src/main/cpp/PspAdapterHost.cpp"
}
case_pspsupported() {  # sabotage: a generated command set drops a real button
  sed -i 's/commands = listOf(0, 7, 1, 3, 41, 6)/commands = listOf(0, 7, 1, 3, 6)/' \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/AdapterLibraryData.kt"
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

for s in key announce sideeffect jni webview stale bridgelate library dsguard pspkeys pspjni pspline pspbridge pspcmd pspregistry pspsupported; do
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
