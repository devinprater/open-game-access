#!/usr/bin/env bash
# cue-field-capture.sh — render the FIELD-MODE beacon over real gameplay audio, to a WAV.
#
# WHAT THIS PROVES, AND WHAT IT CANNOT (the same disclaimer the lock cue's harness carries):
# the cue is synthesised in Swift on the device, so this harness re-implements the SAME encoding
# in C++ and mixes it into the REAL PSP audio the core produces, from a REAL savestate taken in the
# story field with a damaged city. What it proves: the encoding is audible against real game sound,
# at the rates and gains chosen, and the snapshot the adapter reports really tracks a live target.
# What it does NOT prove: that the Swift synth produces byte-identical output -- that needs a
# device. Sources/OpenGameAccess/CueSynth.swift is the reference; if the two drift, the Swift file
# is what shipped and this must be re-mirrored.
#
# ⛔ THIS IS THE FIELD PATH, NOT THE LOCK PATH. scripts/cue-capture.sh renders the BATTLE beacon
# from poke_cue_snapshot (state 2/3/4, centred, 660/1180 Hz). Field mode needs a BEARING and three
# TIMBRES, which is a different entry point (poke_cue_snapshot_field) and a different encoding
# (updateFieldBeacon). Reusing the lock harness would have produced a plausible-sounding file that
# proves nothing about the mode the player is actually in.
# ⛔ IT MIRRORS scripts/psp-host-proof.sh's BUILD. That script is the only path on this host that
# carries a REAL Core/psp_core.cpp: build-host.sh's object set satisfies psp_* with
# host_harness_stub.cpp, which ABORTS at runtime ("psp_create is not available in NDS harness
# builds"), so a hand-rolled link against it looks successful and dies on its first frame. The
# first version of this script did exactly that.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
OUT="${PPSSPP_PROOF_OUT:-$HOME/oga-ppsspp-proof}"
IMG="${DBZ_ISO:-$HOME/dbz-another-road.iso}"
STATE="${DBZ_STATE:-}"
SECONDS_="${1:-14}"

[ -f "$IMG" ] || { echo "FAIL: no game image at $IMG"; exit 1; }
# ⛔ THE SAVESTATE IS OPTIONAL AND OFF BY DEFAULT. A state is bound to the exact build that wrote
# it, and the one on this host came from a PPSSPP four months older, so the harness drives into the
# field from boot instead. Set DBZ_STATE only for a state THIS build wrote.
if [ -n "$STATE" ] && [ ! -f "$STATE" ]; then echo "FAIL: DBZ_STATE set but missing: $STATE"; exit 1; fi
[ -d "$PPSPP_SRC/Core" ] || { echo "FAIL: no PPSSPP tree at $PPSPP_SRC"; exit 1; }

CUE_DIR="$HOME/cue-field-out"; mkdir -p "$CUE_DIR"

cat > "$HOME/cue-field-out/cue-field-capture.cpp" <<'EOF'
// Real gameplay audio + the FIELD-MODE beacon, mixed exactly as CueSynth.render mixes it.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include "pokecore.h"
#include "adapter.h"

static const double kRate = 32768.0;

// ---- the cue voice, mirroring CueSynth.swift's CueVoice + render() ----
struct Voice {
    double freq = 900, gain = 0, pulseHz = 0, duty = 0.5, pan = 0;
    double phase = 0, pulsePhase = 0;
};

static Voice g_beacon;

// ---- the encoder, mirroring CueSynth.updateFieldBeacon ----
static const double FIELD_FAR = 2500, FIELD_NEAR = 60;
static const double FIELD_SLOW = 1.2, FIELD_FAST = 9;
static const double FIELD_ENEMY_HZ = 520, FIELD_CITY_HZ = 780, FIELD_ALLY_HZ = 1120;

static double normalizedBearing(double deg) {
    double d = deg;
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    return d;
}

