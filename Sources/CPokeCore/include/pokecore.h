/*
 * pokecore.h — C ABI between the SwiftUI app and the emulator core.
 *
 * The core itself is melonDS (with the Lua accessibility layer) compiled as a
 * static library; nothing else in the app includes C++ headers. Every entry
 * point here is called from Swift.
 */
#ifndef POKECORE_H
#define POKECORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DS button ids — order matches melonDS's own key order. Macros rather than an
 * enum: Swift imports these as plain Int32 constants with no prefix stripping,
 * which is exactly what the button table wants. */
#define POKE_BTN_A      0
#define POKE_BTN_B      1
#define POKE_BTN_SELECT 2
#define POKE_BTN_START  3
#define POKE_BTN_RIGHT  4
#define POKE_BTN_LEFT   5
#define POKE_BTN_UP     6
#define POKE_BTN_DOWN   7
#define POKE_BTN_R      8
#define POKE_BTN_L      9
#define POKE_BTN_X      10
#define POKE_BTN_Y      11
#define POKE_BTN_COUNT  12

#define POKE_SCREEN_TOP    0
#define POKE_SCREEN_BOTTOM 1

/* The accessibility script speaks through this. `interrupt` false = queue. */
typedef void (*PokeSpeechCallback)(const char *utf8_text, bool interrupt, void *userdata);
/* Same contract, plus the announcement-queue utterance id to hand back to
 * poke_announce_done (0 = the line is not from the queue; report nothing). */
typedef void (*PokeSpeechIdCallback)(const char *utf8_text, bool interrupt,
                                     uint32_t utterance_id, void *userdata);
typedef void (*PokeLogCallback)(const char *utf8_text, void *userdata);
/* Positional sound cue from the reader's own sounds/ tree.
 *
 * The reader calls audio.play(path, flags, pan, volume) 42 times to convey
 * DIRECTION with sound (pan -100..100, negative = left; volume 0..100). A host
 * that cannot play these is losing real information, not decoration.
 *
 * `path` is the reader's own relative path -- it joins `scriptpath` itself, so it
 * arrives as e.g. "sounds\gba\s_grass.wav" with the reader's separators intact.
 * The host resolves it against the reader directory it already knows, and is free
 * to ignore cues entirely by leaving this callback unset.
 */
typedef void (*PokeSoundCallback)(const char *path, int pan, int volume, void *userdata);

typedef struct PokeCore PokeCore;

PokeCore *poke_create(void);
void poke_destroy(PokeCore *core);

void poke_set_speech_callback(PokeCore *core, PokeSpeechCallback cb, void *userdata);
/* When set, every line goes here instead of the plain speech callback. */
void poke_set_speech_id_callback(PokeCore *core, PokeSpeechIdCallback cb, void *userdata);
void poke_set_log_callback(PokeCore *core, PokeLogCallback cb, void *userdata);
/* Sound cues from the reader's audio.play. Optional: with no callback the cues are
 * recorded by the Lua stub and never heard -- the previous behaviour, not an error. */
void poke_set_sound_callback(PokeCore *core, PokeSoundCallback cb, void *userdata);

/* `script` is the compat shim and main.lua concatenated, in that order. */
bool poke_load_rom(PokeCore *core, const char *rom_path, const char *save_path);

/* Optional real BIOS + firmware dumps. Any may be NULL, in which case melonDS's
 * built-in FreeBIOS / generated firmware is used for that piece — but note the
 * generated firmware is NOT bootable, which forces direct boot and skips the
 * firmware/menu handshake a game's boot code may wait on. Call before
 * poke_load_rom. Sizes must be bios9 4096, bios7 16384, firmware >= 131072. */
void poke_set_firmware(PokeCore *core, const char *bios9_path,
                       const char *bios7_path, const char *firmware_path);

