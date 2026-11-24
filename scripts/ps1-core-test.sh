#!/usr/bin/env bash
# ps1-core-test.sh — build the pinned PCSX ReARMed exactly as the app builds it
# (the PCSX_ALL list, PCSX_DEFS, the oga_ps1_ prefix pass) and boot a program
# the test writes itself. No disc, no BIOS: Core/ps1_core_test.cpp makes a tiny
# PS-EXE and the HLE BIOS runs it.
#
# What a pass proves: the TU list compiles and links on its own (nothing is
# missing from PCSX_ALL), the prefix pass leaves no generic global behind, the
# wrapper drives the core through libretro, and RAM reads, audio, video and
# states work. What it does not prove: that a commercial disc boots. That needs
# a real game and is on Devin's test list.
#
# Env: PCSX_SRC (default ~/src/pcsx_rearmed, fetched at the pin when missing),
#      OUT (default ~/oga-ps1-test), CC/CXX, JOBS.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
# shellcheck source=core-sources.sh
. "$ROOT/scripts/core-sources.sh"

OUT="${OUT:-$HOME/oga-ps1-test}"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
PCSX_REV="$(sed -n 's/^PCSX_REV="\${PCSX_REV:-\([0-9a-f]*\)}"/\1/p' scripts/bootstrap-deps.sh)"
PCSX_REPO="$(sed -n 's/^PCSX_REPO="\${PCSX_REPO:-\(.*\)}"/\1/p' scripts/bootstrap-deps.sh)"

if [ ! -f "$PCSX_SRC/frontend/libretro.c" ]; then
  echo "== fetching PCSX ReARMed at $PCSX_REV"
  mkdir -p "$(dirname "$PCSX_SRC")"
  git clone --quiet --filter=blob:none "$PCSX_REPO" "$PCSX_SRC" || { echo "!! clone failed" >&2; exit 1; }
fi
git -C "$PCSX_SRC" checkout --quiet "$PCSX_REV" 2>/dev/null \
  || { git -C "$PCSX_SRC" fetch --quiet origin "$PCSX_REV" && git -C "$PCSX_SRC" checkout --quiet "$PCSX_REV"; } \
  || { echo "!! could not check out the pinned PCSX revision $PCSX_REV" >&2; exit 1; }

mkdir -p "$OUT/obj"
rm -f "$OUT"/obj/*.o
INC="-I$ROOT/Core/pcsx"
for d in $PCSX_INC_REL; do INC="$INC -I$PCSX_SRC/$d"; done

echo "== compiling $(echo $PCSX_ALL | wc -w) PCSX TU(s) with $JOBS job(s)"
export CC PCSX_SRC PCSX_DEFS INC OUT
# Object names keep the path (cdrom.c and gpu.c exist twice) so none overwrite.
echo $PCSX_ALL | tr ' ' '\n' | xargs -P "$JOBS" -I{} sh -c '
  o="$OUT/obj/pcsx_$(echo "{}" | tr "/" "_" | sed "s/\.c$/.o/")"
  $CC -std=gnu11 -O2 -g0 -fPIC -fno-common -fno-strict-aliasing -w $PCSX_DEFS $INC \
      -c "$PCSX_SRC/{}" -o "$o" || { echo "!! compile failed: {}" >&2; exit 255; }
' || { echo "!! PCSX compile failed" >&2; exit 1; }

python3 scripts/ps1-prefix-symbols.py --format elf "$OUT"/obj/pcsx_*.o || exit 1

echo "== compiling the wrapper and the test"
$CXX -std=c++17 -O1 -g -Wall -Wextra -Werror -ICore -I"$PCSX_SRC/deps/libretro-common/include" \
    -c Core/ps1_core.cpp -o "$OUT/obj/ps1_core.o" || exit 1
$CXX -std=c++17 -O1 -g -Wall -Wextra -Werror -ICore -c Core/ps1_core_test.cpp -o "$OUT/obj/ps1_core_test.o" || exit 1
$CXX -o "$OUT/ps1-core-test" "$OUT"/obj/*.o -lpthread -lm -ldl || { echo "!! link failed" >&2; exit 1; }

echo "== running"
"$OUT/ps1-core-test"