// Returns the pan it applied, for the log.
static double updateFieldBeacon(int kind, double bearingDeg, double distance, bool headingLive) {
    if (kind == 0) { g_beacon.gain = 0; g_beacon.pan = 0; return 0; }
    switch (kind) {
        case 1:  g_beacon.freq = FIELD_ENEMY_HZ; break;
        case 2:  g_beacon.freq = FIELD_CITY_HZ;  break;
        default: g_beacon.freq = FIELD_ALLY_HZ;  break;
    }
    double bearing = headingLive ? bearingDeg : 180.0;
    double radians = bearing * M_PI / 180.0;
    g_beacon.pan = sin(radians);

    double d = distance < FIELD_NEAR ? FIELD_NEAR : (distance > FIELD_FAR ? FIELD_FAR : distance);
    double t = (d - FIELD_NEAR) / (FIELD_FAR - FIELD_NEAR);
    // BOTH ARMS ANNOTATED in Swift; here they are doubles, so the same arithmetic.
    double behind = (fabs(normalizedBearing(bearingDeg)) > 90.0) ? 0.35 : 1.0;
    g_beacon.pulseHz = (FIELD_FAST + (FIELD_SLOW - FIELD_FAST) * t) * behind;
    g_beacon.duty = 0.5;
    g_beacon.gain = 0.18 + 0.12 * (1 - t);
    return g_beacon.pan;
}

static std::vector<std::string> g_spoken;
static void onSpeech(const char* text, bool, void*) {
    if (text && *text) { g_spoken.push_back(std::string(text)); printf("SPEAK: %s\n", text); }
}

// ---- reach the FIELD from boot, with NO savestate -------------------------------------------
// ⛔ A SAVESTATE IS BOUND TO THE EXACT BUILD THAT WROTE IT. The first version of this harness took
// a state written by a desktop PPSSPP four months older than this tree and died on a marker
// mismatch deep in the stream -- which reads like a corrupt file, and the emulator then sits on a
// state that never loaded while burning almost no CPU. So drive the game there instead.
// Input is scripted in EMULATED FRAMES, never wall-clock holds: a hold that begins and ends
// between two frames never exists as far as the console is concerned.
static long g_frames = 0;
static unsigned long g_lastDumpHash = 0;

// Cheap fingerprint of the visible frame. Used to answer the only question that matters when a
// scripted press does nothing: did the screen change at all?
static unsigned long frameHash(PokeCore* core) {
    int w = 0, h = 0;
    if (!poke_framebuffer(core, POKE_SCREEN_TOP, &w, &h) || w <= 0 || h <= 0) return 0;
    const uint8_t* px = poke_framebuffer_ptr(core, POKE_SCREEN_TOP);
    if (!px) return 0;
    unsigned long hsh = 1469598103934665603UL;
    long n = (long) w * h * 4;
    for (long i = 0; i < n; i += 331) { hsh ^= px[i]; hsh *= 1099511628211UL; }
    return hsh;
}

static const char* kBtnName[POKE_BTN_COUNT] = {
    "A/cross", "B/circle", "Select", "Start", "Right", "Left",
    "Up", "Down", "R", "L", "X/triangle", "Y/square"
};

// Dump the top screen as a PPM. Cheap, and it is the only instrument that works before the reader
// has anything to say -- outside the field the DBZ reader answers nothing at all, so a speech probe
// reports "(nothing)" on every screen and cannot tell a menu from a hang.
// ⛔ The FILL call must come first: psp_framebuffer stages the pixels and the pointer accessor
// returns them, so calling the pointer first returns null and this writes zero files.
static void dumpFrame(PokeCore* core, const char* dir, long idx) {
    int w = 0, h = 0;
    if (!poke_framebuffer(core, POKE_SCREEN_TOP, &w, &h) || w <= 0 || h <= 0) return;
    const uint8_t* px = poke_framebuffer_ptr(core, POKE_SCREEN_TOP);
    if (!px) { printf("  dump %ld: fill ok but no pixels\n", idx); return; }

    char path[512];
    snprintf(path, sizeof path, "%s/f%05ld.ppm", dir, idx);
    FILE* f = fopen(path, "wb");
    if (!f) { printf("  dump %ld: cannot write %s\n", idx, path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row((size_t) w * 3);
    long nonblack = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t* s = px + (size_t) y * w * 4;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = s[x * 4 + 0];
            row[x * 3 + 1] = s[x * 4 + 1];
            row[x * 3 + 2] = s[x * 4 + 2];
        }
        fwrite(row.data(), 1, row.size(), f);
        for (int x = 0; x < w; x++) {
            const uint8_t* q = s + x * 4;
            if (q[0] > 24 || q[1] > 24 || q[2] > 24) nonblack++;
        }
    }
    fclose(f);
    printf("  dumped %s (%dx%d, %ld lit px)\n", path, w, h, nonblack);
    fflush(stdout);
}

