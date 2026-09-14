#!/usr/bin/env bash
# capture-android-patches.sh — export the Android app's local modifications as a
# patch set, so the APK is reproducible from public upstream sources.
#
# ⛔ WHY. The working Android app is a fork-of-a-fork: rafaelvcaetano/melonDS-android
# plus its core lib, with ~45 local modifications across Java, Kotlin, C++ and
# CMake. Those live in a temp directory. Without capturing them the working APK
# cannot be rebuilt from the repo — the repo would document the idea and not the
# implementation.
#
# Output goes to app/patches/ (committed); the fetched upstreams stay out (GPL, and
# a moving target — see scripts/bootstrap-deps.sh).
set -uo pipefail
SRC="${1:-/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android}"
DEST="${2:-/mnt/c/Users/Devin Prater/pokemon-access-ios/app/patches}"

[ -d "$SRC" ] || { echo "!! no Android working tree at $SRC" >&2; exit 1; }
mkdir -p "$DEST"

echo "== core lib patches (melonDS-android-lib) =="
cd "$SRC/melonDS-android-lib" || exit 1
git add -A -N 2>/dev/null || true
git diff --binary HEAD > "$DEST/core-lib.patch" 2>/dev/null || true
echo "   $(wc -l < "$DEST/core-lib.patch") lines"
echo "   base: $(git rev-parse HEAD)"
git rev-parse HEAD > "$DEST/core-lib.base" 2>/dev/null || true
git remote -v | head -1 > "$DEST/core-lib.remote" 2>/dev/null || true

echo
echo "== app patches =="
cd "$SRC" || exit 1
git add -A -N 2>/dev/null || true
git diff --binary HEAD > "$DEST/app.patch" 2>/dev/null || true
echo "   $(wc -l < "$DEST/app.patch") lines"
echo "   base: $(git rev-parse HEAD)"
git rev-parse HEAD > "$DEST/app.base" 2>/dev/null || true
git remote -v | head -1 > "$DEST/app.remote" 2>/dev/null || true

echo
echo "== new (untracked) files, which a diff against HEAD omits =="
NEWLIST="$DEST/newfiles.txt"
: > "$NEWLIST"
for f in $(git ls-files --others --exclude-standard 2>/dev/null); do
  echo "$f" >> "$NEWLIST"
done
for f in $(cd "$SRC/melonDS-android-lib" && git ls-files --others --exclude-standard 2>/dev/null); do
  echo "melonDS-android-lib/$f" >> "$NEWLIST"
done
echo "   $(wc -l < "$NEWLIST") untracked paths listed in newfiles.txt"

echo
echo "== patches written to $DEST =="
ls -la "$DEST"

echo
echo "== sanity: does the patch apply to a clean clone? (dry run) =="
TMP="$(mktemp -d)"
if git clone -q "https://github.com/rafaelvcaetano/melonDS-android-lib.git" "$TMP/core" 2>/dev/null; then
  cd "$TMP/core" && git checkout -q "$(cat "$DEST/core-lib.base")" 2>/dev/null || true
  if git apply --check "$DEST/core-lib.patch" 2>/tmp/applyerr; then
    echo "   core-lib.patch applies cleanly"
  else
    echo "   !! core-lib.patch does NOT apply cleanly:"; head -5 /tmp/applyerr
  fi
else
  echo "   (could not clone upstream for the dry run — offline?)"
fi
rm -rf "$TMP"
