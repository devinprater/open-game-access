/*
 * systems.h — the emulator-agnostic system registry.
 *
 * WHY THIS EXISTS
 *
 * Everything in this app was built around one console, and the console leaked
 * everywhere: the button table, the screen count, the ROM picker, the settings
 * screen. Adding a second console meant editing all of them, so in practice
 * only the DS ever worked properly.
 *
 * This file is the single source of truth for "what systems does this app know
 * about, and what is each one like" — the hardware facts only. Both the iOS UI
 * and the Android UI read from here instead of hardcoding a switch, so a new
 * console is a row in this table plus a backend, not a hunt through the app.
 *
 * ⛔ HARDWARE FACTS ONLY. Script facts do not belong here.
 *
 * The distinction matters and got this wrong once already: the iOS GameSystem
 * enum mixes physical facts (the DS has two screens) with facts about a
 * particular LUA SCRIPT (which letter repeats speech). Those are different
 * lifetimes. Hardware is fixed for a console; a script key changes when
 * somebody edits the script, and a console can have several scripts or none.
 * Script/hotkey tables live with the adapter layer, keyed by the loaded game —
 * not here. Putting them here would mean the registry has to know about
 * Pokémon Access specifically, which is exactly the coupling this file exists
 * to remove.
 *
 * Adding a console that has no emulator backend yet is FINE and is the point:
 * the system can be listed and selected while its core is still being written,
 * so the UI and the cores can be worked on in either order.
 */
#ifndef OGA_SYSTEMS_H
#define OGA_SYSTEMS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System ids. Append only: the values cross the C ABI to Swift, so renumbering
 * them silently mislabels every button in a shipped build. */
typedef enum {
    OGA_SYS_UNKNOWN = 0,
    OGA_SYS_DS = 1,            /* Nintendo DS / DSi */
    OGA_SYS_GB = 2,            /* Game Boy / Game Boy Color */
    OGA_SYS_GBA = 3,           /* Game Boy Advance */
    OGA_SYS_PSP = 4,           /* PlayStation Portable */
    OGA_SYS_NES = 5,           /* Nintendo Entertainment System / Famicom */
    OGA_SYS_SNES = 6,          /* Super Nintendo */
    OGA_SYS_N64 = 7,           /* Nintendo 64 */
    OGA_SYS_PS1 = 8,           /* PlayStation */
    OGA_SYS_PS2 = 9,           /* PlayStation 2 */
    OGA_SYS_GAMECUBE = 10,     /* GameCube */
    OGA_SYS_WII = 11,          /* Wii */
    OGA_SYS_GENESIS = 12,      /* Sega Genesis / Mega Drive */
    OGA_SYS_DREAMCAST = 13,    /* Sega Dreamcast */
    OGA_SYS_3DS = 14,          /* Nintendo 3DS */
} OgaSystemId;

/* How far along a system is. The UI uses this to decide whether to allow a
 * file to be picked at all, and to say WHY when it does not.
 *
 * The point of the distinction: a system with no backend is not an error, it
 * is work not yet done, and the player deserves to be told which one they are
 * looking at instead of getting a silent failure or a mislabeled pad. */
typedef enum {
    OGA_BACKEND_NONE = 0,      /* no emulator in this build yet */
    OGA_BACKEND_PLANNED = 1,   /* a core is chosen, not yet integrated */
    OGA_BACKEND_READY = 2,     /* a core is present and can load a ROM */
} OgaBackendState;

/* One face button on a console's pad. `raw` is the shared pad index the C core
 * maps into whichever backend is live, so the UI sends the same numbers
 * everywhere and only the labels change. */
typedef struct {
    const char* title;         /* "A", "Cross" */
    const char* symbol;        /* SF Symbol name (iOS); Android ignores it */
    const char* hint;          /* VoiceOver/TalkBack hint, one short clause */
    int raw;                   /* shared pad index, see POKE_BTN_* */
} OgaButton;

/* A console's hardware facts. Everything here is fixed for the console and
 * never depends on which game or script is loaded. */
typedef struct {
    OgaSystemId id;
    const char* name;              /* "Nintendo DS" — what the player hears */
    const char* const* extensions; /* lower-case, WITH the dot; NULL-terminated */
    OgaBackendState backend;

    int screenCount;               /* 2 only on the DS family */
    bool hasShoulders;             /* L/R, or L1/R1 etc. */
    bool hasTouch;                 /* only the DS family */
    int analogSticks;              /* 0 = d-pad only */
    bool hasDpad;

    const OgaButton* faceButtons;  /* NULL when the pad is unusual, see below */
    int faceButtonCount;
} OgaSystem;

/* The system for a ROM path or bare filename, by extension. NULL when the
 * extension is unknown — callers must treat that as "cannot run this", never
 * as "assume the DS". That default is what made a .gba try to boot melonDS. */
const OgaSystem* oga_system_for_path(const char* path);

/* Same, by extension alone ("nds" or ".nds" both accepted). */
const OgaSystem* oga_system_for_extension(const char* ext);

const OgaSystem* oga_system_by_id(OgaSystemId id);

/* Every known system, in table order. Writes the count to out_count when
 * non-NULL. Never NULL; returns an empty array for nothing. */
const OgaSystem* const* oga_all_systems(int* out_count);

/* True when this build has a backend that can actually load the system. */
bool oga_system_is_runnable(const OgaSystem* sys);

/* A phrase for a file this app cannot run, for the picker to speak. Names the
 * extension and, when the console is known but its core is not built, says so.
 * Returns a stable pointer; never NULL. */
const char* oga_unsupported_reason(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* OGA_SYSTEMS_H */
