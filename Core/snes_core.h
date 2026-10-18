/* snes_core.h — C ABI for the Super Nintendo console (MesenCE).
 *
 * Mirrors mesen_core.h deliberately: the app and the front ends already know
 * how to drive a core with this contract, so the SNES speaks the same language.
 *
 * ⛔ IDENTITY IS THE ROM'S CONTENT. A Link to the Past is a 1991 cartridge with
 * no usable four-letter game code (0x7FB0 holds 01 8D 24 01), so snes_load_rom
 * writes "crc:<8 lowercase hex>" into code_out. That string is the adapter
 * registry's key and is never spoken.
 */
#ifndef MESEN_SNES_CORE_H
#define MESEN_SNES_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SNES pad ids: indices into this core's own table, in Mesen's SnesController
 * order ("ABXYLRSTUDLR"). pokecore.cpp's SNES branch translates the app's shared
 * POKE_BTN_* indices to these, exactly as the PSP branch does. */
#define SNES_BTN_A      0
#define SNES_BTN_B      1
#define SNES_BTN_X      2
#define SNES_BTN_Y      3
#define SNES_BTN_L      4
#define SNES_BTN_R      5
#define SNES_BTN_SELECT 6
#define SNES_BTN_START  7
#define SNES_BTN_UP     8
#define SNES_BTN_DOWN   9
#define SNES_BTN_LEFT   10
#define SNES_BTN_RIGHT  11
#define SNES_BTN_COUNT  12

typedef struct SnesCore SnesCore;

SnesCore *snes_create(void);
void snes_destroy(SnesCore *core);

/* Load a .sfc/.smc/.fig/.swc ROM. save_path becomes Mesen's home folder, the
 * same contract as nes_load_rom: without it Mesen throws "Home folder not
 * specified" from inside LoadRom and the caller sees only `false`.
 */
bool snes_load_rom(SnesCore *core, const char *rom_path, const char *save_path,
                   char code_out[16]);

/* The dump's CRC32 over the ROM PAYLOAD (a 512-byte copier header is skipped),
 * as measured in Core/snes_core.cpp. 0 before a successful load. */
uint32_t snes_rom_crc32(SnesCore *core);

bool snes_start(SnesCore *core);
void snes_stop(SnesCore *core);
bool snes_running(SnesCore *core);

/* One emulated frame: waits for the console's own frame counter to advance.
 * Returns false when the console stopped producing frames. */
bool snes_frame(SnesCore *core);
/* Why the last snes_frame returned what it did. Never NULL. */
const char *snes_frame_result(SnesCore *core);

/* Power-cycle through Mesen's own reset. The console object is replaced, which
 * snes_frame is built to follow. */
void snes_reset(SnesCore *core);

/* Video, RGBA8888. The SNES PPU produces palette indices, so the core runs the
 * console's own video filter and converts its ABGR words to RGBA bytes. */
bool snes_framebuffer(SnesCore *core, int *width, int *height);
const uint8_t *snes_framebuffer_ptr(SnesCore *core);
/* Why the last frame produced no video pixels. Diagnostic only, never NULL. */
const char *snes_video_result(SnesCore *core);

void snes_set_button(SnesCore *core, int snes_button, bool down);

/* ---- memory reads (the adapter Host path) ----
 *
 * The SNES work RAM is 128 KiB at 0x7E0000-0x7FFFFF (banks 7E and 7F). Reads
 * outside that window are REFUSED rather than routed through the CPU bus: a
 * read of an MMIO address such as $2100 has side effects, and an adapter must
 * be able to tell "0" from "not this console's RAM".
 */
#define SNES_RAM_BASE 0x7E0000u
#define SNES_RAM_SIZE 0x20000u

bool snes_read(SnesCore *core, uint32_t addr, int width, uint32_t *out);
uint32_t snes_ram_base(void);
uint32_t snes_ram_size(void);

/* The emulated CPU's own program counter, for liveness diagnostics. -1 when
 * unavailable. */
int snes_cpu_pc(SnesCore *core);

/* Test/tooling only: run uncapped so a scripted drive does not cost real
 * minutes. The app leaves this off. */
void snes_set_uncapped(SnesCore *core, bool uncapped);

unsigned long long snes_frames_completed(SnesCore *core);
unsigned snes_console_frame_count(SnesCore *core);
double snes_fps(SnesCore *core);

/* The last failure, in words. Empty string when nothing failed. */
const char *snes_last_error(SnesCore *core);

#ifdef __cplusplus
}
#endif

#endif /* MESEN_SNES_CORE_H */
