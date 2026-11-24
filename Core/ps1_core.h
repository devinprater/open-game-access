/* ps1_core.h — C ABI for the Sony PlayStation core (PCSX ReARMed).
 *
 * Mirrors genesis_core.h deliberately: the app already knows how to drive a
 * core with this contract (create/load/start/frame/buttons/audio/states), so
 * the PlayStation speaks the same language and pokecore only branches on the
 * resolved backend.
 *
 * THE CORE. PCSX ReARMed (libretro/pcsx_rearmed), pinned in
 * scripts/bootstrap-deps.sh, Devin's pick for PS1. It is driven through its
 * own libretro frontend (frontend/libretro.c), linked in-process: that file is
 * the maintained, complete frontend for the core, so this wrapper answers the
 * libretro callbacks instead of re-implementing disc, BIOS and memory-card
 * handling. Every global the core defines carries an oga_ps1_ prefix in the
 * link (scripts/ps1-prefix-symbols.py), so its generic names cannot collide
 * with any other core's.
 *
 * THE CPU PATH, STATED PLAINLY. The interpreter, on every platform. iOS gives a
 * sideloaded app no JIT, so the dynarec could not run there anyway; Android
 * could take one later (PCSX ReARMed's ari64 or lightrec), and the switch is a
 * build flag, not an interface change. The GPU is the P.E.Op.S. software
 * renderer (plain C). No threads: the app drives frames synchronously.
 *
 * THE BIOS. A real PlayStation BIOS is the most compatible, and none ships
 * with the app (it is Sony's firmware). With no BIOS file the core runs its
 * HLE BIOS, which boots most games. ps1_set_bios_dir points the core at a
 * folder the player filled; the core picks a BIOS for the disc's region there
 * (scph5501.bin and the like) and falls back to HLE when it finds none.
 *
 * DISCS. .cue (with its .bin tracks), .chd, .m3u (multi-disc lists), .ccd,
 * .pbp (PlayStation-on-PSP EBOOTs), single-track .bin and .iso images, and
 * PS-EXE programs (.exe), which is what the host test boots.
 *
 * Identity is the disc's own serial: ps1_load_disc writes the boot serial
 * the core read from SYSTEM.CNF ("SLUS00594" — the dash dropped, as the core
 * stores it) into code_out. That is the key a PlayStation reader will be
 * registered under. It is empty for a PS-EXE, which has no disc, and for a
 * disc with no serial (the core's "SLUS99999" placeholder is not passed on).
 */
#ifndef PS1_CORE_H
#define PS1_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PlayStation pad button ids: indices into this core's own table. The
 * PlayStation backend in Core/oga_core.cpp translates the app's shared
 * POKE_BTN_* indices to these. The order is the pad's: face buttons, then
 * shoulders, then Select/Start, then the d-pad, then the stick clicks. */
#define PS1_BTN_CROSS     0
#define PS1_BTN_CIRCLE    1
#define PS1_BTN_TRIANGLE  2
#define PS1_BTN_SQUARE    3
#define PS1_BTN_L1        4
#define PS1_BTN_R1        5
#define PS1_BTN_L2        6
#define PS1_BTN_R2        7
#define PS1_BTN_SELECT    8
#define PS1_BTN_START     9
#define PS1_BTN_UP        10
#define PS1_BTN_DOWN      11
#define PS1_BTN_LEFT      12
#define PS1_BTN_RIGHT     13
#define PS1_BTN_L3        14
#define PS1_BTN_R3        15
#define PS1_BTN_COUNT     16

/* Main RAM: 2 MiB, which the CPU sees at 0x00000000 (KUSEG), 0x80000000
 * (KSEG0, where games put nearly everything) and 0xA0000000 (KSEG1), each
 * mirrored four times across the first 8 MiB. ps1_read accepts any of those
 * spellings. The 1 KiB scratchpad at 0x1F800000 is readable too. */
