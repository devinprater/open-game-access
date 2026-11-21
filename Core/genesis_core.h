/* genesis_core.h — C ABI for the Sega Genesis / Mega Drive core (Genesis Plus GX).
 *
 * Mirrors snes_core.h / mesen_core.h deliberately: the app already knows how to
 * drive a core with this contract (create/load/start/frame/buttons/audio/states),
 * so the Genesis speaks the same language and GameSession only branches on ROM kind.
 *
 * THE CORE. Genesis Plus GX (ekeeke/Genesis-Plus-GX), pinned in
 * scripts/bootstrap-deps.sh. Plain C, no UI dependencies — the property that made
 * mGBA and Mesen portable here. Its one demand is a frontend shim header named
 * osd.h (Core/gpgx/osd.h): the config struct, the optional-BIOS paths, and the
 * file loader. That header is on this core's include path ONLY.
 *
 * SCOPE, STATED PLAINLY. Genesis / Mega Drive cartridges only. The same upstream
 * tree can run Sega CD, Master System and Game Gear; this backend does not route
 * them — the resolver sends .sms/.gg to the Master System registry row, which has
 * no backend, and no CD image reaches load_rom. The CD hardware sources are still
 * COMPILED (the core's own frame dispatcher references them), which is not the
 * same as offering Sega CD: no CD file can be selected, loaded or played.
 *
 * THE PAD. Genesis Plus GX defaults a port to the 6-button pad, and so does this
 * host: a 6-button pad is a superset the games themselves detect — a 3-button
 * game reads A/B/C/Start from the same lines and never asks about X/Y/Z/Mode.
 * So one pad serves both, and the registry lists all six face buttons.
 *
 * Identity is the ROM's CONTENT, as on the SNES: genesis_load_rom writes
 * "crc:<8 lowercase hex>" of the image the core actually runs (post-deinterleave
 * for .smd dumps) into code_out. No adapter keys on it today; it is the stable
 * per-game id the app already expects a core to produce.
 */
#ifndef GENESIS_CORE_H
#define GENESIS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Genesis button ids: indices into this core's own table. pokecore.cpp's Genesis
 * branch (Core/oga_core.cpp) translates the app's shared POKE_BTN_* indices to
 * these. The order is the pad's own reading order: face buttons in two rows
 * (A B C over X Y Z), then Mode and Start, then the d-pad. */
#define GEN_BTN_A      0
#define GEN_BTN_B      1
#define GEN_BTN_C      2
#define GEN_BTN_X      3
#define GEN_BTN_Y      4
#define GEN_BTN_Z      5
#define GEN_BTN_MODE   6
#define GEN_BTN_START  7
#define GEN_BTN_UP     8
#define GEN_BTN_DOWN   9
#define GEN_BTN_LEFT   10
#define GEN_BTN_RIGHT  11
#define GEN_BTN_COUNT  12

/* The Genesis work RAM window: 64 KiB of 68k RAM at 0xFF0000-0xFFFFFF.
 * genesis_read serves this window only (and the cartridge SRAM window below):
 * a read of an MMIO address has side effects on real hardware, and an adapter
 * must be able to tell "0" from "not this console's RAM" — the SNES rule. */
#define GEN_RAM_BASE 0xFF0000u
#define GEN_RAM_SIZE 0x10000u

typedef struct GenesisCore GenesisCore;

GenesisCore *genesis_create(void);
void genesis_destroy(GenesisCore *core);

/* Load a .md/.gen/.smd/.bin ROM. save_path is the battery-save file (.sav): when
 * it exists, its bytes are restored into the cartridge's SRAM after load; the
 * SRAM is flushed back on stop (and before a state save) whenever it changed.
 * On success writes the NUL-terminated content id ("crc:<hex>") into code_out. */
bool genesis_load_rom(GenesisCore *core, const char *rom_path, const char *save_path,
                      char code_out[16]);

bool genesis_start(GenesisCore *core);
void genesis_stop(GenesisCore *core);
bool genesis_running(GenesisCore *core);

/* One emulated frame: run the core to the next vblank, then capture the frame
 * and drain nothing (audio is pulled separately, the app's model). Genesis Plus
 * GX runs synchronously — there is no emulation thread to stall — so a false
 * return means "not loaded", never "still working on it". */
bool genesis_frame(GenesisCore *core);

/* Video. Pixels are RGBA8888, converted from the core's 32bpp output during the
 * frame capture; valid until the next frame. */
bool genesis_framebuffer(GenesisCore *core, int *width, int *height);
const uint8_t *genesis_framebuffer_ptr(GenesisCore *core);

void genesis_set_button(GenesisCore *core, int genesis_button, bool down);

/* Drain the core's audio as interleaved stereo s16 at the app's rate (32768 Hz,
 * the same contract as poke_read_audio). Returns frames written; 0 when dry. */
int genesis_read_audio(GenesisCore *core, int16_t *out, int max_frames);

/* Whole-core states (Genesis Plus GX state_save/state_load), same contract as
 * poke_save_state. Saving a state flushes battery SRAM first, so a state and
 * its .sav never disagree about which is newer. */
bool genesis_save_state(GenesisCore *core, const char *path);
bool genesis_load_state(GenesisCore *core, const char *path);

unsigned long long genesis_frames_completed(GenesisCore *core);
const char *genesis_last_error(GenesisCore *core);

/* Read-only memory read for the adapter Host path: work RAM (0xFF0000 window)
 * and the cartridge SRAM as the game maps it (0x200000-0x20FFFF, the battery
 * window most SRAM games use). width is 1 or 2; the 68k is big-endian, so a
 * width-2 read returns the word as the CPU sees it. Returns false outside both
 * windows — "outside the map", never a fabricated zero. */
bool genesis_read(GenesisCore *core, uint32_t addr, int width, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_CORE_H */
