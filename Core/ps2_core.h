/* ps2_core.h — C ABI for the Sony PlayStation 2 cores (Play! and ARMSX2).
 *
 * Mirrors ps1_core.h deliberately: the app already knows how to drive a
 * core with this contract (create/load/start/frame/buttons/audio/states),
 * so the PlayStation 2 speaks the same language and pokecore only branches
 * on the resolved backend.
 *
 * THE CORES, AND WHY THEY ARE dlopen()ed. Two libretro cores can run a
 * PlayStation 2: Play! (MIT, its own HLE BIOS — no BIOS file needed) and
 * ARMSX2 (the PCSX2 lineage, GPL-3 — needs a real PS2 BIOS from the
 * player's own console). Both are enormous C++ trees whose weak symbols
 * (templates, vtables, their own copies of libchdr/zstd/fmt) cannot be
 * prefixed safely the way PCSX ReARMed's plain C was, so this wrapper does
 * NOT link them. It dlopen()s a pinned, hash-checked .so at run time
 * (RTLD_LOCAL, so a core's symbols stay its own) and drives it through the
 * plain libretro API. The artifact pins live in scripts/bootstrap-deps.sh.
 *
 * WHICH CORE. ps2_set_core_path names one .so exactly. Otherwise the
 * wrapper searches the folder ps2_set_core_dir names, in this order:
 *   1. the ARMSX2 core, when a PS2 BIOS is present in the BIOS folder
 *      (ps2_set_bios_dir) — the accurate core gets the games it can run;
 *   2. the Play! core — it boots with no BIOS at all, so something always
 *      runs;
 *   3. any other *_libretro*.so in the folder, by name order (a core the
 *      player supplied; retro_get_system_info's library_name, never the
 *      file name, is what ps2_core_name reports).
 * File names searched include the buildbot's (play_libretro_android.so,
 * armsx2_libretro_android.so) and the Android jniLibs spellings
 * (libplay_libretro.so, libarmsx2_libretro.so).
 *
 * THE BIOS. Play! never needs one. ARMSX2 reads its BIOS from the system
 * folder this wrapper hands it: ps2_set_bios_dir points at a folder laid
 * out the way the PCSX2 lineage expects — the BIOS images under
 * <dir>/pcsx2/bios/. No BIOS file ships with the app (it is Sony's
 * firmware); the app stages the player's own dump there.
 *
 * VIDEO IS HARDWARE-RENDERED. Both cores draw through OpenGL
 * (RETRO_ENVIRONMENT_SET_HW_RENDER); there is no CPU framebuffer to copy
 * the way PCSX ReARMed offers. This wrapper therefore owns a headless GL
 * context of its own (EGL, surfaceless, an FBO it reads back each frame)
 * and hands the app the same RGBA8888 frames ps1_framebuffer hands it —
 * the app cannot tell the difference, and the readback cost is measured
 * and stated in the harness numbers rather than hidden. On a platform
 * with no EGL the wrapper still loads, but a core that demands HW render
 * is refused in words at load: a PS2 that cannot draw must not pretend.
 *
 * THREADS. Unlike the PS1 core, these cores run their own worker threads
 * (EE/GS). The app still drives frames synchronously through ps2_frame;
 * the audio queue is mutex-guarded exactly as the PS1 wrapper's is.
 *
 * DISCS. .iso (the common case), .chd (both cores carry a CHD reader;
 * DVD CHDs are the 2048-byte-sector kind chdman's createdvd writes),
 * .cso and .isz (Play!). Identity is the disc's own serial:
 * ps2_load_disc parses SYSTEM.CNF's BOOT2 line out of a plain .iso
 * ("SLUS20998"); a CHD's serial is not read (""), because nothing in the
 * image is readable without decompressing it, and the core does not
 * publish one.
 *
 * MEMORY CARDS. The cores own them: a card is a file the core keeps in
 * the save folder (ps2_load_disc's save_path is a DIRECTORY for PS2 —
 * the one deliberate difference from ps1_load_disc, whose save_path is
 * the card file itself, because PCSX ReARMed's card is libretro-managed
 * and these are not). The wrapper creates the folder when missing and
 * never touches the card's bytes.
 */
#ifndef PS2_CORE_H
#define PS2_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DualShock 2 button ids: indices into this core's own table, the same
 * order as the PlayStation's (ps1_core.h). The DualShock 2's buttons are
 * pressure-sensitive on the real pad; they are exposed as digital here —
 * the games this project targets read them digitally, and a pressure
 * value can be added behind ps2_set_button_pressure without moving an
 * index. The PlayStation 2 backend in Core/oga_core.cpp translates the
 * app's shared POKE_BTN_* indices to these. */
#define PS2_BTN_CROSS     0
#define PS2_BTN_CIRCLE    1
#define PS2_BTN_TRIANGLE  2
#define PS2_BTN_SQUARE    3
#define PS2_BTN_L1        4
#define PS2_BTN_R1        5
#define PS2_BTN_L2        6
#define PS2_BTN_R2        7
#define PS2_BTN_SELECT    8
#define PS2_BTN_START     9
#define PS2_BTN_UP        10
#define PS2_BTN_DOWN      11
#define PS2_BTN_LEFT      12
#define PS2_BTN_RIGHT     13
#define PS2_BTN_L3        14
#define PS2_BTN_R3        15
#define PS2_BTN_COUNT     16

