#!/usr/bin/env bash
# android-gb-bridge-test.sh — prove the JS <-> Kotlin bridge contract holds, and
# that the check can actually fail.
#
# WHY THIS EXISTS. There is no Kotlin compiler on the host used for the host
# tests, so Gradle in CI is the only thing that type-checks the Android side. But
# the failure mode that has actually bitten this feature is a NAME, not a type:
# Kotlin posting to a JS function the page never defines (silence — which reads
# as a crash to someone who cannot see the screen), or the page calling a
# @JavascriptInterface method that was never exported. Neither is a compile error
# in either language.
#
# Pass 1: the real tree must pass.
# Pass 2: each sabotaged COPY (in a temp dir; the real tree is never touched)
#         must FAIL. A check that cannot fail proves nothing.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

echo "== real tree"
python3 scripts/android-gb-bridge-test.py "$ROOT" || exit 1

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

stage() {
  rm -rf "$TMP/t"
  mkdir -p "$TMP/t/app/src/main/java/com/devin/opengameaccess" \
           "$TMP/t/app/src/main/assets" \
           "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility" \
           "$TMP/t/app/native-overlay/app/src/main/cpp" \
           "$TMP/t/scripts"
  cp app/src/main/java/com/devin/opengameaccess/GbBridge.kt \
     "$TMP/t/app/src/main/java/com/devin/opengameaccess/"
  cp app/src/main/assets/index.html "$TMP/t/app/src/main/assets/"
  cp app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/GbAccessibilityScript.kt \
     "$TMP/t/app/native-overlay/app/src/main/java/me/magnum/melonds/accessibility/"
  cp app/native-overlay/app/src/main/cpp/MGBAScriptJNI.cpp \
     "$TMP/t/app/native-overlay/app/src/main/cpp/"
  cp scripts/android-gb-bridge-test.py scripts/android-gb-sabotage.py "$TMP/t/scripts/"
}

expect_fail() {
  local what="$1"
  stage
  if ! python3 "$TMP/t/scripts/android-gb-sabotage.py" "$TMP/t" "$what" >/dev/null; then
    printf '  !! %-20s the sabotage could not be applied\n' "$what"
    return 1
  fi
  if python3 "$TMP/t/scripts/android-gb-bridge-test.py" "$TMP/t" >/dev/null 2>&1; then
    printf '  !! %-20s test PASSED with the defect present (not covered)\n' "$what"
    return 1
  fi
  printf '  ok %-20s test fails as required\n' "$what"
  return 0
}

echo
echo "== sabotage: each must FAIL the check"
bad=0
for s in phantom_callback unexported_method wrong_key_mask missing_jni_export stale_claim; do
  expect_fail "$s" || bad=1
done

echo
if [ "$bad" -ne 0 ]; then
  echo "FAIL: the bridge check does not guard everything it claims." >&2
  exit 1
fi
echo "PASS: the Android Game Boy bridge contract holds, and every rule is proved by mutation."
