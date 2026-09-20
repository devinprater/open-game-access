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
typedef void (*PokeLogCallback)(const char *utf8_text, void *userdata);

typedef struct PokeCore PokeCore;

PokeCore *poke_create(void);
void poke_destroy(PokeCore *core);

void poke_set_speech_callback(PokeCore *core, PokeSpeechCallback cb, void *userdata);
void poke_set_log_callback(PokeCore *core, PokeLogCallback cb, void *userdata);

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
 *   5 NextUnactedAlly, 6 DumpState */
bool poke_command(PokeCore *core, int cmd);

/* The DS button id for an adapter command, so the UI can label a button with the
 * same control the game itself uses. -1 when the command has no game button. */
int poke_command_button(PokeCore *core, int cmd);

const char *poke_version(void);

#ifdef __cplusplus
}
#endif

#endif /* POKECORE_H */