/* True when a real, bootable firmware image was installed. */
bool poke_has_firmware(PokeCore *core);
void poke_set_script(PokeCore *core, const char *script);
/* Directory holding the Game Boy Lua reader set (oga_bootstrap.lua + tree).
 * Game Boy ROMs only: call after poke_load_rom, before poke_start. NDS ROMs
 * use poke_set_script instead. */
void poke_set_script_dir(PokeCore *core, const char *dir);

/* WHICH BUNDLED READER SET the loaded ROM needs, as a name the caller resolves to a
 * directory under the app's staged readers (e.g. "Zelda1Access"), or "" when no
 * bundled reader covers this game.
 *
 * ⛔ "" IS A REAL ANSWER, NOT A FAILURE. An unknown dump, a translation patch or a
 * game nobody has written a reader for all give "". The caller must then start the
 * console with NO reader rather than attaching someone else's: a reader narrating
 * the wrong cartridge is the one outcome this project treats as worse than silence.
 *
 * Call AFTER poke_load_rom (the identity is a property of the cartridge) and BEFORE
 * poke_start, which is what loads the reader. */
const char *poke_reader_set(PokeCore *core);
// PSP runtime assets inside the app bundle; set before loading a PSP ROM.
void poke_set_psp_asset_dir(PokeCore *core, const char *asset_dir);
bool poke_start(PokeCore *core);
void poke_stop(PokeCore *core);
bool poke_running(PokeCore *core);
/* Non-zero once the emulated console stopped itself; see Platform::StopReason. */
int poke_stop_reason(PokeCore *core);

/* One emulated frame. Returns false once the core has stopped. Main thread. */
bool poke_frame(PokeCore *core);
bool poke_framebuffer(PokeCore *core, int screen, int *width, int *height);
/* Pointer to the current frame's RGBA8888 pixels, valid until the next frame. */
const uint8_t *poke_framebuffer_ptr(PokeCore *core, int screen);

/* Input. Buttons/touch are read by the core at the start of each frame. */
void poke_set_button(PokeCore *core, int ds_button, bool down);

/* Press a button for a number of EMULATED FRAMES, then release it.
 *
 * ⛔ A PRESS MUST SPAN EMULATED FRAMES, NOT WALL-CLOCK TIME. The VoiceOver tap path used to
 * schedule its release 0.12 s later, and frames come from a CADisplayLink that iOS throttles when
 * the phone is warm or in Low Power Mode. On a slow frame the press AND the release both landed
 * between two frames, the console sampled the pad with the button already up, and the press never
 * existed -- reported as "START does nothing" in Pokemon Crystal, and not reproducible on a host
 * that runs frames back to back.
 *
 * `frames` must be at least 2: the console samples input once per emulated frame, so a hold that
 * spans fewer than two frames can be missed. The release happens inside poke_frame, so it is
 * counted in frames whatever the frame rate is doing. Re-tapping the same button extends the hold
 * rather than latching a second release. */
void poke_tap_button(PokeCore *core, int ds_button, int frames);
/* PSP analog stick, -1..1. No-op on other cores. */
void poke_set_analog(PokeCore *core, float x, float y);
void poke_touch(PokeCore *core, int x, int y, bool down);
/* Hotkeys are the letters main.lua listens for: J K L I O P C E N B R U. */
void poke_set_hotkey(PokeCore *core, const char *key, bool down);

/* Audio: interleaved stereo s16. Returns frames written. */
int poke_read_audio(PokeCore *core, int16_t *out, int max_frames);
void poke_set_audio_enabled(PokeCore *core, bool enabled);

/* Savestates (whole-core, as melonDS does). */
bool poke_save_state(PokeCore *core, const char *path);
bool poke_load_state(PokeCore *core, const char *path);

/* Last error text, for the UI to speak instead of failing silently. */
const char *poke_last_error(PokeCore *core);

