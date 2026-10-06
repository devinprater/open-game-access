/* mesen_core.h — C ABI for the Nintendo Entertainment System core (MesenCE).
 *
 * Mirrors gba_core.h / psp_core.h deliberately: the Swift and Kotlin sides already know how to
 * drive a core with this contract (create/load/start/frame/speech/buttons), so the NES speaks the
 * same language and GameSession only branches on ROM kind.
 *
 * WHY THIS FILE EXISTS AT ALL. Until now the NES was the one console the registry listed as
 * PLANNED with a CHOSEN core and no glue: Core/nes_adapter.cpp is a deliberate seam that REFUSES,
 * because an adapter with no console to read from would have to invent addresses, and speaking
 * confident nonsense to a blind player is the one failure mode this project treats as worse than
 * silence. This header is the missing artifact that seam was waiting for.
 *
 * ⛔ WHAT A PASS HERE MEANS. Mesen's NES core is ADMITTED (44 NES TUs + 41 Shared compile for
 * aarch64-linux-android26, scripts/mesen-feasibility.sh). An admission test is not a backend: it
 * says the code COMPILES for the target, and nothing about booting a ROM, frame pacing or
 * performance. Those are measured, not assumed, and the measurement lives in the host proof
 * (scripts/nes-live-proof.sh), not in this comment.
 *
 * Runtime configuration (fixed, not negotiable without re-proving):
 *   No Qt, no SDL. Mesen's console cores link against Core/Shared and nothing else -- the same
 *   property that made mGBA portable here and Dolphin not.
 */
#ifndef MESEN_CORE_H
#define MESEN_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NES button ids: indices into this core's own table. pokecore.cpp's NES branch translates the
 * app's shared pad indices to these. A NES pad has no X/Y (ignored, like GBA) and no analog. */
#define NES_BTN_A       0
#define NES_BTN_B       1
#define NES_BTN_SELECT  2
#define NES_BTN_START   3
#define NES_BTN_UP      4
#define NES_BTN_DOWN    5
#define NES_BTN_LEFT    6
#define NES_BTN_RIGHT   7
#define NES_BTN_COUNT   8

/* NES screen: 256x240, the visible NTSC frame. Mesen renders 256x240 before overscan cropping. */
#define NES_FB_W 256
#define NES_FB_H 240

/* The NES CPU address space is 64 KB. Internal RAM is 2 KB at 0x0000-0x07FF, mirrored every
 * 0x800 bytes; PRG ROM is mapped at 0x8000+. The adapter reads through the CONSOLE (so mapper
 * banking is respected), while RAM_BASE for a Lua reader is flat 0 (see the note in
 * nes_adapter.cpp about the shim's hardcoded 0x02000000 DS base).
 */
#define NES_RAM_BASE 0x0000
#define NES_RAM_SIZE 0x800

typedef void (*NesSpeechCallback)(const char *utf8_text, bool interrupt, void *userdata);
typedef void (*NesLogCallback)(const char *utf8_text, void *userdata);

typedef struct NesCore NesCore;

NesCore *nes_create(void);
void nes_destroy(NesCore *core);

void nes_set_speech_callback(NesCore *core, NesSpeechCallback cb, void *userdata);
void nes_set_log_callback(NesCore *core, NesLogCallback cb, void *userdata);

/* Load a .nes/.fds/.unf ROM. On success writes the NUL-terminated game code (Mesen's own ROM
 * database id when it recognises the file, else the header-derived code), matching the way the
 * registry identifies a game. save_path is where battery-backed RAM is flushed; pass NULL for a
 * game with no save.
 */
bool nes_load_rom(NesCore *core, const char *rom_path, const char *save_path,
                  char code_out[16]);

/* Drain mixer output as interleaved stereo s16 at the app's rate (32768 Hz, the same contract as
 * poke_read_audio). Returns frames written; 0 when dry. */
int nes_read_audio(NesCore *core, int16_t *out, int max_frames);

bool nes_start(NesCore *core);
/* Test/tooling only: run the console uncapped (no real-time frame limiting) so a scripted drive
 * through a game's menus does not cost real minutes. The app leaves this off. */
