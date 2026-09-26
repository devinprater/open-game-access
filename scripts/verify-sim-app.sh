#!/usr/bin/env bash
# verify-sim-app.sh — prove the packaged .app is really a linked, simulator-targeted
# app with the core and the Lua script inside it.
#
# ⛔ This exists because two earlier "0 symbols" readings were REPORTING bugs, not
# link bugs: GNU nm cannot read Mach-O, and llvm-nm -g on this binary prints almost
# nothing either. A verification step that can report 0 for a healthy artifact is
# worse than none, so the checks here are written to distinguish "absent" from
# "my tool cannot see it".
set -uo pipefail
# ⛔ Derive the repo root from this script's location. A hard-coded
# $HOME/open-game-access default worked here but not in CI, where the repo is
# checked out as open-game-access under the runner workspace — the verify step then
# reported "no binary at /Users/runner/open-game-access/..." for an app that had
# just been built successfully two steps earlier.
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
APP="${1:-$ROOT/xtool-sim/OpenGameAccess.app}"
BIN="$APP/OpenGameAccess"
# Portable platform detection, shared with package-sim-app.sh.
. "$ROOT/scripts/lib-macho.sh"

[ -f "$BIN" ] || { echo "!! no binary at $BIN" >&2; exit 1; }

NM=/usr/local/swift/bin/llvm-nm
[ -x "$NM" ] || NM="$(command -v llvm-nm || command -v nm)"

echo "binary      : $BIN"
echo "size        : $(stat -c%s "$BIN" 2>/dev/null || stat -f%z "$BIN") bytes"

echo
echo "== platform (must be iossimulator, not ios) =="
PLAT="$(macho_platform "$BIN")"
echo "  $PLAT"
case "$PLAT" in
  iossimulator) echo "  correct" ;;
  *)            echo "  detection tools:$(macho_platform_tools)" ;;
esac

echo
echo "== symbol counts =="
TOTAL="$("$NM" "$BIN" 2>/dev/null | wc -l)"
echo "total symbols          : $TOTAL"
echo "melonDS symbols        : $("$NM" "$BIN" 2>/dev/null | grep -ci melonds)"
echo "poke_ symbols          : $("$NM" "$BIN" 2>/dev/null | grep -c 'poke_')"
echo
echo "sample poke_ entries:"
"$NM" "$BIN" 2>/dev/null | grep 'poke_' | head -6 | sed 's/^/  /'

echo
echo "== the Lua script inside the bundle =="
RES="$APP/OpenGameAccess_OpenGameAccess.bundle/Resources"
if [ -d "$RES" ]; then
  ls -la "$RES"
  echo "--- hashes (must match the originals) ---"
  ( cd "$RES" && (sha256sum ./*.lua 2>/dev/null || shasum -a 256 ./*.lua) )
else
  echo "!! no resource bundle — the app would install and find no script" >&2
  exit 1
fi

echo
echo "== PPSSPP runtime assets inside the bundle =="
ASSETS_OK=0
if [ -f "$APP/ppsspp-assets/compat.ini" ] && [ -f "$APP/ppsspp-assets/ppge_atlas.zim" ] && [ -f "$APP/ppsspp-assets/vfpu/vfpu_sin_lut8192.dat" ]; then
  echo "  ppsspp-assets present"
  ASSETS_OK=1
else
  echo "!! ppsspp-assets incomplete" >&2
fi
echo "== Info.plist platform keys =="
grep -A2 -E "CFBundleSupportedPlatforms|DTPlatformName" "$APP/Info.plist" 2>/dev/null

echo
echo
echo "== duplicate global text symbols in the core archive (must be none) =="
# Without -force_load the app link resolves twins first-wins, which is only
# safe while the twins are identical (same LZMA SDK 19.00 in mGBA and PPSSPP).
# A NEW overlap with semantic differences would link silently wrong, so it
# fails here instead: dedupe by hand (one lua, one xxhash) like before.
ARCHIVE="${POKECORE_LIB:-$ROOT/Vendor/sim/libpokecore-sim.a}"
DUPS_OK=0
if [ -f "$ARCHIVE" ]; then
  DUPNAMES="$("$NM" -g --defined-only "$ARCHIVE" 2>/dev/null | awk '$2=="T" {print $3}' | sort | uniq -d | head -n 10)"
  if [ -z "$DUPNAMES" ]; then
    echo "  none"
    DUPS_OK=1
  else
    echo "!! duplicate globals in $ARCHIVE:" >&2
    echo "$DUPNAMES" >&2
  fi
else
  echo "!! no archive at $ARCHIVE" >&2
fi

echo "== verdict =="
PLAT_OK=0
[ "$(macho_platform "$BIN")" = "iossimulator" ] && PLAT_OK=1
SCRIPT_OK=0
[ -f "$RES/main.lua" ] && [ -f "$RES/bizhawk_compat.lua" ] && SCRIPT_OK=1
PLIST_OK=0
grep -q 'iPhoneSimulator' "$APP/Info.plist" 2>/dev/null && PLIST_OK=1

echo "simulator platform : $PLAT_OK"
echo "scripts present    : $SCRIPT_OK"
echo "ppsspp assets      : $ASSETS_OK"
echo "no duplicate globals: $DUPS_OK"
echo "Info.plist sim keys: $PLIST_OK"
echo "core linked in     : $([ "${TOTAL:-0}" -gt 5000 ] && echo 1 || echo 0)  (total symbols $TOTAL)"

if [ "$PLAT_OK" = "1" ] && [ "$SCRIPT_OK" = "1" ] && [ "$ASSETS_OK" = "1" ] && [ "$PLIST_OK" = "1" ] && [ "$DUPS_OK" = "1" ] && [ "${TOTAL:-0}" -gt 5000 ]; then
  echo "PASS — a simulator-targeted app with the core and scripts inside."
  exit 0
fi
echo "FAIL" >&2
exit 1