/* Native game adapters (Fire Emblem: Shadow Dragon today).
 *
 * Two layers exist and they are independent: the bundled Lua script is the
 * Pokémon reader, and an adapter is a native reader for a game the script does
 * not know. A ROM gets whichever applies — the code's game code decides.
 *
 * ⛔ A ROM WITH NO ADAPTER IS THE NORMAL CASE, NOT A FAILURE. poke_adapter_id
 * returns NULL for every game nobody has written a reader for, and the caller
 * must treat that as "use the script" rather than as an error. */

/* The adapter selected for the loaded ROM, or NULL. Its own id string, e.g.
 * "fe11". Stable for the lifetime of the loaded ROM. */
const char *poke_adapter_id(PokeCore *core);

/* Human-readable name of the selected adapter, for the UI to speak. NULL when
 * there is none. */
const char *poke_adapter_name(PokeCore *core);

/* The loaded ROM's four-letter game code from header offset 0x0C, e.g. "IRBO"
 * or "YFEE". Empty string when no ROM is loaded. This is what decides BOTH the
 * adapter AND whether the bundled Lua script knows the game, so the UI reads
 * it to hide the script's buttons for games the script cannot narrate. */
const char *poke_game_code(PokeCore *core);

/* True when the selected adapter has usable game state RIGHT NOW.
 *
 * This is the gate the UI asks before offering adapter commands: a map may not
 * have loaded yet, in which case the adapter has nothing truthful to say and must
 * stay silent rather than narrate uninitialised memory. False when no adapter. */
bool poke_adapter_ready(PokeCore *core);

/* Run one accessibility command on the selected adapter. It speaks through the
 * core's speech callback. Returns false when there is no adapter or it is not
 * ready, so the UI can say so instead of appearing broken.
 *
 * `cmd` values match oga::Command in Core/adapter.h:
 *   0 WhereAmI, 1 NextAlly, 2 PrevAlly, 3 NextEnemy, 4 PrevEnemy,
 *   5 NextUnactedAlly, 6 DumpState ...
 *
 * Two of them are CORE-INTERNAL and need no adapter: StopSpeech (37) and the spoken-
 * history walk RepeatNewest (38) / RepeatOlder (39). They work on any game, including the
 * Lua script and a game with no adapter, because the history lives in the announcement
 * queue rather than in a game reader. */
bool poke_command(PokeCore *core, int cmd);

/* The DS button id for an adapter command, so the UI can label a button with the
same control the game itself uses. -1 when the command has no game button. */
int poke_command_button(PokeCore *core, int cmd);

/* Per-frame battle snapshot for the host cue synth (docs/design/cue-synth.md).
 *
 * 0 = nothing to cue (no adapter, no cue data, not attached yet, or no battle)
 * 1 = in battle, no lock
 * 2 = in battle, locked on the ENEMY
 * 3 = in battle, locked on the EX CORE
 *
 * `*dist` receives the distance to the locked target, or 0 when not locked.
 *
 * ⛔ 0 IS THE FAIL-CLOSED VALUE: a host that gets it stays silent rather than beeping at
 * nothing. This never attaches the adapter and never speaks — it is safe to call at frame
 * rate, and the cue begins once the first command has attached the reader. */
int poke_cue_snapshot(PokeCore *core, float *dist);

/* The FIELD-MODE cue: the same snapshot, plus the two things a free-flight mode needs and a
 * lock-on mode does not -- what the cue points AT, and WHICH WAY to turn to reach it.
 *
 * WHY A SECOND ENTRY POINT RATHER THAN MORE OUT-PARAMS ON THE FIRST: poke_cue_snapshot's
 * signature is the shared ABI for every adapter and every host. Adding out-params to it
 * changes the contract for callers that have nothing to do with field mode; a new call
 * leaves the existing one exactly as it was, and a host that never asks for a bearing never
 * pays for one.
 *
 * Returns 0 = nothing to cue (stay silent), the same fail-closed value as poke_cue_snapshot.
 * Otherwise:
 *   *kind         what the cue points at: 1 = enemy, 2 = city, 3 = ally
 *   *bearing_deg  0..360, CLOCKWISE FROM THE DIRECTION THE PLAYER IS MOVING.
 *                 0 = straight ahead, 90 = to the right, 180 = behind.
 *   *heading_live false when that direction came from the last time the player moved and
 *                 may no longer be where they are facing. The host must not steer by a
 *                 stale heading; it should soften the cue or say so.
 *   *dist         distance to the target, world units.
 */