static bool stepToField(PokeCore* core) {
    const char* frameDir = getenv("DBZ_FRAMES");
    auto run = [&](int frames) {
        for (int i = 0; i < frames; i++) {
            if (!poke_frame(core)) return false;
            // ⛔ LOG PROGRESS AND FLUSH. A drive whose output is only visible at exit cannot be
            // told apart from a hang, and this core paces frames from its own thread, so wall time
            // is not frames. Every line here is evidence of where the run actually is.
            if (++g_frames % 600 == 0) {
                printf("  frame %ld  adapter_ready=%d\n", g_frames,
                       poke_adapter_ready(core) ? 1 : 0);
                fflush(stdout);
                if (frameDir && *frameDir) {
                    // Dump only when the screen actually CHANGED, so the image set is a record of
                    // screens rather than of time -- 26 near-identical stills told us nothing.
                    unsigned long h = frameHash(core);
                    if (h != g_lastDumpHash) { g_lastDumpHash = h; dumpFrame(core, frameDir, g_frames); }
                }
            }
        }
        return true;
    };
    auto tap = [&](int btn, int hold, int settle) {
        poke_tap_button(core, btn, hold < 2 ? 2 : hold);
        return run(settle);
    };

    // ⛔ KNOW WHAT THE GAME SHOWS ON ITS OWN BEFORE PRESSING ANYTHING. Every sequence so far was a
    // guess, and 24 START taps at 3 s apart is 75 s of input -- enough to skip straight through the
    // title AND a menu into whatever is behind it. This mode presses NOTHING and records each
    // screen change, which is the foundation the drive was missing.
    if (getenv("DBZ_IDLE")) {
        printf("idle probe: no input at all, watching the boot sequence\n");
        fflush(stdout);
        if (!run(9000)) return false;
        return poke_adapter_ready(core);
    }

    // ⛔ MEASURE EACH BUTTON AGAINST A NO-PRESS CONTROL. The game animates by itself, so "the screen
    // changed after my press" is ALWAYS true and proves nothing -- that is why an earlier sweep
    // could never fire. What distinguishes a button that does something is leaving the game's own
    // loop: a running attract sequence keeps producing NEW images forever, while a screen that
    // accepts input settles. Count DISTINCT screens over a long window, one button per window.
    if (getenv("DBZ_PROBE")) {
        printf("probe: settling with no input, then distinct screens per button\n");
        fflush(stdout);
        if (!run(9000)) return false;      // let the game reach whatever it does on its own
        for (int b = 0; b < POKE_BTN_COUNT; b++) {
            unsigned long seen[64];
            int nseen = 0;
            poke_tap_button(core, b, 8);
            for (int k = 0; k < 3600; k++) {
                if (!poke_frame(core)) return false;
                if (k % 200 == 0) {
                    unsigned long h = frameHash(core);
                    bool dup = false;
                    for (int j = 0; j < nseen; j++) if (seen[j] == h) dup = true;
                    if (!dup && nseen < 64) seen[nseen++] = h;
                }
            }
            printf("  %-12s %d distinct\n", kBtnName[b], nseen);
            fflush(stdout);
        }
        return poke_adapter_ready(core);
    }

    // ⛔ USE THE SEQUENCE THAT WORKED, IN ITS ORIGINAL ORDER. The proven drive opens with CROSS at
    // the title and nothing before it. The 24 START taps that used to sit here are NOT in it: 75 s
    // of START skips the title AND the menu behind it, which is how a blind drive ends up somewhere
    // the game never intended to go.
    // The title answers CROSS.
    if (!tap(POKE_BTN_A, 8, 210)) return false;                       // ~3.5 s
    // ⛔ STORY MODE IS THE FIRST ROW AND THE CURSOR DOES NOT START THERE -- a blind CROSS lands in
    // ARCADE (a battle) instead. Press UP past the top until the list clamps at row 0; that needs
    // no RAM read and cannot overshoot.
    for (int i = 0; i < 10; i++) { if (!tap(POKE_BTN_UP, 6, 42)) return false; }
    if (!tap(POKE_BTN_A, 8, 240)) return false;                       // ~4 s

    // The story chain (cutscene -> character select -> Team Edit -> field) swallows presses; an
    // event holding input needs START then CROSS to hand control back. Retry against the reader's
    // OWN field gate rather than a press count: poke_adapter_ready is false outside the field.
    for (int n = 0; n < 40; n++) {
        if (poke_adapter_ready(core)) return true;
        if (n % 5 == 4) { if (!tap(POKE_BTN_START, 8, 96)) return false; }
        if (!tap(POKE_BTN_A, 8, 156)) return false;                   // ~2.6 s
    }

    // ⛔ A DRIVE THAT PRESSES ONE BUTTON THROUGH A FROZEN SCREEN IS NOT DRIVING. Measured on this
    // game: the screen animates to about frame 5400, then holds bit-identical for 100+ seconds
    // while CROSS and START keep firing -- a still frame that ignores your button is waiting on a
    // DIFFERENT button, and no repetition count fixes that. So when CROSS changes nothing, sweep
    // every button and press the one that moves the screen. The reader's own field gate, not a
    // press count, decides when we have arrived.
    if (!run(120)) return false;
    while (g_frames < 140000) {
        if (poke_adapter_ready(core)) return true;
        unsigned long h0 = frameHash(core);
        poke_tap_button(core, POKE_BTN_A, 8);
        if (!run(200)) return false;
        if (frameHash(core) != h0) continue;                  // CROSS did something; keep going

        printf("  [frame %ld] CROSS did nothing -- sweeping\n", g_frames);
        fflush(stdout);
        for (int b = 0; b < POKE_BTN_COUNT && g_frames < 140000; b++) {
            if (b == POKE_BTN_A) continue;
            if (poke_adapter_ready(core)) return true;
            unsigned long a0 = frameHash(core);
            poke_tap_button(core, b, 8);
            if (!run(140)) return false;
            if (frameHash(core) != a0) {
                printf("  [frame %ld] %s MOVED the screen\n", g_frames, kBtnName[b]);
                fflush(stdout);
                break;
            }
        }
    }
    return poke_adapter_ready(core);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <image> <out.wav> [seconds]\n"
                        "  DBZ_STATE=path  load a savestate instead of driving from boot\n"
                        "  DBZ_SAVE=path   write a state once the field is reached\n", argv[0]);
        return 2;
    }
    const char* image = argv[1];
    const char* outwav = argv[2];
    double seconds = (argc > 3) ? atof(argv[3]) : 14.0;
    const char* state = getenv("DBZ_STATE");
    const char* saveTo = getenv("DBZ_SAVE");

    PokeCore* core = poke_create();
    if (!core) { fprintf(stderr, "poke_create failed\n"); return 1; }
    if (!poke_load_rom(core, image, "")) { fprintf(stderr, "load_rom failed\n"); return 1; }
    poke_set_audio_enabled(core, true);
    poke_set_speech_callback(core, onSpeech, nullptr);
    printf("assets: %s\n", getenv("PPSSPP_ASSETS") ? getenv("PPSSPP_ASSETS") : "(default)");
    if (!poke_start(core)) { fprintf(stderr, "start failed\n"); return 1; }
    printf("game code: %s   adapter: %s\n",
           poke_game_code(core) ? poke_game_code(core) : "?",
           poke_adapter_id(core) ? poke_adapter_id(core) : "?");

    if (state && *state) {
        if (!poke_load_state(core, state)) {
            fprintf(stderr, "LOAD STATE FAILED (%s): %s\n", state,
                    poke_last_error(core) ? poke_last_error(core) : "?");
            return 1;
        }
        printf("loaded state %s\n", state);
    } else {
        printf("no DBZ_STATE set -- driving into the field from boot\n");
        if (!stepToField(core)) {
            fprintf(stderr, "never reached the field (the reader's gate stayed closed)\n");
            return 1;
        }
        printf("FIELD REACHED\n");
    }
    // ⛔ A LOADED STATE IS NOT AN EVALUATED ONE. The adapter answers Ready() from RAM it reads
    // while frames run, so asking straight after poke_load_state reports "adapter not ready" on a
    // state that was written at FIELD LIVE -- measured on the first state this build ever wrote
    // (arMode set, 3 cities). Give it frames before believing the answer.
    if (!poke_adapter_ready(core)) {
        for (int i = 0; i < 10 && !poke_adapter_ready(core); i++) poke_frame(core);
    }
    if (!poke_adapter_ready(core)) {
        // ⛔ ASK THE ADAPTER, DO NOT GUESS. The state is proven good at the PSP level, so print what
        // the ADAPTER reads on this very core: its own dump command, and the field snapshot's code.
        int k0 = -1, live0 = -1; float b0 = -1, d0 = -1;
        int rc = poke_cue_snapshot_field(core, &k0, &b0, &d0, &live0);
        printf("diagnostic: cue_snapshot rc=%d kind=%d bearing=%.1f dist=%.1f live=%d\n",
               rc, k0, b0, d0, live0);
        printf("diagnostic: DumpState command returned %d\n",
               (int) poke_command(core, (int) oga::Command::DumpState));
        fflush(stdout);
        fprintf(stderr, "adapter not ready\n");
        return 1;
    }
    printf("adapter ready: %s\n", poke_adapter_id(core) ? poke_adapter_id(core) : "?");

    // Optional: one PPM of the field this render is taken from. Same rule as everywhere else here --
    // the FILL call first, the pointer second, and a picture is what settles a claim about a screen.
    {
        const char *shot = getenv("DBZ_SHOT");
        if (shot && *shot) {
            int sw = 0, sh = 0;
            if (poke_framebuffer(core, POKE_SCREEN_TOP, &sw, &sh) && sw > 0) {
                const uint8_t *px = poke_framebuffer_ptr(core, POKE_SCREEN_TOP);
                FILE *sf = px ? fopen(shot, "wb") : nullptr;
                if (sf) {
                    fprintf(sf, "P6\n%d %d\n255\n", sw, sh);
                    for (int y = 0; y < sh; y++) {
                        const uint8_t *row = px + (size_t) y * sw * 4;
                        for (int x = 0; x < sw; x++) {
                            uint8_t rgb[3] = {row[x * 4 + 0], row[x * 4 + 1], row[x * 4 + 2]};
                            fwrite(rgb, 1, 3, sf);
                        }
                    }
                    fclose(sf);
                    printf("wrote frame %s (%dx%d)\n", shot, sw, sh);
                } else {
                    printf("no frame to dump (%s)\n", shot);
                }
            }
        }
    }
    if (saveTo && *saveTo) {
        printf("saved state: %s\n", poke_save_state(core, saveTo) ? "yes" : "no");
    }

    // ---- what does the reader say about this state? ask it, then let the press settle ----
    (void) poke_command(core, (int)oga::Command::WhereAmI);
    for (int i = 0; i < 30; i++) poke_frame(core);
    (void) poke_command(core, (int)oga::Command::NextEnemy);   // the city list
    for (int i = 0; i < 30; i++) poke_frame(core);

    // ⛔ THE RING MUST BE POINTED AT SOMETHING, OR THE CUE IS SILENT BY DESIGN. CueSnapshotFill
    // returns false when the selected kind has no members AND no enemy can be found -- measured on
    // the field moment this harness renders: three cities, no enemy (team >= 2), so the first render
    // came out with 0 cued frames and proved nothing about the cue. Stepping the ring is what the
    // player's own prev/next presses do in the app, so do that here and say which step engaged.
    {
        int engagedAt = -1;
        for (int step = 0; step <= 4 && engagedAt < 0; step++) {
            int k = 0, lv = 0; float b = 0, d = 0;
            int rc = poke_cue_snapshot_field(core, &k, &b, &d, &lv);
            printf("ring step %d: snapshot rc=%d kind=%d bearing=%.1f dist=%.1f live=%d\n",
                   step, rc, k, b, d, lv);
            if (rc != 0 && k != 0) { engagedAt = step; break; }
            (void) poke_command(core, (int) oga::Command::MenuNext);
            for (int i = 0; i < 10; i++) poke_frame(core);
        }
        printf("ring engaged at step %d%s\n", engagedAt,
               engagedAt < 0 ? "  (NOTHING TO CUE -- the render below is silence, by design)" : "");
        fflush(stdout);
    }
    printf("spoken so far: %d lines\n", (int) g_spoken.size());

    std::vector<int16_t> pcm;
    const int kFrameSamples = 546;
    int frames = (int)(seconds * 60);
    std::vector<int16_t> buf(kFrameSamples * 2);
    int lastKind = -999;
    long peaks = 0;                 // envelope rises, for the rate check
    double g_env = 0.0;             // one-pole envelope, see the counter below
    const double kEnvAlpha25Hz = 1.0 - exp(-2.0 * M_PI * 25.0 / kRate);
    int lastLoud = 0;
    double sumPan = 0; int nPan = 0;
    double firstFreq = 0, lastFreq = 0;

    for (int f = 0; f < frames; f++) {
        // ⛔ THE CUE MUST BE DRIVEN, NOT JUST PLAYED. A held-still capture is one steady pitch and
        // proves nothing about steering. Fly a slow circle on the analog stick so the bearing to
        // the target sweeps and the pulse rate changes as the distance closes and opens.
        double ang = 2.0 * M_PI * (double) f / (60.0 * 6.0);   // one full turn every 6 s
        poke_set_analog(core, (float) cos(ang), (float) sin(ang));
        if (!poke_frame(core)) break;

        int kind = 0, live = 0;
        float bearing = 0, dist = 0;
        int rc = poke_cue_snapshot_field(core, &kind, &bearing, &dist, &live);
        double pan = (rc != 0) ? updateFieldBeacon(kind, bearing, dist, live != 0) : 0.0;
        if (rc == 0) updateFieldBeacon(0, 0, 0, false);

        if (rc != 0) { sumPan += pan; nPan++; if (!firstFreq) firstFreq = g_beacon.freq; lastFreq = g_beacon.freq; }

        if (kind != lastKind) {
            printf("t=%.2fs  kind=%d bearing=%.1f dist=%.1f live=%d -> %.0f Hz pan=%+.2f pulse=%.2fHz gain=%.2f\n",
                   f / 60.0, kind, bearing, dist, live, g_beacon.freq, g_beacon.pan, g_beacon.pulseHz, g_beacon.gain);
            lastKind = kind;
        }

        int got = poke_read_audio(core, buf.data(), kFrameSamples);
        for (int i = 0; i < got; i++) {
            Voice& v = g_beacon;
            // ---- pulse envelope: raised-cosine ramp, exactly as the Swift render block ----
            double env = 1.0;
            if (v.pulseHz > 0) {
                double period = kRate / v.pulseHz;
                double pos = fmod(v.pulsePhase, period) / period;
                double duty = v.duty < 0.05 ? 0.05 : (v.duty > 1.0 ? 1.0 : v.duty);
                double ramp = 0.06;
                if (pos < duty) {
                    double into = pos, out = duty - pos;
                    env = into / ramp < out / ramp ? into / ramp : out / ramp;
                    if (env > 1.0) env = 1.0;
                } else env = 0.0;
                v.pulsePhase += 1;
                if (v.pulsePhase >= period) v.pulsePhase -= period;
            }
            // ---- equal-power pan, NOT a linear crossfade ----
            double pan2 = v.pan < -1 ? -1 : (v.pan > 1 ? 1 : v.pan);
            double angle = (pan2 + 1) * M_PI / 4;
            double gl = cos(angle), gr = sin(angle);

            double sample = sin(v.phase * 2 * M_PI) * v.gain * env;
            double mixL = sample * gl, mixR = sample * gr;
            v.phase += v.freq / kRate;
            if (v.phase >= 1) v.phase -= 1;

            // 0.3535534 == 0.5 * 0.7071068, the equal-power centre gain with headroom.
            double k = 0.3535534;
            int addL = (int)((mixL * k) * 32767);
            int addR = (int)((mixR * k) * 32767);
            int l = buf[i * 2] + addL, r = buf[i * 2 + 1] + addR;
            if (l > 32767) l = 32767; if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; if (r < -32768) r = -32768;
            buf[i * 2] = (int16_t)l; buf[i * 2 + 1] = (int16_t)r;

            // ⛔ SMOOTH BEFORE COUNTING, OR YOU COUNT THE CARRIER. A threshold on the raw signal
            // is crossed twice per cycle: the first render reported 143.6 "pulses" per second for a
            // cue whose rate is 1.2-9 Hz, which is the 780 Hz city timbre being counted. One-pole
            // envelope at ~25 Hz leaves only the pulse shape, and the count is then the pulse rate.
            double amp = fabs(mixL) + fabs(mixR);
            g_env += (amp - g_env) * kEnvAlpha25Hz;
            int loud = g_env > 0.05 ? 1 : 0;
            if (loud && !lastLoud) peaks++;
            lastLoud = loud;
        }
        pcm.insert(pcm.end(), buf.begin(), buf.begin() + got * 2);
    }

    printf("\nkind seen: %d   freq walked %.0f -> %.0f Hz\n", lastKind, firstFreq, lastFreq);
    printf("mean pan over %d cued frames: %+.3f\n", nPan, nPan ? sumPan / nPan : 0.0);
    printf("cue pulse rises counted: %ld over %.1f s  (%.2f per second)\n",
           peaks, (double)(pcm.size() / 2) / kRate, peaks / ((double)(pcm.size() / 2) / kRate));

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

