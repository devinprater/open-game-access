#!/usr/bin/env bash
# verify-device.sh — the gate before installing on the phone: the DEVICE app must
# compile, link, and contain the core + both Lua resources.
#
# The core archive is rebuilt only when its sources are newer, so this is fast
# once the core is up to date — but it is NOT skipped: a Swift-only change still
# has to link against the real archive, and that link is the check.
#
# This script FAILS when a check cannot run. Before, LLVM_NM was never assigned,
# so under `set -u` both symbol counts expanded to nothing and the gate printed
# "0 melonDS symbols, 0 poke_* entry points" and still exited 0.
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

# llvm-nm, never GNU nm: GNU nm cannot read Mach-O and prints "file format not
# recognized", which grep -c turns into a confident "0 symbols". Probe the real
# archive rather than a dummy file -- no nm can read /dev/null, so probing with
# one rejects every candidate on a machine that has nm.
LLVM_NM=""
for cand in llvm-nm /usr/local/swift/bin/llvm-nm /usr/bin/llvm-nm nm /usr/bin/nm; do
  if command -v "$cand" >/dev/null 2>&1 && "$cand" "$POKECORE_LIB" >/dev/null 2>&1; then
    LLVM_NM="$cand"; break
  fi
done
[ -n "$LLVM_NM" ] || { echo "!! no nm on this host can read Mach-O; the symbol check below cannot run" >&2; exit 1; }

echo
echo "== xtool dev build (device triple) =="
# ${PIPESTATUS[0]} -- xtool's exit code, not tail's. A bare $? is tail's and is
# always 0, so a failed link would still read as a pass.
timeout 900 xtool dev build 2>&1 | tail -25
BUILD_EXIT="${PIPESTATUS[0]}"
echo "EXIT=$BUILD_EXIT"

APP="$ROOT/xtool/OpenGameAccess.app"
echo
if [ -d "$APP" ]; then
  echo "== device app: $APP"
  ls -la "$APP"
  echo "-- platform --"
  # platform must be "ios", never "iossimulator": a simulator binary cannot be
  # installed on the phone, and xtool writes both triples to ./xtool.
  PLATFORM="$("$(command -v llvm-objdump || echo /usr/bin/otool)" --macho --private-headers "$APP/OpenGameAccess" 2>/dev/null \
    | grep -A2 LC_BUILD_VERSION | grep -m1 platform | awk '{print $2}')"
  echo "   platform=$PLATFORM"
  # Materialise the symbol list to a file first: `nm ... | grep -q` dies of
  # SIGPIPE under pipefail when the symbol IS present, and reports failure.
  SYMS="$(mktemp)"; trap 'rm -f "$SYMS"' EXIT
  "$LLVM_NM" "$APP/OpenGameAccess" > "$SYMS" 2>/dev/null || true
  if [ ! -s "$SYMS" ]; then
    echo "!! $LLVM_NM produced no symbols for the binary; the check cannot run" >&2
    exit 1
  fi
  MELON=$(grep -c melonDS "$SYMS" || true)
  POKE=$(grep -c ' T _poke_' "$SYMS" || true)
  echo "-- core linked: $MELON melonDS symbols, $POKE poke_* entry points"
  echo "-- resources --"
  sha256sum "$APP"/OpenGameAccess_OpenGameAccess.bundle/Resources/*.lua 2>/dev/null

  # The gate: a link failure, a simulator binary, or an app with no core in it
  # must FAIL here rather than print a reassuring zero and exit 0.
  [ "$BUILD_EXIT" -eq 0 ] || { echo "!! xtool dev build failed (exit $BUILD_EXIT)" >&2; exit 1; }
  [ "$PLATFORM" = "ios" ] || { echo "!! app platform is '$PLATFORM', not 'ios' — this is not a device build" >&2; exit 1; }
  [ "$MELON" -gt 0 ] || { echo "!! no melonDS symbols: the core is not linked in" >&2; exit 1; }
  [ "$POKE" -gt 0 ] || { echo "!! no poke_* entry points: the core is not linked in" >&2; exit 1; }
  echo "PASS: device app links the core and targets the device"
else
  echo "!! no .app produced"
  exit 1
fi
