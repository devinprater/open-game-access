#!/usr/bin/env bash
# verify-device.sh — the gate before installing on the phone: the DEVICE app must
# compile, link, and contain the core + both Lua resources.
#
# The core archive is rebuilt only when its sources are newer, so this is fast
# once the core is up to date — but it is NOT skipped: a Swift-only change still
# has to link against the real archive, and that link is the check.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
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

APP="$ROOT/xtool/PokemonAccess.app"
echo
if [ -d "$APP" ]; then
  echo "== device app: $APP"
  ls -la "$APP"
  echo "-- platform --"
  /usr/local/swift/bin/llvm-objdump --macho --private-headers "$APP/PokemonAccess" 2>/dev/null \
    | grep -A2 LC_BUILD_VERSION | head -4
  echo "-- core linked: $(/usr/local/swift/bin/llvm-nm "$APP/PokemonAccess" 2>/dev/null | grep -c melonDS) melonDS symbols, $(/usr/local/swift/bin/llvm-nm "$APP/PokemonAccess" 2>/dev/null | grep -c ' T _poke_') poke_* entry points"
  echo "-- resources --"
  sha256sum "$APP"/PokemonAccess_PokemonAccess.bundle/Resources/*.lua 2>/dev/null
else
  echo "!! no .app produced"
  exit 1
fi
