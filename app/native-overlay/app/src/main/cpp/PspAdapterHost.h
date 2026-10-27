// PspAdapterHost.h — the Android PSP adapter host (stage 4).
//
// The seam between the PSP session (PspSessionJNI.cpp) and the native
// game adapters (Core/dissidia_adapter.cpp, Core/dbzar_adapter.cpp,
// Core/mku_adapter.cpp, Core/khbbs_adapter.cpp). It is the PSP slice of
// what Core/pokecore.cpp does on iOS — selection by DISC_ID, a lazy
// attach, a per-frame pump, a command entry point, an announcement
// queue — and nothing else: pokecore itself is NOT ported, because the
// adapters are self-contained (they speak to the world only through
// oga::Host in Core/adapter.h) and the PSP memory path is one function
// (psp_debug_read in Core/psp_core.h).
//
// The session JNI owns the PspCore* and calls these hooks at its own
// lifecycle points: OnLoad after a successful psp_load_rom, OnStop
// before the core is destroyed, Frame after every successful psp_frame.
// Ready/Command are the UI's path in (PspGameSession polls Ready once a
// frame and sends commands from its reader buttons, mirroring how iOS's
// GameSession polls poke_adapter_ready and calls poke_command).
#ifndef OGA_PSP_ADAPTER_HOST_H
#define OGA_PSP_ADAPTER_HOST_H

#include <jni.h>

#include "psp_core.h"

// Install (or, with null, clear) the speech bridge: the app's single
// AccessibilitySpeech instance, the same object the GB host registers
// through GbAccessibilityScript.setGbSpeechBridge. Adapter speech —
// the announcement queue's sink and the adapters' direct wire alike —
// is forwarded to its speak(String, boolean) / stop() methods.
void PspAdapterHostSetSpeechBridge(JNIEnv* env, jobject bridge);

// Select the adapter for a freshly loaded game (full-string DISC_ID
// match — see the .cpp for why the registry itself is not linked) and
// build the host + announcement queue. A game with no adapter is the
// normal no-reader case: selection is empty and every other hook here
// answers accordingly. Also installs the core's speech callback.
void PspAdapterHostOnLoad(PspCore* core, const char* game_code);

// Detach the adapter (if attached) and tear the host down. Called
// before the core is destroyed, mirroring poke_destroy's order.
void PspAdapterHostOnStop(void);

// Per-frame pump for the session's core: stamp the adapter clock, run
// the adapter's on_frame hook, tick the announcement queue. A core
// that is not the session's (the stage-2 proof's) is ignored.
void PspAdapterHostFrame(PspCore* core);

// poke_adapter_ready's mirror: attach lazily on first call, then
// report the adapter's ready(). False when no adapter is selected.
bool PspAdapterHostReady(void);

// poke_command's mirror, including its core-internal intercepts
// (StopSpeech, the spoken-history walk) and its refusal rules. False
// means refused — no adapter, out of range, or not ready — and the
// caller (not this function) decides what a refusal says.
bool PspAdapterHostCommand(int cmd);

// The selected adapter's registry id / display name, or "" when no
// adapter is selected for the loaded game.
const char* PspAdapterHostId(void);
const char* PspAdapterHostName(void);

// The cue poll: ONE cue_snapshot for the frame, answering all four of
// pokecore's cue questions from it (poke_cue_snapshot's beacon state,
// poke_cue_snapshot_field's free-flight bearing, poke_cue_health's
// one-shot bitmask, poke_cue_guard's continuous window) — see
// PspAdapterHost.cpp for why the answers must come from a single
// snapshot. False when there is nothing to cue
// (no adapter, not attached yet, or the adapter refused the snapshot);
// every out is zeroed first either way, so a false answer is silence,
// never stale data. Any out-pointer may be null. Never attaches: like
// pokecore's calls, the cue starts once a Ready/Command call has
// attached the reader.
bool PspAdapterHostCuePoll(int* state, float* dist, int* field,
                           int* kind, float* bearing, float* field_dist,
                           int* heading_live, int* health_bits,
                           float* foe_hz, float* foe_pan, float* own_hz,
                           float* own_pan, int* guard_active,
                           float* guard_frac, int* guard_beat);

// Push the player's HUD gauge-cue choice into the Birth by Sleep
// adapter (oga::KhbbsHudGaugeCues in Core/adapter.h): true = gauge-full
// moments sound as earcons, false = the adapter speaks its lines
// instead. The session resolves the effective value (the game's Per
// Game Settings override, else the global default) and calls this once
// when a session starts — settings apply at start
// (docs/design/per-game-settings.md). Only that adapter reads the flag.
void PspAdapterHostSetHudGaugeCues(bool use_sound);

#endif  // OGA_PSP_ADAPTER_HOST_H
