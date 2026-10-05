#!/usr/bin/env bash
# cue-capture.sh — render the lock-on cue over real gameplay audio, to a WAV.
#
# WHAT THIS PROVES, AND WHAT IT CANNOT. The cue is synthesised in Swift on the device, so
# this harness re-implements the same encoding in C++ and mixes it into the REAL PSP audio
# the core produces from a REAL battle savestate. What it proves: the encoding is audible
# against real game sound, at the rates and gains chosen, and the snapshot the adapter
# reports actually tracks a live lock. What it does NOT prove: that the Swift synth produces
# byte-identical output -- that needs the device.
#
# The encoding is copied from Sources/OpenGameAccess/CueSynth.swift; if the two drift, this
# file is the reference for what was auditioned.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG=${DISSIDIA_CSO:-$HOME/dissidia.cso}

mkdir -p "$HOME/cue-out"
cat > "$HOME/cue-out/cue-capture.cpp" <<'EOF'
// Real gameplay audio + the lock-on cue, mixed exactly as the Swift synth mixes it.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include "pokecore.h"
#include "adapter.h"

static const double kRate = 32768.0;

// ---- the cue voice, mirroring CueSynth.swift ----
struct Voice {
    double freq = 660, gain = 0, pulseHz = 0, duty = 0.5;
    double phase = 0, pulsePhase = 0;
    double render(double sr) {
        double env = 1.0;
        if (pulseHz > 0) {
            double period = sr / pulseHz;
            double pos = fmod(pulsePhase, period) / period;
            double ramp = 0.06;
            if (pos < duty) {
                double into = pos, out = duty - pos;
                env = (into / ramp < out / ramp ? into / ramp : out / ramp);
                if (env > 1.0) env = 1.0;
            } else env = 0.0;
            pulsePhase += 1; if (pulsePhase >= period) pulsePhase -= period;
        }
        double s = sin(phase * 2 * M_PI) * gain * env;
        phase += freq / sr; if (phase >= 1) phase -= 1;
        return s;
    }
};

static Voice g_beacon;
static const double ENEMY_HZ = 660, CORE_HZ = 1180;
static const double FAR = 45, NEAR = 4, SLOW = 1.6, FAST = 11;

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <image> <state> <out.wav> [seconds]\n", argv[0]); return 2; }
    const char* image = argv[1];
    const char* state = argv[2];
    const char* outwav = argv[3];
    double seconds = (argc > 4) ? atof(argv[4]) : 12.0;

    PokeCore* core = poke_create();
    if (!core) return 1;
    if (!poke_load_rom(core, image, ""$HOME/cue-out/save"")) return 1;
    if (!poke_start(core)) return 1;
    if (!poke_load_state(core, state)) { fprintf(stderr, "load state failed\n"); return 1; }
    poke_adapter_ready(core);
    (void) poke_command(core, (int)oga::Command::MenuState);

    std::vector<int16_t> pcm;
    const int kFrameSamples = 546;          // ~1 emulated frame at 60 Hz
    int frames = (int)(seconds * 60);
    std::vector<int16_t> buf(kFrameSamples * 2);
    int lastState = -1;
    double stateChangeAt = 0;

    for (int f = 0; f < frames; f++) {
        if (!poke_frame(core)) break;

        // ---- what the adapter reports, exactly as the app polls it ----
        float dist = 0;
        int st = poke_cue_snapshot(core, &dist);

        // ---- the encoding from CueSynth.updateBeacon ----
        if (st == 2 || st == 3) {
            g_beacon.freq = (st == 3) ? CORE_HZ : ENEMY_HZ;
            double d = dist < NEAR ? NEAR : (dist > FAR ? FAR : dist);
            double t = (d - NEAR) / (FAR - NEAR);
            g_beacon.pulseHz = FAST + (SLOW - FAST) * t;
            g_beacon.duty = 0.5;
            g_beacon.gain = 0.18 + 0.12 * (1 - t);
        } else {
            g_beacon.gain = 0;
        }
        if (st != lastState) {
            printf("t=%.2fs  state=%d dist=%.1f  pulse=%.2fHz gain=%.2f\n",
                   f / 60.0, st, dist, g_beacon.pulseHz, g_beacon.gain);
            lastState = st; stateChangeAt = f / 60.0;
        }
        (void) stateChangeAt;

        // ---- real game audio, then the cue mixed over it (same as the iOS render block) ----
        int got = poke_read_audio(core, buf.data(), kFrameSamples);
        for (int i = 0; i < got; i++) {
            double mix = g_beacon.render(kRate);
            double s = mix * 0.5;
            int add = (int)(s * 32767);
            int l = buf[i * 2] + add, r = buf[i * 2 + 1] + add;
            if (l > 32767) l = 32767; if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; if (r < -32768) r = -32768;
            buf[i * 2] = (int16_t)l; buf[i * 2 + 1] = (int16_t)r;
        }
        pcm.insert(pcm.end(), buf.begin(), buf.begin() + got * 2);
    }

    poke_stop(core);
    poke_destroy(core);

    FILE* fp = fopen(outwav, "wb");
    if (!fp) { fprintf(stderr, "cannot write %s\n", outwav); return 1; }
    uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
    uint32_t rate = (uint32_t)kRate;
    uint16_t ch = 2, bits = 16;
    uint32_t byteRate = rate * ch * bits / 8;
    uint16_t blockAlign = ch * bits / 8;
    fwrite("RIFF", 1, 4, fp); uint32_t riffSz = 36 + dataBytes;
    fwrite(&riffSz, 4, 1, fp); fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp); uint32_t fmtSz = 16; fwrite(&fmtSz, 4, 1, fp);
    uint16_t pcmFmt = 1; fwrite(&pcmFmt, 2, 1, fp);
    fwrite(&ch, 2, 1, fp); fwrite(&rate, 4, 1, fp);
    fwrite(&byteRate, 4, 1, fp); fwrite(&blockAlign, 2, 1, fp); fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp); fwrite(&dataBytes, 4, 1, fp);
    fwrite(pcm.data(), 2, pcm.size(), fp);
    fclose(fp);
    printf("wrote %s: %u frames, %.1f s\n", outwav, (unsigned)(pcm.size() / 2),
           (double)(pcm.size() / 2) / kRate);
    return 0;
}
EOF

g++ -O1 -g -std=c++17 -I Core -I Sources/CPokeCore/include \
  "$HOME/cue-out/cue-capture.cpp" -c -o "$OUT/cue-capture.o" || exit 1

rm -f "$OUT/libcue.a"
ar rcs "$OUT/libcue.a" "$ROOT"/Vendor/hostobj/*.o \
  "$OUT"/n64_adapter.o "$OUT"/nes_adapter.o \
  $(ls "$OUT"/host-obj/*.o | grep -v "proof-main.o") || exit 1

g++ -O1 -g "$OUT/cue-capture.o" "$OUT/libcue.a" -o "$OUT/cue-capture" \
  -lz -lpthread -ldl -lm \
  "$HOME"/ffmpeg-host/lib/libavformat.a "$HOME"/ffmpeg-host/lib/libavcodec.a \
  "$HOME"/ffmpeg-host/lib/libswresample.a "$HOME"/ffmpeg-host/lib/libswscale.a \
  "$HOME"/ffmpeg-host/lib/libavutil.a > "$OUT/cue-link.log" 2>&1 \
  || { echo "!! link failed"; tail -20 "$OUT/cue-link.log"; exit 1; }

export PPSSPP_ASSETS="$OUT/asset-subset"
"$OUT/cue-capture" "$IMG" "$1" "$2" "${3:-12}" 2>/dev/null | tail -n 20