# ---- the PPSSPP-core subset, exactly as scripts/psp-host-proof.sh builds it ----
GEN="$OUT/host-gen"; mkdir -p "$GEN" "$OUT/host-obj" "$OUT/host-save"
FFMPEG_HOST="${FFMPEG_HOST:-$HOME/ffmpeg-host}"
if [ ! -f "$GEN/miniupnpcstrings.h" ] || [ "$PPSPP_SRC/ext/miniupnp/miniupnpc/VERSION" -nt "$GEN/miniupnpcstrings.h" ]; then
  ( cd "$PPSPP_SRC/ext/miniupnp/miniupnpc" && sh updateminiupnpcstrings.sh "$GEN/miniupnpcstrings.h" miniupnpcstrings.h.in ) > /dev/null
fi
INC="$(ppspp_inc "$PPSPP_SRC") -I$GEN"
# ⛔ THE READER-CORE HEADERS NEED THEIR OWN ROOT. pokecore.cpp includes "pokecore.h", which lives in
# Sources/CPokeCore/include -- NOT beside Core/. The PPSSPP include set does not carry it, so the
# reader core fails with a missing header that reads like a broken source. Add the root; a whole
# translation unit failing on the FIRST include is the tell for a missing root rather than bad code.
READER_INC="-I$ROOT/Sources/CPokeCore/include"
# ⛔ AND pokecore.cpp ALSO pulls the melonDS roots (it includes NDS.h, and that tree's own headers
# beside it). build-host.sh's INC is the authority for how this TU compiles: Core, CPokeCore,
# melonds SRC/src, lua, and teakra. Without the melonDS roots a whole file dies on its SECOND
# include, which reads like a broken source rather than a missing include root.
NDS_SRC="${MELONDS_SRC:-$HOME/src/melonds-lua}"
LUA_SRC="${LUA_SRC:-$HOME/src/lua-5.4.7}"
READER_INC="$READER_INC -I$NDS_SRC/src -I$LUA_SRC/src -I$NDS_SRC/src/teakra/include"
CXX="g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -std=c++17 $INC $READER_INC -I$ROOT/Core -DMOBILE_DEVICE -DUSE_FFMPEG -I$FFMPEG_HOST/include"
CC="gcc -O1 -g -fPIC -fwrapv -fno-strict-aliasing $INC $READER_INC -DMOBILE_DEVICE -DLUA_USE_IOS -DUSE_FFMPEG -I$FFMPEG_HOST/include"

