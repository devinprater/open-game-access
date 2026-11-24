#!/usr/bin/env bash
# n64-host-proof.sh — boot the app's real N64 glue (Core/n64_core.cpp) on the
# host, against Mupen64Plus-Next built from the SAME source list and flags the
# app builds use (scripts/core-sources.sh: M64P_ALL, M64P_OGA_C, M64P_DEFS,
# m64p_inc), and a synthetic cartridge (scripts/n64-smoke-rom.py). No ROM file
# from anywhere else is used or needed.
#
# What it proves (each line of the driver's output names its own check):
#   * the trimmed, GL-free core set compiles and links with nothing but libc,
#     libstdc++ and pthread (no GL, no zlib, no libpng);
#   * scripts/m64p-isolate.sh renames every core global, and the renamed core
#     links beside an object that defines the core's own generic names
#     (retro_init, main_run, md5_init, g_dev, path_basename) — while the
#     UNRENAMED objects do not (the mutation half: the isolation is load-bearing);
#   * a cartridge boots on the software path (angrylion + cxd4 + cached
#     interpreter), delivers video and audio, and its RDRAM reads back through
#     n64_read in the CPU's big-endian view, through all three address windows;
#   * savestates round-trip, junk is refused, and a SECOND core in the same
#     process boots too (pokecore recreates the backend between games).
#
# Usage: n64-host-proof.sh            (M64P_SRC defaults to ~/src/mupen64plus-libretro-nx;
#                                      when absent it is fetched at the pinned revision)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/core-sources.sh"

if [ ! -d "$M64P_SRC/mupen64plus-core" ]; then
  # The pin lives in ONE place, bootstrap-deps.sh; read it rather than copy it.
  rev="$(grep -o 'M64P_REV:-[0-9a-f]\{40\}' "$ROOT/scripts/bootstrap-deps.sh" | head -1 | cut -d- -f2)"
  repo="$(grep -o 'M64P_REPO:-[^}]*' "$ROOT/scripts/bootstrap-deps.sh" | head -1 | cut -d- -f2-)"
  [ -n "$rev" ] && [ -n "$repo" ] || { echo "!! no M64P pin in bootstrap-deps.sh" >&2; exit 1; }
  echo "== fetching Mupen64Plus-Next $rev into $M64P_SRC"
  mkdir -p "$M64P_SRC" && git -C "$M64P_SRC" init -q && \
    git -C "$M64P_SRC" fetch -q --depth 1 "$repo" "$rev" && \
    git -C "$M64P_SRC" checkout -q FETCH_HEAD || { echo "!! fetch failed" >&2; exit 1; }
fi

OUT="${N64_PROOF_OUT:-$ROOT/Vendor/n64proof}"
RAW="$OUT/raw"; ISO="$OUT/iso"
mkdir -p "$RAW" "$ISO"
CC="${CC:-gcc}"; CXX="${CXX:-g++}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
M64P_INC="$(m64p_inc "$M64P_SRC" "$ROOT")"
# The host's OS define, as the app builds pass theirs (OS_IOS on iOS).
M64P_CFLAGS="-O2 -g -fPIC -w $M64P_DEFS -DOS_LINUX -DHAVE_POSIX_MEMALIGN=1 $M64P_INC"

# ---- compile the core (cached on the flag string + source mtime) ----
{
  for f in $M64P_ALL; do
    tag="m64p_$(echo "$f" | tr '/' '_' | sed 's/\.[a-z]*$//')"
    printf '%s|%s\n' "$M64P_SRC/$f" "$tag"
  done
  for f in $M64P_OGA_C; do printf '%s|%s\n' "$ROOT/Core/$f" "m64p_oga_$(basename "$f" .c)"; done
} > "$OUT/list.txt"
rm -f "$OUT/.failed"
export CC CXX M64P_CFLAGS RAW OUT
xargs -P "$JOBS" -I{} bash -c '
  IFS="|" read -r src tag <<< "{}"
  out="$RAW/$tag.o"; ff="$RAW/$tag.flags"
  case "$src" in *.cpp) cc="$CXX -std=gnu++11"; ;; *) cc="$CC -std=gnu11"; ;; esac
  sig="$cc $M64P_CFLAGS"
  if [ -f "$out" ] && [ "$out" -nt "$src" ] && [ -f "$ff" ] && [ "$(cat "$ff")" = "$sig" ]; then exit 0; fi
  if $cc $M64P_CFLAGS -c "$src" -o "$out" 2> "$RAW/$tag.err"; then echo "$sig" > "$ff"
  else echo "FAIL $tag"; head -20 "$RAW/$tag.err"; touch "$OUT/.failed"; fi