/* EE main RAM: 32 MiB at physical 0x00000000..0x01FFFFFF. The CPU also
 * sees it through the KSEG0 (0x80000000) and KSEG1 (0xA0000000) segment
 * mirrors and the 0x20000000 / 0x30000000 uncached aliases; ps2_read
 * accepts any of those spellings. It is NOT mirrored past 32 MiB the way
 * the PS1's RAM is — an address at or past 0x02000000 physical is outside
 * the map and refuses. The 16 KiB scratchpad sits at 0x70000000, outside
 * every segment mask, and is readable when the core publishes it. */
#define PS2_RAM_SIZE        0x2000000u
#define PS2_SCRATCH_BASE    0x70000000u
#define PS2_SCRATCH_SIZE    0x4000u

/* The IOP's own 2 MiB of RAM, a separate address space the EE cannot
 * address directly. ps2_read_iop takes a plain offset into it. Not every
 * core publishes it; when one does not, the read returns false. */
#define PS2_IOP_RAM_SIZE    0x200000u

/* The rate ps2_read_audio delivers (the app's mixer rate, the same
 * contract as poke_read_audio). The core's own rate is read from its AV
 * info at load (44100 from Play!, 48000 from the PCSX2 lineage) and the
 * wrapper resamples from whatever it actually is. */
#define PS2_APP_AUDIO_RATE  32768

typedef struct Ps2Core Ps2Core;

/* One live core at a time, as with the PS1: the libretro callbacks carry
 * no user pointer, so they reach the one live wrapper through a global.
 * A second ps2_create while one lives returns NULL — and, unlike the
 * PS1 wrapper, says why: ps2_create_error() holds the refusal in words
 * ("" when the last create succeeded or none was refused). */
Ps2Core *ps2_create(void);
const char *ps2_create_error(void);
void ps2_destroy(Ps2Core *core);

/* Optional, before load: name the core .so exactly, or the folder to
 * search (see WHICH CORE above), and the BIOS folder (see THE BIOS
 * above). An empty BIOS folder means no BIOS: Play! still boots, ARMSX2
 * will refuse in the core's own words. */
void ps2_set_core_path(Ps2Core *core, const char *path);
void ps2_set_core_dir(Ps2Core *core, const char *dir);
void ps2_set_bios_dir(Ps2Core *core, const char *dir);

/* Load a disc image (see DISCS above). save_path is the save DIRECTORY
 * (memory cards live there; see MEMORY CARDS above); it is created when
 * missing. Empty or NULL = a scratch folder under the system temp dir.
 * On success writes the NUL-terminated disc serial into code_out (""
 * when the image's serial could not be read). Refuses, in words, a file
 * the core cannot read as a PlayStation 2 disc. */
bool ps2_load_disc(Ps2Core *core, const char *disc_path, const char *save_path,
                   char code_out[16]);

bool ps2_start(Ps2Core *core);
void ps2_stop(Ps2Core *core);
bool ps2_running(Ps2Core *core);

/* One emulated frame (retro_run). Synchronous; false means "not running". */
bool ps2_frame(Ps2Core *core);

/* Video. Pixels are RGBA8888, read back from the wrapper's GL framebuffer;
 * valid until the next frame. The size changes with the game's video mode. */
bool ps2_framebuffer(Ps2Core *core, int *width, int *height);
const uint8_t *ps2_framebuffer_ptr(Ps2Core *core);

void ps2_set_button(Ps2Core *core, int ps2_button, bool down);

/* The DualShock 2's sticks: stick 0 is the left, 1 the right; each axis
 * -1..1, +x right, +y down. */
#define PS2_STICK_LEFT  0
#define PS2_STICK_RIGHT 1
void ps2_set_analog(Ps2Core *core, int stick, float x, float y);

/* The loaded disc's frame rate (the core's own timing: about 59.94 for
 * NTSC, 50 for PAL), or 0 before a load. */
double ps2_frame_rate(Ps2Core *core);

/* Interleaved stereo s16 at PS2_APP_AUDIO_RATE. Returns frames written. */
int ps2_read_audio(Ps2Core *core, int16_t *out, int max_frames);

/* Whole-machine states (retro_serialize). A core may not support them —
 * Play!'s are about 40 MiB — so support is probed at load: when the core
 * reports a serialize size of 0, both calls refuse in words rather than
 * pretending. */
bool ps2_save_state(Ps2Core *core, const char *path);
bool ps2_load_state(Ps2Core *core, const char *path);

unsigned long long ps2_frames_completed(Ps2Core *core);
const char *ps2_last_error(Ps2Core *core);

/* The loaded core's own name, from retro_get_system_info ("Play!",
 * "ARMSX2"), or "" before a load. Never the .so's file name. */
const char *ps2_core_name(Ps2Core *core);

/* True when the running machine uses an HLE BIOS: always true for Play!
 * (it has no other kind), false for ARMSX2 once it has a real BIOS. */
bool ps2_using_hle_bios(Ps2Core *core);

/* Read-only memory read for a future adapter Host path. width is 1, 2
 * or 4; the Emotion Engine is little-endian, and a read returns the
 * value as the CPU sees it. EE RAM (any mirror or alias, see
 * PS2_RAM_SIZE) and the scratchpad only; anything else returns false
 * ("outside the map", never a fabricated zero): reading hardware
 * registers has side effects on the real machine. */
bool ps2_read(Ps2Core *core, uint32_t addr, int width, uint32_t *out);

/* The IOP's RAM, by plain offset (see PS2_IOP_RAM_SIZE). Returns false
 * when the loaded core does not publish the region. */
bool ps2_read_iop(Ps2Core *core, uint32_t offset, int width, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PS2_CORE_H */
