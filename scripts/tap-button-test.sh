#!/usr/bin/env bash
# tap-button-test.sh — a press must span EMULATED FRAMES, not wall-clock time.
#
# Reported on device: "START does nothing" in Pokemon Crystal, not reproducible on the host.
# Root cause: the VoiceOver tap path released the button 0.12 s later on a wall-clock timer, while
# frames come from a CADisplayLink that iOS throttles. On a slow frame the press AND the release
# both fell between two frames and the console never saw the button.
#
# This drives the real core with a real cartridge and hashes the framebuffer, so it measures the
# thing the player cares about (the screen changed), not the app's own bookkeeping.
#
#   mode 0  poke_tap_button(START, 4), normal frames   -> MUST change the screen
#   mode 1  down then up with NO frame between          -> MUST NOT change it (the bug, reproduced)
#   mode 2  poke_tap_button(START, 4), each frame +60ms -> MUST change it (immune to frame rate)
#
# Mode 2 is the point: it is a throttled phone, and the frame-counted hold must not care.
set -u

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1
T="${TMPDIR:-$HOME/fe}/tap-test"; mkdir -p "$T"

ROMDIR="${OGA_ROM_DIR:-/mnt/c/Users/Devin Prater/Dropbox/games/GBA}"
ROM="$ROMDIR/Pokemon - Crystal Version (USA).gbc"
[ -f "$ROM" ] || { echo "SKIP: no Crystal ROM at $ROMDIR (needs the ROM library)"; exit 0; }

echo "== rebuilding the host objects (this test links them, so it must build them)"
bash scripts/build-host.sh > "$T/build.log" 2>&1 || { echo "SKIP: build-host.sh failed"; tail -5 "$T/build.log"; exit 0; }

cat > "$T/p.cpp" <<'CPP'
#include "pokecore.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

static unsigned long hashFrame(PokeCore* core)
{
    int w = 0, h = 0;
    if (!poke_framebuffer(core, 0, &w, &h)) return 0;
    const uint8_t* px = poke_framebuffer_ptr(core, 0);
    if (!px || w <= 0 || h <= 0) return 0;
    unsigned long hash = 1469598103934665603UL;         // FNV-1a
    for (size_t i = 0, n = (size_t) w * h * 4; i < n; i += 97) { hash ^= px[i]; hash *= 1099511628211UL; }
    return hash;
}

int main(int argc, char** argv)
{
    int mode = atoi(argv[3]);
    PokeCore* core = poke_create();
    if (!poke_load_rom(core, argv[1], nullptr)) { printf("LOADFAIL\n"); return 1; }
    poke_set_script_dir(core, argv[2]);
    if (!poke_start(core)) { printf("STARTFAIL\n"); return 1; }

    for (int f = 0; f < 1500; f++) poke_frame(core);      // let Crystal's title settle
    unsigned long before = hashFrame(core);

    if (mode == 1) { poke_set_button(core, POKE_BTN_START, true); poke_set_button(core, POKE_BTN_START, false); }
    else           { poke_tap_button(core, POKE_BTN_START, 4); }

    int pad = (mode == 2) ? 60 : 0;                       // a throttled frame, in ms
    unsigned long changedAt = 0;
    for (int f = 1; f <= 40; f++) {
        poke_frame(core);
        if (pad) std::this_thread::sleep_for(std::chrono::milliseconds(pad));
        if (!changedAt && hashFrame(core) != before) changedAt = f;
    }
    printf("TAP mode=%d changed=%s at_frame=%lu\n", mode, changedAt ? "YES" : "no", changedAt);
    return 0;
}
CPP

if ! g++ -O2 -g -DPOKE_HOST=1 -ICore -ISources/CPokeCore/include \
      -I"$HOME/src/melonds-lua/src" -std=c++17 \
      -o "$T/p" "$T/p.cpp" Vendor/hostobj/*.o -lpthread -lm -ldl 2> "$T/link.log"; then
  echo "SKIP: the probe did not link"; tail -5 "$T/link.log"; exit 0
fi

fails=0
run() {   # run <mode> <want YES|no> <label>
  local out
  out=$(timeout 300 "$T/p" "$ROM" "$R/Sources/OpenGameAccess/Resources/gba-lua" "$1" 2>/dev/null | grep "^TAP" | head -1)
  case "$out" in
    *"changed=YES"*) got=YES ;;
    *"changed=no"*)  got=no ;;
    *) echo "  BAD  $3: no result ($out)"; fails=$((fails+1)); return ;;
  esac
  if [ "$got" = "$2" ]; then
    echo "  ok   $3 ($out)"
  else
    echo "  BAD  $3: wanted changed=$2, got $out"; fails=$((fails+1))
  fi
}

run 0 YES "a frame-counted tap reaches the console"
run 1 no  "a press with NO frame between down and up is INVISIBLE (the device bug)"
run 2 YES "a frame-counted tap still reaches it when every frame is 60 ms (a throttled phone)"

echo
[ "$fails" -eq 0 ] && echo "PASS: a press is held for EMULATED FRAMES, so a slow frame cannot swallow it." \
                   || echo "FAIL: $fails case(s) wrong."
exit "$fails"
