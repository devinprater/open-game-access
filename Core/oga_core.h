/*
 * oga_core.h — the one backend interface every emulator core implements.
 *
 * WHY THIS EXISTS
 *
 * The app grew three emulator backends the way apps do: one at a time, each
 * bolted onto the entry points it needed. That produced Core/pokecore.cpp with
 * ~100 dispatch sites — `if (core->isGba) ... if (core->isPsp) ...` repeated in
 * poke_frame, poke_set_button, poke_framebuffer, poke_touch, poke_read_audio,
 * poke_save_state, poke_load_state, and a dozen more — plus two booleans
 * (isGba, isPsp) that every one of them had to agree about.
 *
 * The tell that this was already an interface trying to be born: Core/gba_core.h
 * and Core/psp_core.h independently converged on the SAME twenty-odd function
 * shapes — load, start, stop, frame, framebuffer, button, save/load state,
 * speech callback, last error. Nobody designed that; it is what a backend
 * obviously needs. This file writes it down once.
 *
 * ⛔ WHAT THIS IS NOT. It is not an emulator abstraction and it does not try to
 * be one. Every member below exists because TWO OR THREE REAL BACKENDS needed
 * it. There is no `vblank()`, no `set_region()`, no `get_cpu_state()` — add a
 * member only when a second backend actually implements it. A vtable with
 * speculative members is how a shell becomes a framework, and this project has
 * exactly the consoles it can boot.
 *
 * The DS backend additionally owns the adapter + Lua-script subsystem, which
 * the Game Boy and PSP backends do not have. That asymmetry is real and is
 * expressed honestly: the NDS ops populate `attach`, `on_frame`, `read` and
 * `tick`; the others leave those NULL and the core checks. Making a console
 * pretend to have an adapter layer it lacks would be worse than a null check.
 */
#ifndef OGA_CORE_H
#define OGA_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations: a backend is opaque to everything but its own file. */
typedef struct GbaCore GbaCore;
typedef struct PspCore PspCore;

struct OgaCore;

/*
 * One backend's implementation.
 *
 * `state` is whatever the backend needs — the core object itself, in practice.
 * Every function takes it because an ops table is a static const: there is one
 * of each per backend, shared by every live core, so it cannot carry per-core
 * data itself.
 *
 * A NULL member means "this console does not have that idea". The core treats
 * NULL as a refusal, never as a silent success: a caller that got `false` or 0
 * can tell the difference between "no" and "quietly did nothing".
 */
typedef struct OgaCoreOps {
    /* Short id for logs and diagnostics ("nds", "gba", "psp"). */
    const char* id;

    /* ---- lifecycle ---- */

    /* Boot the loaded ROM. The Game Boy backend starts the reader here; the DS
     * backend starts the console and the Lua script. */
    bool (*start)(void* state);
    void (*stop)(void* state);
    /* Run exactly one emulated frame. Returns false when the console stopped
     * itself. Does NOT increment the core's frame counter or run the
     * per-frame adapter hook — the core does both, once, for every backend. */
    bool (*frame)(void* state);
    /* Per-frame bookkeeping after the frame counter has advanced. The DS uses
     * it to flush its save once a second; leave NULL when there is nothing to
     * do. `frame_index` is the count AFTER the frame that just ran. */
    void (*tick)(void* state, uint64_t frame_index);

    /* ---- memory reads (the adapter Host path) ----
     *
     * Reads a scalar out of the console's address space. Returns false when the
     * address is not readable — an adapter must be able to tell "0" from
     * "outside the map", or it will narrate garbage. */
    bool (*read)(void* state, uint32_t addr, int width, uint32_t* out);

    /* ---- game adapters ----
     *
     * Only the DS backend implements these. `attach` is called once, lazily,
     * the first time an adapter is asked for anything: an adapter's reads are
     * only meaningful after the game has allocated its own structures, which
     * happens some way into boot. */
    void (*attach)(void* state);
    void (*on_frame)(void* state);

    /* ---- video ----
     *
     * Fills the core's staging buffer and reports its size. `screen` selects
     * the DS's top or bottom panel and is ignored by one-screen consoles.
     * `pixels` points INTO THE BACKEND's own framebuffer; the core copies it,
     * because the backend's pointer is only valid until the next frame. */
    bool (*framebuffer)(void* state, int screen, int* width, int* height,
                        const uint8_t** pixels);

    /* ---- input ----
     *
     * Pad indices are the app's shared numbering (POKE_BTN_*), NOT the
     * backend's: the backend translates. That is what lets the UI send the same
     * numbers for every console and only change the labels. */
    void (*set_button)(void* state, int pad_button, bool down);
    void (*set_analog)(void* state, float x, float y);
    /* Touch. NULL = this console has no touch screen. */
    void (*set_touch)(void* state, int x, int y, bool down);
    /* Script hotkey letters. NULL = no hotkey layer (the native adapters are
     * the only reader there, so a dead key would be worse than none). */
    void (*set_hotkey)(void* state, const char* key, bool down);

    /* Point the backend at the Lua reader set that loads its own files. Only
     * the Game Boy backend has one: its reader is ~174 files loaded with
     * loadfile, while the DS reader is a single concatenated script string.
     * NULL = this backend takes its script some other way. */
    void (*set_script_dir)(void* state, const char* dir);

    /* ---- audio ----
     *
     * Interleaved stereo s16 at the app's sample rate. Returns frames written.
     * NULL = no audio path from this console yet. */
    int (*read_audio)(void* state, int16_t* out, int max_frames);

    /* ---- savestates ---- */
    bool (*save_state)(void* state, const char* path);
    bool (*load_state)(void* state, const char* path);

    /* ---- diagnostics ---- */
    unsigned long long (*frames_completed)(void* state);
    const char* (*last_error)(void* state);
} OgaCoreOps;

/* A live backend: which ops, and the state they act on. */
typedef struct OgaCore {
    const OgaCoreOps* ops;
    void* state;
} OgaCore;

/* ------------------------------------------------------------------ backends
 *
 * These build an OgaCore around an already-created backend. They live in
 * Core/oga_core.cpp. The DS backend cannot be built here — its ops need the
 * private PokeCore type, so Core/pokecore.cpp owns them.
 */

OgaCore oga_gba_core(GbaCore* gba);
/* NesCore is opaque here on purpose: oga_core.h must not pull in Mesen. */
typedef struct NesCore NesCore;
OgaCore oga_nes_core(NesCore* nes);
OgaCore oga_psp_core(PspCore* psp);

/* Which backend runs a file, by extension — or NULL when this build has none.
 *
 * ⛔ THIS IS THE ONE PLACE THAT DECIDES "which emulator runs this ROM". It used
 * to be decided twice: once by the extension checks in pokecore.cpp and once by
 * the UI's own switch, which is how a .gba came to be offered to melonDS.
 * Adding a console is now a case here plus an ops table. */
typedef struct OgaResolvedBackend {
    const char* id;              /* "nds", "gba", "psp" */
    bool gba_hint;               /* the file should reach the Game Boy backend */
    bool psp_hint;
    bool nes_hint;               /* the file should reach the Mesen NES backend */
} OgaResolvedBackend;

/* Resolves a ROM path to a backend id, or NULL when unsupported.
 * `.iso`/`.cso`/`.pbp` are ambiguous by nature (PSP and PS1 both use them);
 * this function returns the PSP, which is the only one of the two this build
 * can boot, and the core confirms by the disc header after loading. */
const OgaResolvedBackend* oga_resolve_backend(const char* rom_path);

#ifdef __cplusplus
}
#endif

#endif /* OGA_CORE_H */
