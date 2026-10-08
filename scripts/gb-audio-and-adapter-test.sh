#!/usr/bin/env bash
# =============================================================================
# gb-audio-and-adapter-test.sh
#
# Two host-side gates for regressions that were both SILENT on device:
#
#   1. THE GAME BOY AUDIO PATH EXISTS. kGbaOps's read_audio slot was NULL, so every GB/GBC/GBA
#      game was silent while the core filled its ring and nothing read it. A NULL there compiles,
#      links, boots and says nothing.
#   2. THE GBA ADAPTER REFUSES A GAME BOY COLOR CARTRIDGE. gba_attach accepted an empty game code
#      ("let the reader identify itself"), and this adapter's reads are GBA addresses -- on a GBC
#      cartridge those are arbitrary data, heard as "a bunch of numbers".
#
# ⛔ BOTH CHECKS ARE BEHAVIOURAL, NOT GREPS. A grep for a symbol passes after the condition around
# it is deleted; the first version of this gate did exactly that and survived its own mutation.
# The audio half ASKS THE CORE FOR SAMPLES; the adapter half ASKS IT WHETHER IT IS READY.
#
# Needs the mGBA tree, the ROM library and the built host objects. SKIPS (exit 0, loudly) when
# any is absent, so CI runs it where the pieces exist without failing where they do not.
# =============================================================================
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1

GBC="/mnt/c/Users/Devin Prater/Dropbox/games/GBA/Pokemon - Crystal Version (USA).gbc"
DIR="$R/Sources/OpenGameAccess/Resources/gba-lua"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0

skip() { echo "SKIP: $*"; exit 0; }
ok()   { printf '  ok   %s\n' "$*"; }
bad()  { printf '  FAIL %s\n' "$*"; fail=1; }

[ -f "$GBC" ] || skip "no Crystal ROM at $GBC (needs the ROM library)"
[ -d "$HOME/src/mgba" ] || skip "no mGBA tree"
[ -d Vendor/hostobj ] || skip "no host objects (run scripts/build-host.sh)"
[ -f "$DIR/oga_bootstrap.lua" ] || skip "no reader set"

# ---- the probe: asks the core the two questions the device report raised
cat > "$TMP/probe.cpp" <<'CPP'
#include "pokecore.h"
#include <cstdio>
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    if (!poke_load_rom(core, argv[1], nullptr)) { printf("LOADFAIL\n"); return 1; }
    poke_set_script_dir(core, argv[2]);
    poke_set_audio_enabled(core, true);
    if (!poke_start(core)) { printf("STARTFAIL\n"); return 1; }
    for (int f = 0; f < 900; f++) poke_frame(core);

    // Q1: does the console's own audio reach the host at all?
    static int16_t buf[2048 * 2];
    int best = 0;
    for (int f = 0; f < 600; f++) {
        poke_frame(core);
        if (f % 100 == 0) { int n = poke_read_audio(core, buf, 2048); if (n > best) best = n; }
    }
    printf("AUDIO_FRAMES %d\n", best);

    // Q2: does the native GBA adapter claim this cartridge?
    printf("ADAPTER_READY %d\n", poke_adapter_ready(core) ? 1 : 0);
    return 0;
}
CPP

g++ -O2 -g -DPOKE_HOST=1 -ICore -ISources/CPokeCore/include \
  -I"$HOME/src/melonds-lua/src" -std=c++17 \
  -o "$TMP/probe" "$TMP/probe.cpp" Vendor/hostobj/*.o -lpthread -lm -ldl > "$TMP/link.log" 2>&1
if [ ! -x "$TMP/probe" ]; then
  echo "SKIP: the probe did not link (see below)"; tail -5 "$TMP/link.log"; exit 0
fi

out=$(timeout 900 "$TMP/probe" "$GBC" "$DIR" 2>/dev/null | grep -vE "^GB I/O")

echo "== 1. the Game Boy audio path returns real frames"
frames=$(printf '%s\n' "$out" | sed -n 's/^AUDIO_FRAMES //p' | head -1)
if [ -n "${frames:-}" ] && [ "$frames" -gt 0 ] 2>/dev/null; then
  ok "poke_read_audio returned $frames frames (was 0 with the NULL slot)"
else
  bad "poke_read_audio returned ${frames:-nothing} frames -- the GB/GBC/GBA path is silent"
fi

echo "== 2. the GBA adapter refuses a Game Boy Color cartridge"
ready=$(printf '%s\n' "$out" | sed -n 's/^ADAPTER_READY //p' | head -1)
if [ "${ready:-1}" = "0" ]; then
  ok "the adapter refuses Crystal (a .gbc); the Lua reader set handles this console"
else
  bad "the GBA adapter attached to a .gbc and will read GBA addresses at it"
fi

echo
if [ "$fail" -eq 0 ]; then echo "PASS: GB audio is live and the GBA adapter is console-gated."
else echo "FAIL: see above."; fi
exit "$fail"
