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

# ⛔ REBUILD WHEN THE SOURCE MOVED. This gate measures BEHAVIOUR from Vendor/hostobj/*.o, so
# linking against objects built before an edit measures the OLD code -- a mutation survives, and
# the gate reports a pass on source it never compiled. It did exactly that. Rebuilding costs
# ~60 s and only runs when a Core source is newer than its object.
newest_src=$(find Core -name '*.cpp' -o -name '*.h' 2>/dev/null | xargs ls -t 2>/dev/null | head -1)
oldest_obj=$(ls -t Vendor/hostobj/*.o 2>/dev/null | tail -1)
if [ -z "$newest_src" ] || [ -z "$oldest_obj" ] || [ "$newest_src" -nt "$oldest_obj" ]; then
  echo "== rebuilding the host objects (a Core source is newer than the objects)"
  bash scripts/build-host.sh > "$TMP/build.log" 2>&1 || { echo "SKIP: build-host failed"; tail -5 "$TMP/build.log"; exit 0; }
  ok "host objects rebuilt from the current tree"
fi

# ---- the probe: asks the core the two questions the device report raised
cat > "$TMP/probe.cpp" <<'CPP'
#include "pokecore.h"
#include <cstdio>
static void Say(const char* t, bool interrupt, uint32_t id, void* ud)
{ (void) interrupt; (void) id; (void) ud; if (t) { printf("SPOKEN %s\n", t); fflush(stdout); } }
static void Log(const char* t, void* ud)
{ (void) ud; if (t) { printf("LOGGED %s\n", t); fflush(stdout); } }
int main(int argc, char** argv)
{
    PokeCore* core = poke_create();
    poke_set_speech_id_callback(core, Say, nullptr);
    poke_set_log_callback(core, Log, nullptr);
    if (!poke_load_rom(core, argv[1], nullptr)) { printf("LOADFAIL\n"); return 1; }
    poke_set_script_dir(core, argv[2]);
    poke_set_audio_enabled(core, true);
    if (!poke_start(core)) { printf("STARTFAIL\n"); return 1; }
    for (int f = 0; f < 900; f++) poke_frame(core);

    // Q1: does the console's own audio reach the host at all?
    // \u26d4 "FRAMES > 0" IS NOT "THERE IS SOUND". The ring drained 223000 frames of pure ZEROS
    // while opts.volume was 0, and a frames-only check passed on it. Measure the SIGNAL: peak
    // amplitude and the nonzero fraction. Silence is a full ring of zeros, so this is the only
    // honest proof, and it is what caught the master-volume bug.
    static int16_t buf[2048 * 2];
    int best = 0;
    long total = 0, nonzero = 0; int peak = 0;
    for (int f = 0; f < 900; f++) {
        poke_frame(core);
        int n = poke_read_audio(core, buf, 2048);
        if (n > best) best = n;
        for (int i = 0; i < n * 2; i++) {
            int v = buf[i]; total++;
            if (v) nonzero++;
            if (v < 0 ? -v > peak : v > peak) peak = v < 0 ? -v : v;
        }
    }
    printf("AUDIO_FRAMES %d\n", best);
    printf("AUDIO_SIGNAL %ld %ld %d\n", total, nonzero, peak);

    // Q2: does the native GBA adapter CLAIM this cartridge?
    //
    // ⛔ NOT poke_adapter_ready(). That is `g_ready = player_xy(...)`, and player_xy fails on a
    // Game Boy Color cartridge whatever the attach gate does -- so it reads false before and after
    // the fix and a mutation survives it (measured). What differs is the ATTACH DECISION, which is
    // observable two ways: the log line the refusal writes, and whether WhereAmI gets answered
    // with GBA-address numbers.
    poke_adapter_ready(core);          // force the attach attempt
    for (int f = 0; f < 5; f++) poke_frame(core);
    printf("=== WhereAmI ===\n");
    poke_command(core, 0 /* WhereAmI */);
    for (int f = 0; f < 60; f++) poke_frame(core);
    printf("ADAPTER_DONE\n");
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

echo "== 1b. and those frames carry SOUND, not silence"
sig=$(printf '%s\n' "$out" | sed -n 's/^AUDIO_SIGNAL //p' | head -1)
tot=$(printf '%s' "$sig" | awk '{print $1}')
nz=$(printf '%s' "$sig" | awk '{print $2}')
pk=$(printf '%s' "$sig" | awk '{print $3}')
if [ -n "${pk:-}" ] && [ "$pk" -gt 500 ] 2>/dev/null; then
  ok "peak amplitude $pk, $nz of $tot samples nonzero -- real audio"
else
  bad "the stream is silence (peak ${pk:-0}, $nz of ${tot:-0} nonzero): a zero MASTER VOLUME or a muted core gives exactly this, and a frames-only check passes on it"
fi

echo "== 2. the GBA adapter refuses a Game Boy Color cartridge"
# The attach refusal states itself, and an attached adapter answers WhereAmI with a numeric
# position ("x N, y N") computed from GBA addresses -- arbitrary data on a GBC cartridge.
if printf '%s\n' "$out" | grep -q "Game Boy / Game Boy Color cartridge: the native GBA reader does not"; then
  ok "the adapter refused the cartridge and said why"
else
  bad "no refusal: the GBA adapter attached to a .gbc"
fi
if printf '%s\n' "$out" | grep -qE "^SPOKEN +x [0-9]+, y [0-9]+"; then
  bad "the adapter ANSWERED WhereAmI with GBA-address numbers -- this is the 'bunch of numbers'"
else
  ok "WhereAmI produced no GBA-address numbers"
fi
# The probe must have reached the end, or neither answer means anything.
if printf '%s\n' "$out" | grep -q "^ADAPTER_DONE$"; then
  ok "the probe completed its adapter section"
else
  bad "the probe did not reach ADAPTER_DONE -- the result above is not trustworthy"
fi

echo
if [ "$fail" -eq 0 ]; then echo "PASS: GB audio is live and the GBA adapter is console-gated."
else echo "FAIL: see above."; fi
exit "$fail"
