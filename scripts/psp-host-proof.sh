#!/usr/bin/env bash
# psp-host-proof.sh — reproduce the host proof the PPSSPP pin rests on.
#
# Compiles the audited subset from scripts/core-sources.sh with the host
# compiler (Linux g++, no Apple SDK), links the proof harness, boots a PSP
# game image, and asserts: expected game ID, all requested frames ran, the
# framebuffer is live (nonzero pixels), clean shutdown.
#
# Needs: g++ 11+, the fetched PPSSPP tree (scripts/bootstrap-deps.sh), and a
# game image. ROMs live outside repos (see memory: Dropbox/games/<platform>).
# Usage: DISSIDIA_CSO=~/path/to/game.cso ./scripts/psp-host-proof.sh [frames]
# Without a game image the script SKIPS (exit 0) — CI has no ROMs, and a
# red CI on missing-ROM would teach everyone to ignore this proof.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG="${DISSIDIA_CSO:-$HOME/dissidia.cso}"
FRAMES="${1:-900}"

if [ ! -f "$IMG" ]; then
  echo "SKIP: no game image at $IMG (set DISSIDIA_CSO); compile/CI gates still apply"
  exit 0
fi
[ -d "$PPSPP_SRC/Core" ] || { echo "FAIL: no PPSSPP tree at $PPSPP_SRC" >&2; exit 1; }
command -v g++ >/dev/null || { echo "FAIL: host g++ not found" >&2; exit 1; }

INC="$(ppspp_inc "$PPSPP_SRC")"
CXX="g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -std=c++17 $INC -I$ROOT/Core"
CC="gcc -O1 -g -fPIC -fwrapv -fno-strict-aliasing $INC"
mkdir -p "$OUT/host-obj" "$OUT/host-save"

n=0
compile_one() { # compile_one <lang> <src> <obj>
  n=$((n + 1))
  case "$1" in
    ppspp) $CXX -c "$2" -o "$3" ;;
    ppsppc) $CC -c "$2" -o "$3" ;;
    ppsppx) $CC -DSTACK_LINE_READER_BUFFER_SIZE=1024 -c "$2" -o "$3" ;;
    ppsppasm) $CC -c "$2" -o "$3" ;;
  esac
}
fail=0
t=0
for f in $PPSPP_CORE; do
  o="$OUT/host-obj/c_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { compile_one ppspp "$PPSPP_SRC/$f" "$o" || fail=1; }
  t=$((t + 1))
done
for f in $PPSPP_EXT_CPP; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { compile_one ppspp "$PPSPP_SRC/$f" "$o" || fail=1; }
  t=$((t + 1))
done
for f in $PPSPP_EXT_C; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
  [ -f "$o" ] || { compile_one ppsppc "$PPSPP_SRC/$f" "$o" || fail=1; }
  t=$((t + 1))
done
for f in $PPSPP_LUA; do
  o="$OUT/host-obj/l_$(basename "$f" .c).o"
  [ -f "$o" ] || { compile_one ppsppc "$PPSPP_SRC/ext/lua/$f" "$o" || fail=1; }
  t=$((t + 1))
done
case "$(uname -m)" in
  x86_64)
    for f in $PPSPP_X86; do
      o="$OUT/host-obj/x_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
      [ -f "$o" ] || { compile_one ppsppx "$PPSPP_SRC/$f" "$o" || fail=1; }
      t=$((t + 1))
    done
    for f in $PPSPP_X86_ASM; do
      o="$OUT/host-obj/x_$(basename "$f" .S).o"
      [ -f "$o" ] || { compile_one ppsppasm "$PPSPP_SRC/$f" "$o" || fail=1; }
      t=$((t + 1))
    done ;;
esac
for f in $PPSPP_GLUE; do
  o="$OUT/host-obj/g_$(basename "$f" .cpp).o"
  rm -f "$o" # glue is small and changes often; always rebuild it
  compile_one ppspp "$ROOT/Core/$f" "$o" || fail=1
  t=$((t + 1))
done
[ "$fail" -eq 0 ] || { echo "FAIL: $fail compile errors above" >&2; exit 1; }
echo "== compiled $t translation units =="

$CXX -c "$ROOT/scripts/psp-host-proof-main.cpp" -o "$OUT/host-obj/proof-main.o"
g++ -O1 -g "$OUT"/host-obj/*.o -o "$OUT/psp-proof" -lz -lpthread -ldl
echo "== linked $OUT/psp-proof =="

out="$("$OUT/psp-proof" "$IMG" "$OUT/host-save" "$FRAMES" 2>"$OUT/proof-stderr.log")"
echo "$out"
echo "$out" | grep -q "PROOF: gameid=ULUS10437" || { echo "FAIL: unexpected game id" >&2; exit 1; }
echo "$out" | grep -q "PROOF: frames=$FRAMES/$FRAMES" || { echo "FAIL: short run" >&2; exit 1; }
echo "$out" | grep -q "PROOF: fb=480x272" || { echo "FAIL: bad framebuffer geometry" >&2; exit 1; }
[ "$(echo "$out" | sed -n 's/PROOF: nonzero=//p')" -gt 0 ] || { echo "FAIL: blank framebuffer" >&2; exit 1; }
echo "$out" | grep -q "PROOF: shutdown-clean" || { echo "FAIL: unclean shutdown" >&2; exit 1; }
echo "PASS: PPSSPP host proof (game boots, frames run, framebuffer live)"
