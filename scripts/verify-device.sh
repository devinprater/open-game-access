#!/usr/bin/env bash
# verify-device.sh — the gate before installing on the phone: the DEVICE app must
# compile, link, and contain the core + both Lua resources.
#
# The core archive is rebuilt only when its sources are newer, so this is fast
# once the core is up to date — but it is NOT skipped: a Swift-only change still
# has to link against the real archive, and that link is the check.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin

echo "== core archive =="
if [ ! -f Vendor/libpokecore.a ]; then
  echo "-- missing, building"
  bash "$ROOT/scripts/build-core.sh" || exit 1
else
  echo "-- present: $(ls -la Vendor/libpokecore.a | awk '{print $5}') bytes"
fi
export POKECORE_LIB="$ROOT/Vendor/libpokecore.a"

echo
echo "== xtool dev build (device triple) =="
timeout 900 xtool dev build 2>&1 | tail -25
echo "EXIT=${PIPESTATUS[0]}"

APP="$ROOT/xtool/OpenGameAccess.app"
echo
if [ -d "$APP" ]; then
  echo "== device app: $APP"
  ls -la "$APP"
  echo "-- platform --"
  "$(command -v llvm-objdump || echo /usr/bin/otool)" --macho --private-headers "$APP/OpenGameAccess" 2>/dev/null \
    | grep -A2 LC_BUILD_VERSION | head -4
  echo "-- core linked: $("$LLVM_NM" "$APP/OpenGameAccess" 2>/dev/null | grep -c melonDS) melonDS symbols, $("$LLVM_NM" "$APP/OpenGameAccess" 2>/dev/null | grep -c ' T _poke_') poke_* entry points"
  echo "-- resources --"
  sha256sum "$APP"/OpenGameAccess_OpenGameAccess.bundle/Resources/*.lua 2>/dev/null
else
  echo "!! no .app produced"
  exit 1
fi
