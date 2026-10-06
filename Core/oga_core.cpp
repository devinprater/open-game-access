/*
 * oga_core.cpp — the Game Boy and PSP op tables, and the backend resolver.
 *
 * See oga_core.h for why the interface exists. This file is deliberately the
 * ONLY place that names a backend's own functions, so the "which emulator runs
 * what" decision and the "how do I drive it" decision both live in one screen
 * of code instead of being re-derived at ~100 call sites in pokecore.cpp.
 *
 * The DS backend's ops live in pokecore.cpp, because they need PokeCore's
 * private type (the melonDS NDS, the Lua state, the adapter registry).
 */
#include "oga_core.h"

#include "gba_core.h"
#include "psp_core.h"
#include "mesen_core.h"

// For POKE_BTN_* — the app's shared pad numbering, which the PSP's button
// translation table below is indexed by. This header is stdlib-only and is on
// every include path that compiles this file (OGA_GLUE runs on all three
// targets), so it costs nothing to depend on.
#include "pokecore.h"

#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------- Game Boy
 *
 * mGBA's GBA/GB/GBC backend. Everything here is a straight forward to
 * gba_core.h — the shapes already matched, which is the whole point.
 */

static bool GbaStart(void* s)        { return gba_start((GbaCore*) s); }
static void GbaStop(void* s)         { gba_stop((GbaCore*) s); }
static bool GbaFrame(void* s)        { return gba_frame((GbaCore*) s); }

static bool GbaRead(void* s, uint32_t addr, int width, uint32_t* out)
{
    GbaCore* gba = (GbaCore*) s;
    if (!gba || (width != 1 && width != 2 && width != 4)) return false;
    *out = gba_debug_read(gba, addr, width);
    return true;
}

static bool GbaFramebuffer(void* s, int screen, int* w, int* h, const uint8_t** pixels)
{
    (void) screen;   /* one 240x160 panel; top/bottom is an NDS idea */
    GbaCore* gba = (GbaCore*) s;
    if (!gba || !gba_framebuffer(gba, w, h)) return false;
    *pixels = gba_framebuffer_ptr(gba);
    return *pixels != NULL;
}

static void GbaSetButton(void* s, int pad_button, bool down)
{
    /* Pad indices 0-9 match mGBA's own key order (A,B,Select,Start,Right,Left,
     * Up,Down,R,L). X/Y (10/11) do not exist on a Game Boy and are dropped:
     * silently ignoring is right here, because the UI never offers them (the
     * registry lists two face buttons for this console). */
    if (pad_button < 0 || pad_button >= GBA_BTN_COUNT) return;
    gba_set_button((GbaCore*) s, pad_button, down);
}

static void GbaSetHotkey(void* s, const char* key, bool down)
{
    if (key && *key) gba_set_hotkey((GbaCore*) s, key, down);
}

static void GbaSetScriptDir(void* s, const char* dir)
{
    if (dir && *dir) gba_set_script_dir((GbaCore*) s, dir);
}

static bool GbaSaveState(void* s, const char* path) { return gba_save_state((GbaCore*) s, path); }
static bool GbaLoadState(void* s, const char* path) { return gba_load_state((GbaCore*) s, path); }
static unsigned long long GbaFrames(void* s) { return gba_frames_completed((GbaCore*) s); }
static const char* GbaLastError(void* s)     { return gba_last_error((GbaCore*) s); }

static const OgaCoreOps kGbaOps = {
    "gba",
    GbaStart, GbaStop, GbaFrame,
    NULL,                       /* tick: nothing per-frame */
    GbaRead,
    NULL, NULL,                 /* attach/on_frame: no adapter layer on this path */
    GbaFramebuffer,
    GbaSetButton, NULL, NULL,   /* no analog, no touch, hotkeys yes */
    GbaSetHotkey,
    GbaSetScriptDir,
    NULL,                       /* no audio path yet (reader cues are text) */
    GbaSaveState, GbaLoadState,
    GbaFrames, GbaLastError,
};

OgaCore oga_gba_core(GbaCore* gba)
{
    OgaCore c = { &kGbaOps, gba };
    return c;
}

/* ------------------------------------------------------------------------ NES
 *
 * MesenCE. Same story as GBA: the shapes came from mesen_core.h, which was written from the
 * backend contract rather than from Mesen's own API, so this table is thin on purpose.
 *
 * ⛔ WHAT IS NULL HERE, AND WHY. No attach/on_frame (the NES core has no adapter layer of its own --
 * Core/nes_adapter.cpp reads through the Host this core feeds, same as every other adapter). No
 * tick (nothing to flush per frame; battery save happens on stop). No analog, no touch (a NES pad
 * has neither). No audio: nes_read_audio returns 0 and says so, and a NULL here would say the same
 * thing less clearly, so it is wired to the real function.
 */

