/* gba_core.h — C ABI for the Game Boy / GBC / GBA core (mGBA) on iOS.
 *
 * Mirrors pokecore.h's shape deliberately: the Swift side already knows how to
 * drive a core with this contract (create/load/start/frame/speech/buttons),
 * so the GBA core speaks the same language and GameSession only branches on
 * ROM kind. The Lua accessibility readers (pokemon.lua + set) run inside this
 * core's own vendored Lua 5.4 state, exactly the way main.lua runs inside the
 * DS core — see gba_core.cpp for the binding map.
 */
#ifndef GBA_CORE_H
#define GBA_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GBA button ids. The first ten match POKE_BTN_* order and mGBA's own
 * GBAKey order (A,B,Select,Start,Right,Left,Up,Down,R,L); X/Y do not exist
 * on a GBA and are ignored here. */
#define GBA_BTN_A      0
#define GBA_BTN_B      1
#define GBA_BTN_SELECT 2
#define GBA_BTN_START  3
#define GBA_BTN_RIGHT  4
#define GBA_BTN_LEFT   5
#define GBA_BTN_UP     6
#define GBA_BTN_DOWN   7
#define GBA_BTN_R      8
#define GBA_BTN_L      9
#define GBA_BTN_COUNT  10

/* mGBA's platform numbers, repeated here so Swift never includes mGBA headers:
 * 0 = GBA, 1 = GB/GBC. Matches what the reader's get_device() expects from
 * emu.platform(). */
#define GBA_PLATFORM_GBA 0
#define GBA_PLATFORM_GB  1

typedef void (*GbaSpeechCallback)(const char *utf8_text, bool interrupt, void *userdata);
typedef void (*GbaLogCallback)(const char *utf8_text, void *userdata);

typedef struct GbaCore GbaCore;

GbaCore *gba_create(void);
void gba_destroy(GbaCore *core);

void gba_set_speech_callback(GbaCore *core, GbaSpeechCallback cb, void *userdata);
void gba_set_log_callback(GbaCore *core, GbaLogCallback cb, void *userdata);

/* Directory holding the reader Lua set (oga_bootstrap.lua, mgba_compat.lua,
 * oga_bit/audio/pure.lua, pokemon.lua, gb/gba.lua, game/, message/). The
 * bootstrap derives everything else from its own path, so this one directory
 * is the only thing the host must supply. */
void gba_set_script_dir(GbaCore *core, const char *dir);

/* Load a .gba/.gbc/.gb ROM. On success writes the NUL-terminated game
 * identifier into code_out (GBA: 4-char code from header+0xAC; GB/GBC: the
 * 16-byte title from header+0x134, truncated to 15 chars) and the platform
 * (GBA_PLATFORM_*) into platform_out. */
bool gba_load_rom(GbaCore *core, const char *rom_path, const char *save_path,
                  char code_out[16], int *platform_out);

bool gba_start(GbaCore *core);
void gba_stop(GbaCore *core);
bool gba_running(GbaCore *core);

/* One emulated frame: latch buttons, run, resume the reader coroutine. */
bool gba_frame(GbaCore *core);

/* Video. Dimensions are the core's own (GBA 240x160, GB/GBC 160x144);
 * pixels are RGBA8888, valid until the next frame. */
bool gba_framebuffer(GbaCore *core, int *width, int *height);
const uint8_t *gba_framebuffer_ptr(GbaCore *core);

void gba_set_button(GbaCore *core, int gba_button, bool down);
/* Hotkey letters the reader listens for (Y M E P H J K L ...). */
void gba_set_hotkey(GbaCore *core, const char *key, bool down);

/* Whole-core states (mGBA serialisation), same contract as poke_save_state. */
bool gba_save_state(GbaCore *core, const char *path);
bool gba_load_state(GbaCore *core, const char *path);

unsigned long long gba_frames_completed(GbaCore *core);
const char *gba_last_error(GbaCore *core);

/* Host-only diagnostic: raw bus read for binding verification (compare
 * against the ROM file). ios-debug: not used by the app. */
uint32_t gba_debug_read(GbaCore *core, uint32_t addr, int width);

#ifdef __cplusplus
}
#endif

#endif /* GBA_CORE_H */