#define PS1_RAM_SIZE        0x200000u
#define PS1_SCRATCH_BASE    0x1F800000u
#define PS1_SCRATCH_SIZE    0x400u

/* The rate ps1_read_audio delivers (the app's mixer rate, the same contract as
 * poke_read_audio). The core itself mixes at 44100 Hz; the wrapper resamples. */
#define PS1_APP_AUDIO_RATE  32768

typedef struct Ps1Core Ps1Core;

/* One live core at a time: PCSX ReARMed is one global machine, so a second
 * ps1_create while one lives returns NULL rather than a second handle to the
 * same hardware. pokecore owns exactly one live backend at a time. */
Ps1Core *ps1_create(void);
void ps1_destroy(Ps1Core *core);

/* Optional, before load: a folder that may hold a real PlayStation BIOS. An
 * empty or missing folder means the HLE BIOS. */
void ps1_set_bios_dir(Ps1Core *core, const char *dir);

/* Load a disc image (see DISCS above). save_path is the memory card file for
 * slot 1 (a raw 128 KiB .mcd); when it exists it is restored, and the card is
 * written back on stop and before a state save whenever it changed. Empty or
 * NULL = no persistent card. On success writes the NUL-terminated disc serial
 * into code_out ("" for a PS-EXE). Refuses, in words, a file the core cannot
 * read as a PlayStation disc. */
bool ps1_load_disc(Ps1Core *core, const char *disc_path, const char *save_path,
                   char code_out[16]);

bool ps1_start(Ps1Core *core);
void ps1_stop(Ps1Core *core);
bool ps1_running(Ps1Core *core);

/* One emulated frame (retro_run). Synchronous; false means "not running". */
bool ps1_frame(Ps1Core *core);

/* Video. Pixels are RGBA8888, converted from the core's XRGB8888 output; valid
 * until the next frame. The size changes with the game's video mode. */
bool ps1_framebuffer(Ps1Core *core, int *width, int *height);
const uint8_t *ps1_framebuffer_ptr(Ps1Core *core);

void ps1_set_button(Ps1Core *core, int ps1_button, bool down);

/* The DualShock's sticks: stick 0 is the left, 1 the right; each axis -1..1,
 * +x right, +y down. The pad is a DualShock, which every digital game also
 * accepts (it starts in digital mode); a game that asks for analog gets it
 * switched on by the core, and L3 + R3 together toggle it by hand. */
#define PS1_STICK_LEFT  0
#define PS1_STICK_RIGHT 1
void ps1_set_analog(Ps1Core *core, int stick, float x, float y);

/* The loaded disc's frame rate (the core's own timing: about 59.94 for NTSC,
 * 50 for PAL), or 0 before a load. */
double ps1_frame_rate(Ps1Core *core);

/* Interleaved stereo s16 at PS1_APP_AUDIO_RATE. Returns frames written. */
int ps1_read_audio(Ps1Core *core, int16_t *out, int max_frames);

/* Whole-machine states (retro_serialize). Saving flushes the memory card
 * first, so a state and its card never disagree about which is newer. */
bool ps1_save_state(Ps1Core *core, const char *path);
bool ps1_load_state(Ps1Core *core, const char *path);

unsigned long long ps1_frames_completed(Ps1Core *core);
const char *ps1_last_error(Ps1Core *core);

/* True when the running machine uses the HLE BIOS (no BIOS file was found). */
bool ps1_using_hle_bios(Ps1Core *core);

/* Read-only memory read for the adapter Host path. width is 1, 2 or 4; the
 * R3000A is little-endian, and a read returns the value as the CPU sees it.
 * Main RAM (any mirror, see PS1_RAM_SIZE) and the scratchpad only; anything
 * else returns false ("outside the map", never a fabricated zero): reading
 * hardware registers has side effects on the real machine. */
bool ps1_read(Ps1Core *core, uint32_t addr, int width, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PS1_CORE_H */
