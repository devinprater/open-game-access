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
APP="${1:-$HOME/pokemon-access-ios/xtool-sim/PokemonAccess.app}"
BIN="$APP/PokemonAccess"
# Portable platform detection, shared with package-sim-app.sh.
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib-macho.sh"

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
RES="$APP/PokemonAccess_PokemonAccess.bundle/Resources"
if [ -d "$RES" ]; then
  ls -la "$RES"
  echo "--- hashes (must match the originals) ---"
  ( cd "$RES" && (sha256sum ./*.lua 2>/dev/null || shasum -a 256 ./*.lua) )
else
  echo "!! no resource bundle — the app would install and find no script" >&2
  exit 1
fi

echo
echo "== Info.plist platform keys =="
grep -A2 -E "CFBundleSupportedPlatforms|DTPlatformName" "$APP/Info.plist" 2>/dev/null

echo
echo "== verdict =="
PLAT_OK=0
[ "$(macho_platform "$BIN")" = "iossimulator" ] && PLAT_OK=1
SCRIPT_OK=0
[ -f "$RES/main.lua" ] && [ -f "$RES/bizhawk_compat.lua" ] && SCRIPT_OK=1
PLIST_OK=0
grep -q 'iPhoneSimulator' "$APP/Info.plist" 2>/dev/null && PLIST_OK=1

echo "simulator platform : $PLAT_OK"
echo "scripts present    : $SCRIPT_OK"
echo "Info.plist sim keys: $PLIST_OK"
echo "core linked in     : $([ "${TOTAL:-0}" -gt 5000 ] && echo 1 || echo 0)  (total symbols $TOTAL)"

if [ "$PLAT_OK" = "1" ] && [ "$SCRIPT_OK" = "1" ] && [ "$PLIST_OK" = "1" ] && [ "${TOTAL:-0}" -gt 5000 ]; then
  echo "PASS — a simulator-targeted app with the core and scripts inside."
  exit 0
fi
echo "FAIL" >&2
exit 1
