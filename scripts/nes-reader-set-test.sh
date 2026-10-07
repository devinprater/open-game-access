#!/usr/bin/env bash
# nes-reader-set-test.sh — does the CORE pick and LOAD the bundled NES reader?
#
# ⛔ WHY THIS TEST EXISTS AND WHY IT STAGES A COPY.
#
# v0.6.0-nes shipped with "NES readers host and speak" in its release notes and NO
# reader assets in the IPA. Nothing in the build could see it: the Mesen core linked
# (281 objects in the binary), the resolver dispatched .nes correctly, and no code ever
# pointed the NES backend at a script. "Compiles", "links" and "the core boots" were all
# true and the player heard nothing.
#
# So this test drives the app's OWN reader-set layout and requires SPEECH. It runs the
# same source lists the app compiles (scripts/build-host.sh) and the same wrapper files
# that get bundled.
#
# ⛔ IT RUNS AGAINST A STAGED COPY, NOT THE REPO TREE. The readers OPEN FILES FOR
# WRITING in their own Data/ directory (speech text, settings, progress). Running them
# in place wrote those files back into the source tree -- both a dirty checkout and a
# preview of the bug the app must avoid: an app bundle is read-only, so the Swift side
# copies the set into the container before handing the path over. This test does the
# same, so it exercises the shape the device does.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
MESEN="${MESEN_SRC:-$HOME/src/mesen}"
OUT="$HOME/oga-nes-proof"
SRC_READERS="$ROOT/Sources/OpenGameAccess/Resources/nes-lua"
STAGE="$OUT/readers"          # the writable copy the readers actually run from
mkdir -p "$OUT"

if [ ! -f "$MESEN/Core/pch.h" ]; then
  echo "!! no Mesen source at $MESEN (run scripts/bootstrap-deps.sh)" >&2
  exit 1
fi
[ -d "$SRC_READERS" ] || { echo "!! no bundled NES readers at $SRC_READERS" >&2; exit 1; }

# ---- the writable copy ---------------------------------------------------------------
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -R "$SRC_READERS/." "$STAGE/"
echo "== staged $(find "$STAGE" -type f | wc -l) reader files -> ${STAGE#$HOME/}"

echo "== host objects (the app's own source lists) =="
bash scripts/build-host.sh 2>&1 | tail -3
[ -f Vendor/hostobj/.failed ] && { echo "!! host build failed" >&2; exit 1; }

ar rcs "$OUT/libnes.a" Vendor/hostobj/*.o || exit 1

echo
echo "== build the harness =="
g++ -O0 -g -std=c++17 -w -I"$ROOT/Core" -I"$ROOT/Sources/CPokeCore/include" \
    -I"$MESEN" -I"$MESEN/Core" -I"$MESEN/Utilities" -include "$MESEN/Core/pch.h" \
    -c scripts/nes-reader-set-test.cpp -o "$OUT/settest.o" || { echo "!! compile failed"; exit 1; }
link() { # <obj> <out>
  g++ -O1 -g "$1" "$OUT/libnes.a" -o "$2" -lz -lpthread -ldl -lm \
    "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
    "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
    "$HOME"/ffmpeg-host/lib/libavutil.a
}
link "$OUT/settest.o" "$OUT/nes-reader-set-test" > "$OUT/settest-link.log" 2>&1 \
    || { echo "!! LINK FAILED"; tail -n 20 "$OUT/settest-link.log"; exit 1; }
echo "  linked $OUT/nes-reader-set-test"

# ---- pick ROMs -----------------------------------------------------------------------
GAMES="/mnt/c/Users/Devin Prater/Dropbox/games/NES"
pick() { find "$GAMES" -maxdepth 1 -type f -name "$1" 2>/dev/null | head -1; }

DEFAULT_ROMS=(
  "Legend of Zelda, The (USA).nes"
  "Legend of Zelda, The (USA) (Rev 1).nes"
  "Dragon Warrior (USA).nes"
  "Dragon Warrior (USA) (Rev 1).nes"
  "Super Mario Bros. (World).nes"
)
ROMS=()
if [ "$#" -gt 0 ]; then
  ROMS=("$@")
else
  for name in "${DEFAULT_ROMS[@]}"; do
    r="$(pick "$name")"
    if [ -n "$r" ]; then ROMS+=("$r"); else echo "  (not in the library: $name)"; fi
  done
fi
[ "${#ROMS[@]}" -eq 0 ] && { echo "!! no ROMs found under $GAMES; pass paths as arguments" >&2; exit 1; }

NATIVE="$HOME/nes-rom"; mkdir -p "$NATIVE"
fail=0
run_one() { # <rom> <binary> <frames>
  cp "$1" "$NATIVE/test.nes" 2>/dev/null || return 1
  "$2" "$NATIVE/test.nes" "$STAGE" "$3" 2>&1
}

for rom in "${ROMS[@]}"; do
  echo
  echo "################ $(basename "$rom") ################"
  out="$(run_one "$rom" "$OUT/nes-reader-set-test" 600)" || fail=1
  printf '%s\n' "$out"
  setname="$(printf '%s\n' "$out" | sed -n 's/^reader set *: *//p')"
  base="$(basename "$rom")"
  case "$base" in
    "Legend of Zelda"*)        want="Zelda1Access" ;;
    "Dragon Warrior (USA).nes"|"Dragon Warrior (USA) (Rev 1).nes") want="DragonWarriorAccess" ;;
    *)                         want="" ;;
  esac
  if [ -n "$want" ]; then
    [ "$setname" = "$want" ] || { echo "  !! expected '$want', got '$setname'"; fail=1; }
  fi
done

echo
[ "$fail" -ne 0 ] && { echo "FAIL: the reader-set path is broken (see above)." >&2; exit 1; }
echo "PASS: every known dump resolved to its bundled reader and spoke; unknown dumps resolved to none."

# =======================================================================================
# MUTATION PASS. A check that cannot fail proves nothing -- and this claim was wrong for an
# entire release, so prove the gate can see that exact state.
echo
echo "== mutation: corrupted CRCs must make a known dump resolve to NO reader =="
cp Core/mesen_core.cpp "$OUT/mesen_core.bak"
ROOT="$ROOT" python3 "$ROOT/scripts/nes-reader-set-sabotage.py" || { cp "$OUT/mesen_core.bak" Core/mesen_core.cpp; exit 1; }

bash scripts/build-host.sh >/dev/null 2>&1
ar rcs "$OUT/libnes.a" Vendor/hostobj/*.o
link "$OUT/settest.o" "$OUT/nes-reader-set-test-sab" >/dev/null 2>&1
sab="$(run_one "$(pick 'Legend of Zelda, The (USA).nes')" "$OUT/nes-reader-set-test-sab" 60)"
if printf '%s' "$sab" | grep -q "reader set : (none)"; then
  echo "  ok  Zelda resolved to (none) once its CRC no longer matched"
else
  echo "  !!  Zelda STILL matched with corrupted CRCs -- the identity is not load-bearing"
  fail=1
fi
cp "$OUT/mesen_core.bak" Core/mesen_core.cpp
rm -f "$OUT/nes-reader-set-test-sab"
bash scripts/build-host.sh >/dev/null 2>&1   # restore the real objects
echo "  restored Core/mesen_core.cpp"

echo
[ "$fail" -ne 0 ] && { echo "FAIL: reader identity is not proved by mutation." >&2; exit 1; }
echo "PASS: reader identity and loading are covered, and the identity is proved by mutation."