static bool NesStart(void* s) { return nes_start((NesCore*) s); }
static void NesStop(void* s)  { nes_stop((NesCore*) s); }
static bool NesFrame(void* s) { return nes_frame((NesCore*) s); }

static bool NesRead(void* s, uint32_t addr, int width, uint32_t* out)
{
    return nes_read((NesCore*) s, addr, width, out);
}

static bool NesFramebuffer(void* s, int screen, int* w, int* h, const uint8_t** pixels)
{
    (void) screen;   /* one 256x240 panel; top/bottom is an NDS idea */
    NesCore* nes = (NesCore*) s;
    if (!nes || !nes_framebuffer(nes, w, h)) return false;
    *pixels = nes_framebuffer_ptr(nes);
    return *pixels != NULL;
}

static void NesSetButton(void* s, int pad_button, bool down)
{
    /* The app's shared pad indices for a two-face-button console: 0=A, 1=B, 2=Select, 3=Start
     * (the POKE_BTN_* order mesen_core.h documents), and 4..7 for the d-pad. nes_core maps those
     * to the controller's own bit order; anything above the console's count is dropped, because
     * the UI never offers it (the registry lists no X/Y for this console). */
    if (pad_button < 0 || pad_button >= NES_BTN_COUNT) return;
    nes_set_button((NesCore*) s, pad_button, down);
}

/* ⛔ THE READER NEEDS A SCRIPT DIRECTORY. nes_set_script_dir exists and loads the mod; a NULL slot
 * here is the difference between "the console runs" and "the reader talks". */
static void NesSetScriptDir(void* s, const char* dir) {
    if (dir && *dir) nes_set_script_dir((NesCore*) s, dir);
}

static bool NesSaveState(void* s, const char* path) { return nes_save_state((NesCore*) s, path); }
static bool NesLoadState(void* s, const char* path) { return nes_load_state((NesCore*) s, path); }
static unsigned long long NesFrames(void* s) { return nes_frames_completed((NesCore*) s); }
static const char* NesLastError(void* s)     { return nes_last_error((NesCore*) s); }

static const OgaCoreOps kNesOps = {
    "nes",                      /* id */
    NesStart, NesStop, NesFrame,
    NULL,                       /* tick: nothing to flush per frame (battery save happens on stop) */
    NesRead,
    NULL, NULL,                 /* attach, on_frame: no adapter layer on this path */
    NesFramebuffer,
    NesSetButton,               /* set_button */
    NULL,                       /* set_analog: a NES pad has no stick */
    NULL,                       /* set_touch:  no touchscreen */
    NULL,                       /* set_hotkey: none defined for this console yet */
    NesSetScriptDir,            /* set_script_dir: loads the NES reader mod */
    NULL,                       /* read_audio: nes_read_audio exists but returns 0 by design; a NULL
                                 * slot says the same thing, so wire it when there is real audio */
    NesSaveState, NesLoadState, /* both refuse out loud today -- see mesen_core.cpp */
    NesFrames, NesLastError,
};

OgaCore oga_nes_core(NesCore* nes)
{
    OgaCore c = { &kNesOps, nes };
    return c;
}

/* ------------------------------------------------------------------------ PSP
 *
 * PPSSPP. Same story: the shapes came from psp_core.h.
 */

static bool PspStart(void* s) { return psp_start((PspCore*) s); }
static void PspStop(void* s)  { psp_stop((PspCore*) s); }
static bool PspFrame(void* s) { return psp_frame((PspCore*) s); }

static bool PspRead(void* s, uint32_t addr, int width, uint32_t* out)
{
    PspCore* psp = (PspCore*) s;
    if (!psp || (width != 1 && width != 2 && width != 4)) return false;
    *out = psp_debug_read(psp, addr, width);
    return true;
}

static bool PspFramebuffer(void* s, int screen, int* w, int* h, const uint8_t** pixels)
{
    (void) screen;   /* one 480x272 panel */
    PspCore* psp = (PspCore*) s;
    if (!psp || !psp_framebuffer(psp, w, h)) return false;
    *pixels = psp_framebuffer_ptr(psp);
    return *pixels != NULL;
}

/* The app's shared pad indices, translated to PPSSPP's own button order.
 * Kept as a table rather than arithmetic because the two orderings genuinely
 * differ (PSP starts at Select, not A) and a wrong index is a wrong button. */
