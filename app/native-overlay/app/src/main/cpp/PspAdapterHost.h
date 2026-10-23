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

#endif  // OGA_PSP_ADAPTER_HOST_H
