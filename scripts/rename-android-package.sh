#!/usr/bin/env bash
# rename-android-package.sh — rename the Android app's Kotlin package in the temp
# Android working tree, so the staged repo stays consistent.
#
# ⛔ WHY AT THE SOURCE. scripts/stage-repo.sh copies app/src out of the Android
# working tree. Renaming inside the staged clone only is undone by the next staging
# run — which is exactly what happened: the package rename reverted on the following
# stage. The temp tree is the source of truth for those files, so the rename belongs
# there.
#
# The JNI entry points MUST change with the package: JNI symbol names are derived
# from the package and class (Java_<pkg>_<Class>_<method>), so renaming the Kotlin
# package without the C++ bridge leaves every native method unresolved at runtime,
# with no compile-time error to warn you.
set -uo pipefail

AND="/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app"
OLD="$AND/app/src/main/java/com/devin/pokemonaccess"
NEW="$AND/app/src/main/java/com/devin/opengameaccess"

[ -d "$AND" ] || { echo "!! no Android tree at $AND" >&2; exit 1; }

echo "== before =="
ls "$AND/app/src/main/java/com/devin/" 2>/dev/null || echo "  (no com/devin yet)"

if [ -d "$OLD" ]; then
  mkdir -p "$NEW"
  for f in "$OLD"/*.kt; do
    [ -f "$f" ] || continue
    mv "$f" "$NEW/"
    echo "  moved $(basename "$f")"
  done
  rmdir "$OLD" 2>/dev/null || true
fi

if [ -d "$NEW" ]; then
  echo
  echo "== rewriting package declarations =="
  for f in "$NEW"/*.kt; do
    [ -f "$f" ] || continue
    sed -i 's/package com\.devin\.pokemonaccess/package com.devin.opengameaccess/' "$f"
    echo "  $(basename "$f"): $(head -1 "$f")"
  done
fi

BRIDGE="$AND/app/src/main/cpp/melonds_bridge.cpp"
if [ -f "$BRIDGE" ]; then
  echo
  echo "== rewriting JNI symbols =="
  sed -i 's/Java_com_devin_pokemonaccess_MelonCore_/Java_com_devin_opengameaccess_MelonCore_/g' "$BRIDGE"
  sed -i 's/com\.devin\.pokemonaccess\.MelonCore/com.devin.opengameaccess.MelonCore/g' "$BRIDGE"
  echo "  Java_com_devin_opengameaccess_* : $(grep -c 'Java_com_devin_opengameaccess' "$BRIDGE")"
  echo "  Java_com_devin_pokemonaccess_*  : $(grep -c 'Java_com_devin_pokemonaccess' "$BRIDGE")  (must be 0)"
fi

# Any other Kotlin/Java under the Android tree naming the old package.
echo
echo "== sweep for leftovers =="
LEFT="$(grep -rln 'devin\.pokemonaccess' "$AND/app/src" "$AND"/*.kts "$AND"/*.md 2>/dev/null | head -10)"
if [ -n "$LEFT" ]; then
  echo "$LEFT" | sed 's/^/  /'
  # Gradle/manifest reference the APPLICATION ID, which is separate from the Kotlin
  # package and may legitimately differ; report rather than silently rewrite.
  echo "  ^ check each: the applicationId may intentionally differ from the package"
else
  echo "  none"
fi

echo
echo "== after =="
ls "$AND/app/src/main/java/com/devin/"