echo "== compiling the PPSSPP-core subset (cached; psp-host-proof.sh's own recipe)"
n=0; fail=0
for f in $PPSPP_CORE; do
  o="$OUT/host-obj/c_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { $CXX -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_EXT_CPP; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.cpp$//').o"
  [ -f "$o" ] || { $CXX -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_EXT_C; do
  o="$OUT/host-obj/e_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
  [ -f "$o" ] || { $CC -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
done
for f in $PPSPP_LUA; do
  o="$OUT/host-obj/l_$(basename "$f" .c).o"
  [ -f "$o" ] || { $CC -c "$PPSPP_SRC/ext/lua/$f" -o "$o" || fail=1; n=$((n+1)); }
done
case "$(uname -m)" in
  x86_64)
    for f in $PPSPP_X86; do
      o="$OUT/host-obj/x_$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o"
      [ -f "$o" ] || { $CC -DSTACK_LINE_READER_BUFFER_SIZE=1024 -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
    done
    for f in $PPSPP_X86_ASM; do
      o="$OUT/host-obj/x_$(basename "$f" .S).o"
      [ -f "$o" ] || { $CC -c "$PPSPP_SRC/$f" -o "$o" || fail=1; n=$((n+1)); }
    done ;;
esac
# Glue, the adapter, and the reader core this tree is: always rebuilt, so the artifact is this source.
for f in $PPSPP_GLUE; do
  o="$OUT/host-obj/g_$(basename "$f" .cpp).o"
  rm -f "$o"; $CXX -c "$ROOT/Core/$f" -o "$o" || fail=1; n=$((n+1))
done
for f in dbzar_adapter.cpp adapters.cpp announce.cpp pokecore.cpp; do
  o="$OUT/host-obj/g_$(basename "$f" .cpp).o"
  rm -f "$o"; $CXX -c "$ROOT/Core/$f" -o "$o" || fail=1; n=$((n+1))
done
[ "$fail" -eq 0 ] || { echo "!! compile errors"; exit 1; }
echo "== compiled $n TUs this pass (the rest cached)"

echo "== compiling the capture"
$CXX -c "$CUE_DIR/cue-field-capture.cpp" -o "$CUE_DIR/cue-field-capture.o" \
  || { echo "!! capture compile failed"; exit 1; }

echo "== linking"
# ⛔ THE APP'S HOST OBJECT SET IS THE SOURCE OF EVERYTHING melonDS NEEDS, and the melonDS
# objects are NOT SELECTABLE BY SYMBOL. pokecore.cpp is also the NDS reader core, so the link
# needs melonDS's own objects -- but melonDS's transitive deps (FatFs `f_open`/`f_mount`, SHA1,
# AES_CTR_xcrypt_buffer) define NO `melonDS::` symbol at all, so a symbol-based selector keeps
# missing them and the link walks from one undefined-reference wall to the next. A census of the
# union link settled it: with the WHOLE app set present there are ZERO undefined references. Take
# the whole set minus the six the harness owns itself, and solve the collisions explicitly (below).
NDS_OBJS=""
for o in "$ROOT"/Vendor/hostobj/*.o; do
  b="$(basename "$o")"
  case "$b" in
    # Rebuilt by this harness: pokecore.cpp (reader core under test), announce.cpp,
    # adapters.cpp, dbzar_adapter.cpp (the adapter under test).
    pokecore.o|announce.o|adapters.o|dbzar_adapter.o) continue ;;
    # psp_core.cpp: PPSPP_GLUE builds it into host-obj as the REAL PSP core.
    # host_harness_stub.cpp: the aborting stub this harness exists to avoid.
    psp_core.o|host_harness_stub.o) continue ;;
  esac
  NDS_OBJS="$NDS_OBJS $o"
done
echo "== app host objects taken: $(echo $NDS_OBJS | wc -w)"
# ⛔ TWO COMPLETE LUA IMPLEMENTATIONS AND TWO LZMA IMPLEMENTATIONS END UP IN ONE BINARY HERE.
# psp_core.cpp is compiled against PPSSPP's bundled Lua fork (`l_*.o`) while melonDS's LuaInterface
# and mGBA link lua-5.4.7 (`lua_*.o`); likewise PPSSPP's ext/lzma-sdk collides with mGBA's vendored
# lzma, and PPSSPP's xbrz with the app's. `--allow-multiple-definition` resolves each to the first
# object seen (host-obj is listed first, so PPSSPP's own copies win for the PSP core). This is a
# HARNESS-ONLY CRUTCH, and it is honest to say what it costs: the shipping app links exactly ONE
# Lua, so this harness proves the CUE IS AUDIBLE over real gameplay -- it does NOT prove the app's
# own link. Do not carry this flag into the app. See DUPLICATE_SYMBOLS.md.
DUPLICATE_OK="-Wl,--allow-multiple-definition"
# ⛔ ONLY ONE main() MAY EXIST in the shared object dir, and a stale capture-main.o from a past
# experiment once got picked up by a wildcard link and failed on symbols the target never uses.
rm -f "$OUT/host-obj/proof-main.o" "$OUT/host-obj/capture-main.o"
g++ -O1 -g $DUPLICATE_OK "$CUE_DIR/cue-field-capture.o" "$OUT"/host-obj/*.o $NDS_OBJS \
  -o "$CUE_DIR/cue-field-capture" \
  -lz -lpthread -ldl -lm \
  "$FFMPEG_HOST"/lib/libavformat.a "$FFMPEG_HOST"/lib/libavcodec.a \
  "$FFMPEG_HOST"/lib/libswresample.a "$FFMPEG_HOST"/lib/libswscale.a \
  "$FFMPEG_HOST"/lib/libavutil.a > "$CUE_DIR/link.log" 2>&1 \
  || { echo "!! link failed"; grep -E "error|undefined reference|multiple definition" "$CUE_DIR/link.log" | head -15; exit 1; }

echo "== rendering"
export PPSSPP_ASSETS="$ROOT/build-assets/ppsspp-assets"
[ -d "$PPSSPP_ASSETS" ] || export PPSSPP_ASSETS="$OUT/asset-subset"
DBZ_STATE="$STATE" DBZ_SAVE="${DBZ_SAVE:-}" \
  "$CUE_DIR/cue-field-capture" "$IMG" "$CUE_DIR/field-cue.wav" "$SECONDS_" \
  > "$CUE_DIR/render.log" 2>&1
echo "render exit: $?"
# ⛔ SHOW THE LOG, DO NOT PIPE THE BINARY THROUGH tail. A pipe buffers the child's stdout, so a run
# that is slowly progressing and a run that is deadlocked look identical: both show nothing.
echo "--- harness output ---"
tail -60 "$CUE_DIR/render.log"
echo "--- field reached? ---"
grep -nE "FIELD REACHED|never reached|saved state|wrote " "$CUE_DIR/render.log" | head
echo
ls -la "$CUE_DIR/field-cue.wav" 2>/dev/null || echo "NO WAV"
