/* n64_core.h — C ABI for the Nintendo 64 core (Mupen64Plus-Next).
 *
 * The same contract genesis_core.h set out for the Genesis, so pokecore.cpp
 * and Core/oga_core.cpp treat the N64 like every other backend. Underneath is
 * Mupen64Plus-Next, driven through its libretro entry points as a static
 * library (scripts/core-sources.sh, M64P block). Its symbols are renamed with
 * the oga_m64p_ prefix by scripts/m64p-isolate.sh, so the core cannot collide
 * with the other emulators in this link.
 *
 * THE SOFTWARE PATH, ALWAYS. Video is angrylion (pixel-accurate software RDP),
 * its RSP is cxd4 (LLE), and the CPU is the cached interpreter. No GPU, no GL
 * context, no JIT: iOS cannot JIT on a free sideload, and this is the one
 * configuration that runs the same on iOS, Android and the host proof. It is
 * also slow: expect well under full speed on a phone for 3D-heavy games until
 * a faster path (dynarec on Android, the parallel RSP) is added.
 *
 * ONE MACHINE. Mupen64Plus is a process-wide singleton (g_dev and friends);
 * n64_create refuses a second live core, as genesis_create does.
 */
#ifndef N64_CORE_H
#define N64_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* N64 controller inputs: indices into this core's own table. Core/oga_core.cpp
 * maps the app's shared pad onto these; see the N64 row in Core/systems.cpp. */
#define N64_BTN_A        0
#define N64_BTN_B        1
#define N64_BTN_Z        2
#define N64_BTN_START    3
#define N64_BTN_UP       4
#define N64_BTN_DOWN     5
#define N64_BTN_LEFT     6
#define N64_BTN_RIGHT    7
#define N64_BTN_L        8
#define N64_BTN_R        9
#define N64_BTN_C_UP     10
#define N64_BTN_C_DOWN   11
#define N64_BTN_C_LEFT   12
#define N64_BTN_C_RIGHT  13
#define N64_BTN_COUNT    14

/* RDRAM as the CPU addresses it: physical 0x00000000-0x007FFFFF (8 MiB, the
 * Expansion Pak size the core always allocates), also reachable through the
 * KSEG0 (0x80000000) and KSEG1 (0xA0000000) mirrors games use. */
#define N64_RDRAM_SIZE 0x800000u

typedef struct N64Core N64Core;

N64Core *n64_create(void);
void n64_destroy(N64Core *core);

/* Load a .z64/.n64/.v64 ROM (any byte order; the core normalises it).
 * save_path is the battery-save file (.srm: EEPROM, SRAM, FlashRAM and the
 * Controller Paks in the core's own layout): restored on load when it exists
 * and matches the core's size, written back on stop and on save_state when the
 * game changed it. code_out receives the game's identity as "n64:" plus the
 * four-character cartridge ID from the ROM header (e.g. "n64:NPOE"), which is
 * what the adapter registry matches on. */
bool n64_load_rom(N64Core *core, const char *rom_path, const char *save_path,
                  char code_out[16]);

bool n64_start(N64Core *core);
void n64_stop(N64Core *core);
bool n64_running(N64Core *core);

/* One emulated frame (one VI interrupt), then capture video and audio. */
bool n64_frame(N64Core *core);

/* Video: RGBA8888, converted from the core's XRGB8888 output. The pointer is
 * valid until the next n64_frame. */
bool n64_framebuffer(N64Core *core, int *width, int *height);
const uint8_t *n64_framebuffer_ptr(N64Core *core);

void n64_set_button(N64Core *core, int n64_button, bool down);
/* The analog stick, each axis -1..1, +x right, +y DOWN (screen convention, as
 * the app's other analog backends take it). */
void n64_set_analog(N64Core *core, float x, float y);

/* Drain audio as interleaved stereo s16 at the app's rate (32768 Hz),
 * resampled from the core's own output rate. Returns frames written. */
int n64_read_audio(N64Core *core, int16_t *out, int max_frames);

bool n64_save_state(N64Core *core, const char *path);
bool n64_load_state(N64Core *core, const char *path);

unsigned long long n64_frames_completed(N64Core *core);
const char *n64_last_error(N64Core *core);

/* Read-only RDRAM read for the adapter Host path, width 1, 2 or 4, returning
 * THE CPU'S VIEW (big-endian), whatever the host's byte order: the core keeps
 * RDRAM as host-order 32-bit words, and the adapter must never need to know
 * that. Returns false outside RDRAM. */
bool n64_read(N64Core *core, uint32_t addr, int width, uint32_t *out);

/* The core's library version string (from retro_get_system_info), for the
 * host proof and diagnostics. */
const char *n64_core_version(void);

#ifdef __cplusplus
}
#endif

#endif /* N64_CORE_H */