' < "$OUT/list.txt"
[ -f "$OUT/.failed" ] && { echo "!! core compile errors" >&2; exit 1; }
want=$(wc -l < "$OUT/list.txt"); have=$(ls "$RAW"/m64p_*.o 2>/dev/null | wc -l)
[ "$have" -eq "$want" ] || { echo "!! $have of $want core objects built" >&2; exit 1; }
echo "== core: $have objects"

# ---- isolate (on a copy, so the raw set is kept for the mutation half) ----
rm -f "$ISO"/*.o
cp "$RAW"/m64p_*.o "$ISO/"
bash "$ROOT/scripts/m64p-isolate.sh" "$ISO" || exit 1

# ---- collision probe: the core's own generic names, defined by "another core" ----
cat > "$OUT/collide.c" <<'EOF'
/* Stand-in for another emulator in the same link: it defines names the
 * libretro core also defines. Only the isolated core may link beside it. */
int g_dev;
void retro_init(void) {}
void main_run(void) {}
void md5_init(void *s) { (void) s; }
const char *path_basename(const char *p) { return p; }
EOF
"$CC" -c -fPIC -o "$OUT/collide.o" "$OUT/collide.c" || exit 1

if "$CXX" -shared -o "$OUT/mutant.so" "$RAW"/m64p_*.o "$OUT/collide.o" -lpthread 2> "$OUT/mutant.err"; then
  echo "FAIL: the UNRENAMED core linked beside colliding names -- the probe proves nothing" >&2
  exit 1
fi
grep -q "multiple definition" "$OUT/mutant.err" || { echo "FAIL: the mutant link failed, but not on a duplicate symbol:" >&2; head -5 "$OUT/mutant.err" >&2; exit 1; }
echo "ok: the unrenamed core does NOT link beside colliding names ($(grep -c 'multiple definition' "$OUT/mutant.err") duplicates)"

# ---- the glue and the driver ----
GLUE_FLAGS="-O1 -g -fPIC -std=c++17 -I$ROOT/Core -I$M64P_SRC/libretro-common/include -Wall -Wno-unused-function -Werror=return-type"
"$CXX" $GLUE_FLAGS -c "$ROOT/Core/n64_core.cpp" -o "$OUT/n64_core.o" || exit 1
"$CXX" $GLUE_FLAGS -c "$ROOT/scripts/n64-host-proof-main.cpp" -o "$OUT/main.o" || exit 1
"$CXX" -o "$OUT/n64-proof" "$OUT/main.o" "$OUT/n64_core.o" "$ISO"/m64p_*.o "$OUT/collide.o" \
  -Wl,--no-undefined -lpthread -lm || { echo "FAIL: the isolated core did not link beside colliding names" >&2; exit 1; }
echo "ok: the isolated core links beside colliding names, with no undefined symbols"

# ---- the cartridge, in two byte orders, and the run ----
python3 "$ROOT/scripts/n64-smoke-rom.py" "$OUT/smoke.z64" --byteorder z64 || exit 1
python3 "$ROOT/scripts/n64-smoke-rom.py" "$OUT/smoke.v64" --byteorder v64 || exit 1
RUN="$OUT/run"; rm -rf "$RUN"; mkdir -p "$RUN"
( cd "$RUN" && timeout 300 "$OUT/n64-proof" "$OUT/smoke.z64" "$OUT/smoke.v64" "$RUN/smoke.srm" "$RUN/smoke.state" ) 2>&1 \
  | tee "$OUT/proof.log"
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] || { echo "!! N64 host proof failed (exit $rc)" >&2; exit 1; }