static const int kPadToPsp[POKE_BTN_COUNT] = {
    PSP_BTN_CROSS,    /* A */
    PSP_BTN_CIRCLE,   /* B */
    PSP_BTN_SELECT,   /* Select */
    PSP_BTN_START,    /* Start */
    PSP_BTN_RIGHT,    /* Right */
    PSP_BTN_LEFT,     /* Left */
    PSP_BTN_UP,       /* Up */
    PSP_BTN_DOWN,     /* Down */
    PSP_BTN_R,        /* R */
    PSP_BTN_L,        /* L */
    PSP_BTN_TRIANGLE, /* X */
    PSP_BTN_SQUARE,   /* Y */
};

static void PspSetButton(void* s, int pad_button, bool down)
{
    /* The PSP has four face buttons where the DS has four, so X/Y map onto
     * Triangle/Square rather than being dropped. */
    if (pad_button < 0 || pad_button >= POKE_BTN_COUNT) return;
    psp_set_button((PspCore*) s, kPadToPsp[pad_button], down);
}

static void PspSetAnalog(void* s, float x, float y) { psp_set_analog((PspCore*) s, x, y); }

static int PspReadAudio(void* s, int16_t* out, int max_frames)
{
    return psp_read_audio((PspCore*) s, out, max_frames);
}

static bool PspSaveState(void* s, const char* path) { return psp_save_state((PspCore*) s, path); }
static bool PspLoadState(void* s, const char* path) { return psp_load_state((PspCore*) s, path); }
static unsigned long long PspFrames(void* s) { return psp_frames_completed((PspCore*) s); }
static const char* PspLastError(void* s)     { return psp_last_error((PspCore*) s); }

static const OgaCoreOps kPspOps = {
    "psp",
    PspStart, PspStop, PspFrame,
    NULL,
    PspRead,
    NULL, NULL,
    PspFramebuffer,
    PspSetButton, PspSetAnalog, NULL,   /* no touch screen */
    NULL,                               /* no hotkey layer on PSP */
    NULL,                               /* no external script set on PSP */
    PspReadAudio,
    PspSaveState, PspLoadState,
    PspFrames, PspLastError,
};

OgaCore oga_psp_core(PspCore* psp)
{
    OgaCore c = { &kPspOps, psp };
    return c;
}

/* ------------------------------------------------------------------ resolver */

/* Lower-cased extension INCLUDING the dot, or empty. Mirrors systems.cpp's
 * NormalizeExt, but is deliberately independent: the registry answers "which
 * console is this", this answers "can we boot it", and folding them together
 * would mean a console with no core could not be named by the UI. */
static void ExtOf(const char* path, char* out, int out_size)
{
    out[0] = '\0';
    if (!path || out_size < 2) return;
    const char* dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return;
    int n = 0;
    for (const char* p = dot; *p && *p != '/' && *p != '\\' && n < out_size - 1; p++, n++)
        out[n] = (char) tolower((unsigned char) *p);
    out[n] = '\0';
}

static bool ExtIs(const char* ext, const char* const* list)
{
    for (int i = 0; list[i]; i++) if (strcmp(ext, list[i]) == 0) return true;
    return false;
}

const OgaResolvedBackend* oga_resolve_backend(const char* rom_path)
{
    static const OgaResolvedBackend kNds = { "nds", false, false };
    static const OgaResolvedBackend kGba = { "gba", true,  false };
    static const OgaResolvedBackend kPsp = { "psp", false, true  };
    static const OgaResolvedBackend kNes = { "nes", false, false, true };

    static const char* const kGb[]  = { ".gba", ".gb", ".gbc", NULL };
    static const char* const kPspE[] = { ".iso", ".cso", ".pbp", ".elf", ".prx", ".ppdmp", NULL };
    /* A bare .nds/.dsi/.srl is ours; anything else is NOT the DS. This is the
     * rule that stops a .gba booting melonDS because "default to DS" was the
     * convenient choice. */
    static const char* const kNdsE[] = { ".nds", ".dsi", ".srl", NULL };
    /* Mesen's NES core. ⛔ .unf (UNIF) and .fds (Famicom Disk System) are NES-family formats the
     * core handles; they belong to this row and to no other. */
    static const char* const kNesE[] = { ".nes", ".fds", ".unf", NULL };

    char ext[8];
    ExtOf(rom_path, ext, (int) sizeof(ext));
    if (ext[0] == '\0') return NULL;

    if (ExtIs(ext, kGb))   return &kGba;
    if (ExtIs(ext, kPspE)) return &kPsp;
    if (ExtIs(ext, kNdsE)) return &kNds;
    if (ExtIs(ext, kNesE)) return &kNes;
    return NULL;
}