void nes_set_uncapped(NesCore *core, bool uncapped);
void nes_stop(NesCore *core);
bool nes_running(NesCore *core);

/* One emulated frame: latch buttons, run to the next vblank, pump callbacks. */
bool nes_frame(NesCore *core);

/* Video. Pixels are RGBA8888, valid until the next frame. */
bool nes_framebuffer(NesCore *core, int *width, int *height);
const uint8_t *nes_framebuffer_ptr(NesCore *core);

void nes_set_button(NesCore *core, int nes_button, bool down);

/* Whole-core states (Mesen savestates), same contract as poke_save_state. */
bool nes_save_state(NesCore *core, const char *path);
bool nes_load_state(NesCore *core, const char *path);

unsigned long long nes_frames_completed(NesCore *core);
/* Diagnostic read-onlys: the emulator's own paused flag, and the console's frame counter. Mesen
 * gates its frame limiter on these and on an internal lock counter, so a harness that sees a
 * stalled console needs to know which of them is holding it. */
int nes_is_paused(NesCore *core);
unsigned nes_console_frame_count(NesCore *core);
/* The emulated CPU's own position, and the PPU's scanline. Together they answer "is the machine
 * executing?" directly, which RAM sampling only infers. -1 means unavailable. */
int nes_cpu_pc(NesCore *core);
long long nes_cpu_cycles(NesCore *core);
int nes_ppu_scanline(NesCore *core);
/* The two numbers Mesen's frame delay is built from: 1000 / fps / (speed / 100). A frame delay that
 * jumps by seconds explains a console that stops executing while nothing reports as paused. */
double nes_fps(NesCore *core);
int nes_emulation_speed(NesCore *core);
const char *nes_last_error(NesCore *core);

/* Raw NES memory read through the CONSOLE, so mapper banking and the RAM mirrors behave as the
 * game sees them. width is 1 or 2. Returns false when the address is not readable -- an adapter
 * must be able to tell "0" from "outside the map", or it will narrate garbage.
 *
 * ⛔ READ-ONLY, AND THE ADAPTER CONTRACT DEPENDS ON IT. There is deliberately no nes_debug_write:
 * game adapters inspect memory and drive the console's real buttons; they never write RAM or
 * teleport game state.
 */
bool nes_read(NesCore *core, uint32_t addr, int width, uint32_t *out);

/* Where the game's own RAM sits in the address space this core exposes, so a Lua reader shim can
 * point RAM_BASE at the right place per console instead of hardcoding the DS's 0x02000000. */
uint32_t nes_ram_base(NesCore *core);

/* PPU nametable RAM read (CIRAM). NOT part of the CPU address space: it is a separate 2 KB region the
 * PPU owns, reachable only through the PPU. Dragon Warrior's reader inspects it to identify the
 * current screen, because the game's menus are recognised by their nametable text. Returns 0 when the
 * console cannot serve it -- and callers must treat that as "no data", never as a valid zero byte.
 *
 * ⛔ A DIFFERENT WIDTH OF READ THAN nes_read. NesMemoryManager::DebugRead would answer about the CPU
 * bus; this answers about the PPU's own memory. Conflating them makes a reader misidentify a screen.
 */
uint8_t nes_read_nametable(NesCore *core, uint32_t addr);
/* Cartridge PRG ROM, for readers that decode game data straight out of the cartridge (they ask for
 * it with an explicit "PRG ROM" domain). Returns 0 past the end of the ROM. */
uint8_t nes_read_prg_rom(NesCore *core, uint32_t addr);

/* Point the core at a reader script directory (holding oga_bootstrap.lua or the reader's entry
 * file). Must outlive the core or until replaced. Pass NULL/"" to run without a reader. */
void nes_set_script_dir(NesCore *core, const char *dir);

/* True once a reader script has loaded and not yet errored. */
bool nes_script_loaded(NesCore *core);

#ifdef __cplusplus
}
#endif

#endif /* MESEN_CORE_H */
