/* psp_core.h — C ABI for the PlayStation Portable core (PPSSPP) on iOS.
 *
 * Mirrors gba_core.h / pokecore.h's shape deliberately: the Swift side already
 * knows how to drive a core with this contract (create/load/start/frame/
 * speech/buttons), so the PSP core speaks the same language and GameSession
 * only branches on ROM kind. There is no Lua reader for PSP — the native
 * game adapters (Dissidia, ...) read through the Host in pokecore.cpp, which
 * routes PSP addresses to psp_debug_read here.
 *
 * Runtime configuration (fixed, not negotiable without re-proving):
 *   CPU = IR interpreter (no JIT: iOS has no MAP_JIT under a free sideload,
 *     same reason the DS core runs its interpreter).
 *   GPU = software rasterizer, read back on CPU (no GL context on iOS here).
 */
#ifndef PSP_CORE_H
#define PSP_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PSP button ids: indices into this core's own table below. pokecore.cpp's
 * PSP branch translates the app's pad indices to these (X/Y have no PSP
 * equivalent and are ignored, like on GBA). */
#define PSP_BTN_SELECT   0
#define PSP_BTN_START    1
#define PSP_BTN_UP       2
#define PSP_BTN_RIGHT    3
#define PSP_BTN_DOWN     4
#define PSP_BTN_LEFT     5
#define PSP_BTN_TRIANGLE 6
#define PSP_BTN_CIRCLE   7
#define PSP_BTN_CROSS    8
#define PSP_BTN_SQUARE   9
#define PSP_BTN_L        10
#define PSP_BTN_R        11
#define PSP_BTN_COUNT    12

/* PSP screen dimensions. */
#define PSP_FB_W 480
#define PSP_FB_H 272

typedef void (*PspSpeechCallback)(const char *utf8_text, bool interrupt, void *userdata);
typedef void (*PspLogCallback)(const char *utf8_text, void *userdata);

typedef struct PspCore PspCore;

PspCore *psp_create(void);
void psp_destroy(PspCore *core);

void psp_set_speech_callback(PspCore *core, PspSpeechCallback cb, void *userdata);
void psp_set_log_callback(PspCore *core, PspLogCallback cb, void *userdata);

/* Load a .iso/.cso/.pbp/.elf PSP game. On success writes the NUL-terminated
 * game ID (e.g. ULUS10437, from PARAM.SFO) into code_out. save_path is the
 * memstick root this core gets (savedata lives under PSP/SAVEDATA below it).
 */
/* Asset root override. The app calls this with its bundled ppsspp-assets
 * directory (see scripts/ppsspp-stage-assets.sh) before loading; without it
 * the core falls back to $PPSSPP_ASSETS, then ./ppsspp-assets. Must outlive
 * the core or until replaced.
 */
void psp_set_asset_dir(PspCore *core, const char *asset_dir);

bool psp_load_rom(PspCore *core, const char *rom_path, const char *save_path,
                  char code_out[16]);

bool psp_start(PspCore *core);
void psp_stop(PspCore *core);
bool psp_running(PspCore *core);

/* One emulated frame: latch buttons, run to the next vblank, pump callbacks. */
bool psp_frame(PspCore *core);

/* Video. Pixels are RGBA8888, valid until the next frame. */
bool psp_framebuffer(PspCore *core, int *width, int *height);
const uint8_t *psp_framebuffer_ptr(PspCore *core);

void psp_set_button(PspCore *core, int psp_button, bool down);

/* Whole-core states (PPSSPP savestates), same contract as poke_save_state. */
bool psp_save_state(PspCore *core, const char *path);
bool psp_load_state(PspCore *core, const char *path);

unsigned long long psp_frames_completed(PspCore *core);
const char *psp_last_error(PspCore *core);

/* Raw PSP memory read (user RAM 0x08000000..0x0A000000 and friends) for the
 * adapter Host path and binding verification. */
uint32_t psp_debug_read(PspCore *core, uint32_t addr, int width);

/* Raw PSP memory write for test cheats (e.g. zeroing foe HP to skip a
 * fight the bot cannot win). Same guards as the read; no-op on bad input. */
void psp_debug_write(PspCore *core, uint32_t addr, uint32_t value, int width);

#ifdef __cplusplus
}
#endif

#endif /* PSP_CORE_H */