int poke_cue_snapshot_field(PokeCore *core, int *kind, float *bearing_deg, float *dist,
                            int *heading_live);

/* Announcement-queue completion hooks (additive: estimate pacing works without
 * them). The queue releases one line at a time; a platform that can report
 * "finished" calls poke_announce_done with the utterance id it got from the
 * id speech callback, so the next line releases immediately instead of
 * waiting out its estimate. Thread-safe: may be called from any thread; the
 * frame thread applies it. */
void poke_announce_done(PokeCore *core, uint32_t utterance_id, int success);
/* Legacy, for hosts on the plain speech callback: newest utterance id whose
 * text equals utf8_text exactly, or 0. Ambiguous for repeated lines; prefer
 * poke_set_speech_id_callback. Frame thread only. */
uint32_t poke_announce_id_for_text(PokeCore *core, const char *utf8_text);

const char *poke_version(void);

/* ---------------------------------------------------------------- systems
 *
 * The console registry (Core/systems.h) re-exported here, because CPokeCore is
 * the ONLY thing Swift is allowed to see of the core: a Swift file cannot
 * include Core/systems.h without dragging the whole core into the module map.
 *
 * The point of exposing it is that the UI should not keep its own copy of "which
 * consoles exist and what each is like". It had one (GameSystem.swift, a switch
 * over four systems with its own extension table), and that copy is how a .gba
 * came to be handled by a UI path that knew nothing about the Game Boy.
 *
 * ⛔ ABI NOTE: these three are the registry's read-only surface. The ids cross
 * this boundary as plain ints (matching OgaSystemId), so the enum is APPEND-ONLY
 * — see the warning in systems.h.
 */

/* Which console a ROM path belongs to, by extension; NULL when unknown.
 * ⛔ NULL means "cannot run this", never "assume the DS". */
const void* oga_system_for_path(const char* path);

/* True when this build has a backend that can actually load that console. */
bool oga_system_is_runnable(const void* sys);

/* What the player should hear for a file this build cannot run: names the
 * extension, or the console when the file was understood. Never NULL. */
const char* oga_unsupported_reason(const char* path);

/* Console id (OgaSystemId) for a resolved system, or 0 for unknown. */
int oga_system_id(const void* sys);

/* A system by its id (OgaSystemId), or NULL for one this build does not list.
 * This is how the UI names a console it has no file for — the pre-load default
 * pad is the DS, and there is no extension to look it up by. */
const void* oga_system_by_id(int id);

/* Display name ("Game Boy Advance"), or "" for NULL. Stable pointer. */
const char* oga_system_name(const void* sys);

/* One face button: writes up to `max` (title, sf_symbol, hint, raw pad index).
 * Returns how many were written. The button list is what the pad renders, so a
 * console with two buttons must not report four. */
int oga_system_face_buttons(const void* sys,
                            const char** titles, const char** symbols,
                            const char** hints, int* raws, int max);

/* Screens the console draws (2 only on the DS family). */
int oga_system_screen_count(const void* sys);

/* Shoulder buttons on the pad? */
bool oga_system_has_shoulders(const void* sys);

/* Analog stick count: 0 means d-pad only. */
int oga_system_analog_sticks(const void* sys);

/* Every known console, as opaque handles. Returns the count; the array is
 * owned by the registry and must not be freed.
 *
 * ⛔ NOT named oga_all_systems: Core/systems.h already exports that name with a
 * `const OgaSystem* const*` return type, and two signatures for one C symbol is
 * an ODR violation. This is the opaque-handle mirror of it. */
const void* const* oga_all_system_handles(int* out_count);

#ifdef __cplusplus
}
#endif

#endif /* POKECORE_H */
