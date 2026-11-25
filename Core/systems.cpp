/*
 * systems.cpp — the system registry table and its lookups.
 *
 * See systems.h for why this exists. The short version: this app was built
 * around one console, and "which console" was answered by hardcoded switches
 * scattered through the UI. Those switches are the reason a second console
 * never worked.
 *
 * Each row is hardware facts only. Script/hotkey facts deliberately live with
 * the adapters, not here — see the note in systems.h.
 */
#include "systems.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

namespace {

/* Pad indices, mirroring pokecore.h. Duplicated as literals rather than
 * including the app ABI header, so this file stays usable by the Android
 * native code and by host tests that never link the app. */
enum { BTN_A = 0, BTN_B = 1, BTN_SELECT = 2, BTN_START = 3,
       BTN_RIGHT = 4, BTN_LEFT = 5, BTN_UP = 6, BTN_DOWN = 7,
       BTN_R = 8, BTN_L = 9, BTN_X = 10, BTN_Y = 11 };

/* ---- pads -------------------------------------------------------------
 *
 * A blind player cannot tell a greyed-out button from a broken one, so a
 * console lists ONLY the buttons it really has. The DS keeps X/Y; the Game
 * Boy has no X/Y equivalent and the core ignores them, so they are absent
 * rather than disabled.
 */

/* The four-button diamond, as the DS and every modern dual-analog pad has it. */
const OgaButton kDiamond[] = {
    { "A", "a.circle", "Confirm, talk, select", BTN_A },
    { "B", "b.circle", "Cancel, back",          BTN_B },
    { "X", "x.circle", "Menu",                 BTN_X },
    { "Y", "y.circle", "Use item",             BTN_Y },
};

/* Two face buttons (Game Boy, NES, and the whole Mesen 8-bit set: the
 * Master System, PC Engine and WonderSwan pads are all two-button at heart;
 * their extra buttons are Select/Start, which the shared numbering has. */
const OgaButton kTwoFace[] = {
    { "A", "a.circle", "Confirm, talk, select", BTN_A },
    { "B", "b.circle", "Cancel, back",          BTN_B },
};

/* Game Boy Advance: two face buttons plus shoulders, still no X/Y. */
const OgaButton kTwoFaceShoulders[] = {
    { "A", "a.circle", "Confirm, talk, select", BTN_A },
    { "B", "b.circle", "Cancel, back",          BTN_B },
};

/* PlayStation: the same four positions under Sony's names. Cross is confirm
 * and Circle is cancel in the Japanese convention the PSP hardware used. */
const OgaButton kPlayStation[] = {
    { "Cross",    "xmark.circle",  "Cross button, confirm",  BTN_A },
    { "Circle",   "circle",        "Circle button, cancel",  BTN_B },
    { "Triangle", "triangle",      "Triangle button",        BTN_X },
    { "Square",   "square",        "Square button",          BTN_Y },
};

/* Sega Genesis 3-button pad: A/B/C, no shoulders, no X/Y equivalent. The
 * 6-button pad's X/Y/Z map onto the shoulder/X/Y slots when a core needs them. */
const OgaButton kGenesis[] = {
    { "A", "a.circle", "A button", BTN_A },
    { "B", "b.circle", "B button", BTN_B },
    { "C", "circle",   "C button", BTN_X },
    { "X", "x.circle", "X button", BTN_L },
    { "Y", "y.circle", "Y button", BTN_Y },
    { "Z", "z.circle", "Z button", BTN_R },
};

/* Nintendo 64 (Mupen64Plus-Next, Core/n64_core.cpp). The N64 has fourteen
 * inputs and the shared pad twelve, so this row says exactly what each slot
 * does on this console, and Core/oga_core.cpp's N64SetButton is where the
 * slots become the core's buttons:
 *   L is the Z trigger and R is R, so the row has NO "L/R" shoulders of its
 *   own (hasShoulders false) and lists both here under their N64 names;
 *   X is C-left, Y is C-right, and the Select slot is C-down.
 * C-UP AND THE N64's L HAVE NO SLOT: they are not listed, because a blind
 * player cannot tell a missing button from a broken one. The stick is the
 * row's one analog stick. */
const OgaButton kN64[] = {
    { "A",       "a.circle",  "A button",                 BTN_A },
    { "B",       "b.circle",  "B button",                 BTN_B },
    { "Z",       "z.circle",  "Z trigger",                BTN_L },
    { "R",       "r.circle",  "R shoulder button",        BTN_R },
    { "C left",  "arrowtriangle.left.circle",  "C button left",  BTN_X },
    { "C right", "arrowtriangle.right.circle", "C button right", BTN_Y },
    { "C down",  "arrowtriangle.down.circle",  "C button down, also on Select", BTN_SELECT },
};

/* ---- extension lists, WITH the leading dot ---------------------------- */

const char* const kDsExt[]    = { ".nds", ".dsi", ".srl", NULL };
const char* const kGbExt[]    = { ".gb", ".gbc", NULL };
const char* const kGbaExt[]   = { ".gba", NULL };
const char* const kPspExt[]   = { ".iso", ".cso", ".pbp", ".elf", ".prx", ".ppdmp", NULL };
const char* const kNesExt[]   = { ".nes", ".fds", ".unf", ".unif", NULL };
const char* const kSnesExt[]  = { ".sfc", ".smc", ".fig", ".swc", NULL };
const char* const kN64Ext[]   = { ".n64", ".z64", ".v64", NULL };
/* The PlayStation. By NAME, first match wins: .iso and .pbp are the PSP's and
 * .bin the Genesis's, so oga_system_for_path reaches this row only for .cue,
 * .chd, .m3u and .ccd. They stay listed because they ARE PlayStation formats,
 * and oga_system_for_file (the call for a file that exists) routes a PS1
 * .iso/.pbp/.bin here by its header — see the disc sniff above. */
const char* const kPs1Ext[]   = { ".iso", ".bin", ".cue", ".chd", ".pbp", ".m3u", ".ccd", NULL };
const char* const kPs2Ext[]   = { ".iso", ".chd", NULL };
const char* const kGcExt[]    = { ".iso", ".gcm", ".rvz", ".wia", NULL };
const char* const kWiiExt[]   = { ".iso", ".wbfs", ".rvz", ".wia", NULL };
const char* const kGenesisExt[] = { ".md", ".gen", ".smd", ".bin", NULL };
/* Dreamcast disc images. Deliberately WITHOUT .chd: that extension already
 * resolves to the PlayStation row (first match wins in the table), so listing
 * it here would read as support that cannot be reached. */
const char* const kDreamcastExt[] = { ".cdi", ".gdi", NULL };
const char* const k3dsExt[]   = { ".3ds", ".cia", ".cci", ".cxi", NULL };

/* Mesen's 8-bit set. ⛔ NO `.cue` HERE. Mesen's PC Engine core accepts
 * .cue (CD titles) too, but the PlayStation row above already claims .cue
 * and FIRST MATCH WINS in this table - listing it again would advertise a
 * PC Engine path no file could ever reach. Same rule that keeps .chd off
 * Dreamcast. The Mesen SMS core also loads ColecoVision (.col); not listed
 * either, because the row is named for the Sega consoles and a mislabeled
 * row tells a blind player the wrong console name. */
#ifdef SABOTAGE_MASTER_LOSES_GG
/* The bug this switch models: a Game Gear ROM stops resolving because .gg
 * was lost from the Sega 8-bit list. systems-test.sh requires the test to
 * FAIL here, which is what proves the check is really checking. */
const char* const kMasterExt[] = { ".sms", ".sg", NULL };
#else
const char* const kMasterExt[] = { ".sms", ".gg", ".sg", NULL };
#endif
const char* const kPceExt[]    = { ".pce", ".sgx", NULL };
const char* const kWsExt[]     = { ".ws", ".wsc", NULL };

/* ---- the table --------------------------------------------------------
 *
 * backend: READY means this build has a core that can load the system.
 * PLANNED means a core is chosen but not integrated. NONE means not started.
 *
 * DS, GB, GBA and PSP are READY because their cores are in this build
 * (melonDS-lua, mGBA, PPSSPP). The rest are listed so the UI and the cores
 * can be developed in either order — the picker can speak a real answer for
 * them instead of silently ignoring the file.
 */
const OgaSystem kSystems[] = {
    { OGA_SYS_DS, "Nintendo DS", kDsExt, OGA_BACKEND_READY,
#ifndef SABOTAGE_DS_ONE_SCREEN
      /*screens*/ 2,
#else
      /*screens*/ 1,
#endif
      /*shoulders*/ true, /*touch*/ true, /*sticks*/ 0, /*dpad*/ true,
      kDiamond, 4 },

    { OGA_SYS_GB, "Game Boy", kGbExt, OGA_BACKEND_READY,
#ifdef SABOTAGE_GB_GETS_XY
      1, true, false, 0, true, kDiamond, 4 },
#else
      1, false, false, 0, true, kTwoFace, 2 },
#endif

    { OGA_SYS_GBA, "Game Boy Advance", kGbaExt, OGA_BACKEND_READY,
      1, true, false, 0, true, kTwoFaceShoulders, 2 },

    { OGA_SYS_PSP, "PlayStation Portable", kPspExt, OGA_BACKEND_READY,
      1, true, false, 1, true, kPlayStation, 4 },

    /* --- READY ---
     *
     * The MesenCE NES core is linked into the app (Core/mesen_core.cpp), a real .nes
     * boots (`scripts/nes-live-proof.sh`), and the reader path is wired end to end:
     * the core names a bundled reader set by the ROM's CRC32, the app stages a
     * writable copy of it, and `nes_set_script_dir` loads it. See §Nintendo below.
     */
    { OGA_SYS_NES, "Nintendo Entertainment System", kNesExt, OGA_BACKEND_READY,
      1, false, false, 0, true, kTwoFace, 2 },

    /* --- listed, core not yet integrated ---
     *
     * These are honest placeholders. Naming the intended core here is useful:
     * it is the answer to "what would it take", and it keeps the plan in one
     * place. Update to READY the moment a core loads a ROM.
     *
     * SNES       -> Mesen, INTEGRATED: Core/snes_core.cpp boots a ROM, runs
     *              frames, reads work RAM and is proved by
     *              scripts/snes-alttp-proof.sh (READY row below).
     *              ONE TREE, SEVEN CORES: adding another Mesen console is a
     *              host file (Core/mesen_core.cpp's shape) plus a build list,
     *              not a new dependency. See docs/plans/emulator-agnostic-shell.md.
     * N64        -> Mupen64Plus-Next, INTEGRATED on its software path
     *              (angrylion + cxd4 + cached interpreter: no JIT, which iOS
     *              does not allow): Core/n64_core.cpp, proved on the host by
     *              scripts/n64-host-proof.sh (READY row below).
     * PS1        -> PCSX ReARMed, INTEGRATED: Core/ps1_core.cpp (READY row
     *              below, interpreter + software GPU on every platform).
     * PS2        -> libretro cores loaded at run time (Play!, and ARMSX2
     *              when a BIOS is present): Core/ps2_core.cpp (READY row
     *              below; Android first, JIT cores, speed proven per game
     *              on the device, never assumed from the host).
     * GC/Wii     -> Dolphin-class; same caveat, and 32-bit ARM is a wall
     */

    { OGA_SYS_SNES, "Super Nintendo", kSnesExt, OGA_BACKEND_READY,
      1, true, false, 0, true, kDiamond, 4 },

#ifdef SABOTAGE_DUPLICATE_IDS
    { OGA_SYS_DS, "Nintendo 64", kN64Ext, OGA_BACKEND_READY,
#else
    { OGA_SYS_N64, "Nintendo 64", kN64Ext, OGA_BACKEND_READY,
#endif
      1, false, false, 1, true, kN64, 7 },

    /* The Genesis is READY, and it sits ahead of the PlayStation row ON
     * PURPOSE: filename lookup is first-match, both consoles claim .bin,
     * and the Genesis is the one with a backend. A PlayStation .bin was
     * never bootable in this build; a Genesis .bin now is, and one that is
     * not really a Genesis ROM is refused in words by the core's content
     * check rather than played on the wrong machine. The core is Genesis
     * Plus GX (Core/genesis_core.cpp); the pad is the full 6-button pad. */
    { OGA_SYS_GENESIS, "Sega Genesis", kGenesisExt, OGA_BACKEND_READY,
      1, false, false, 0, true, kGenesis, 6 },

    /* READY: PCSX ReARMed is linked in (Core/ps1_core.cpp), proved by
     * scripts/ps1-core-test.sh. A DualShock: two sticks, and L2/R2/L3/R3 on
     * the extended pad slots (POKE_BTN_L2..R3 in pokecore.h). */
    { OGA_SYS_PS1, "PlayStation", kPs1Ext, OGA_BACKEND_READY,
      1, true, false, 2, true, kPlayStation, 4 },

    /* READY: the PS2 wrapper is in (Core/ps2_core.cpp) — it dlopen()s a
     * libretro core (Play!, or ARMSX2 when the player has a BIOS) rather
     * than linking one, proved by scripts/ps2-core-test.sh and the
     * tools/ps2-map-harness.cpp boot proof. A DualShock 2: two sticks,
     * and L2/R2/L3/R3 on the extended pad slots. */
    { OGA_SYS_PS2, "PlayStation 2", kPs2Ext, OGA_BACKEND_READY,
      1, true, false, 2, true, kPlayStation, 4 },

    { OGA_SYS_GAMECUBE, "GameCube", kGcExt, OGA_BACKEND_NONE,
      1, true, false, 2, true, kPlayStation, 4 },

    { OGA_SYS_WII, "Wii", kWiiExt, OGA_BACKEND_NONE,
      1, true, false, 2, true, kPlayStation, 4 },

    /* --- listed because a core for them exists and is installed here ---
     *
     * These are not "planned" in the hand-wavy sense: each names a core that
     * already runs on this machine (docs/emulator-inventory.md) and could be
     * brought into the app. They are PLANNED rather than READY because the
     * core is not linked into this build yet — the registry's job is to say
     * which console a file is, so the picker can name it instead of ignoring
     * it, and so adding the console later is a backend plus no UI work.
     *
     * A system with no available core is NOT added here at all, not even as a
     * row: listing consoles nothing can ever boot would make the picker's
     * "not playable yet" lie. PS2/GC/Wii stay above because they are on the
     * wanted list with real cores, just not phone-viable yet.
     */
    { OGA_SYS_DREAMCAST, "Dreamcast", kDreamcastExt, OGA_BACKEND_PLANNED,
      1, true, false, 1, true, kDiamond, 4 },

    /* The 3DS is the SECOND two-screen console: a top panel plus a touch
     * panel, and one circle pad. Its pad is the DS diamond plus a stick. */
    { OGA_SYS_3DS, "Nintendo 3DS", k3dsExt, OGA_BACKEND_PLANNED,
      2, true, true, 1, true, kDiamond, 4 },

    /* --- the rest of Mesen's set, measured the same way ---
     *
     * Mesen (MesenCE; nesdev-org/MesenCE, the maintained successor to the
     * archived SourMesen/Mesen2) is ONE tree carrying seven console cores
     * (Core/NES, SNES,
     * Gameboy, GBA, PCE, SMS, WS), each self-contained and linking only
     * against Core/Shared. scripts/mesen-feasibility.sh compiles all seven
     * for aarch64-linux-android26 and all seven pass, so each row below
     * names a core whose BUILD is a measured fact. What is missing is the
     * host glue (Core/mesen_core.cpp), not the core.
     *
     * They stay PLANNED until a ROM boots here, which is the registry's rule
     * and the only honest answer: a console that claims to be playable and
     * then does nothing is worse for a blind player than a named refusal.
     */
    { OGA_SYS_MASTER, "Sega Master System", kMasterExt, OGA_BACKEND_PLANNED,
      1, false, false, 0, true, kTwoFace, 2 },

    { OGA_SYS_PCE, "PC Engine", kPceExt, OGA_BACKEND_PLANNED,
      1, false, false, 0, true, kTwoFace, 2 },

    { OGA_SYS_WONDERSWAN, "WonderSwan", kWsExt, OGA_BACKEND_PLANNED,
      1, false, false, 0, true, kTwoFace, 2 },
};

const int kSystemCount = (int)(sizeof(kSystems) / sizeof(kSystems[0]));

/* Case-insensitive comparison of a filename's extension against a list. */
bool ExtInList(const char* ext, const char* const* list)
{
    for (int i = 0; list[i] != NULL; i++)
        if (strcmp(ext, list[i]) == 0) return true;
    return false;
}

const OgaSystem* FindByExt(const char* ext)
{
    for (int i = 0; i < kSystemCount; i++)
        if (ExtInList(ext, kSystems[i].extensions)) return &kSystems[i];
    return NULL;
}

/* Lower-cased extension INCLUDING the dot, or empty when there is none.
 * `out` must hold at least 8 bytes; longer extensions are truncated, which is
 * safe because every extension the table knows is 6 characters or fewer. */
void NormalizeExt(const char* path, char* out, int outSize)
{
    out[0] = '\0';
    if (!path || outSize < 2) return;

    const char* dot = strrchr(path, '.');
    if (!dot) return;

    /* A trailing dot is not an extension. */
    if (dot[1] == '\0') return;

    /* NOTE: no separate "is this dot in a directory name?" guard is needed.
     * strrchr already found the LAST dot, and the loop below copies to the END
     * of the string, so the copied text only matches an extension when the dot
     * is followed by exactly that extension and nothing else. A dot inside a
     * directory name therefore cannot match. A guard for it was written, tested
     * against, and removed: the sabotage harness showed no input could tell the
     * two versions apart, so the line was dead code claiming to be a rule. */

    int n = 0;
    for (const char* p = dot; *p && *p != '/' && *p != '\\' && n < outSize - 1; p++, n++)
        out[n] = (char) tolower((unsigned char) *p);
    out[n] = '\0';
}


/* ---- PlayStation disc sniff --------------------------------------------
 *
 * .iso, .pbp and .bin are shared names: a PSP UMD, a PS1 disc and a PSP EBOOT
 * all use .iso/.pbp, and .bin is both a Genesis ROM and a PS1 track. The name
 * cannot say which console a file is for, so these three are decided by the
 * first bytes of the file. Everything a decision needs sits in the first
 * 64 KiB (OGA_DISC_HEADER_BYTES):
 *
 *   cooked ISO (2048-byte sectors): the ISO 9660 primary volume descriptor at
 *     0x8000 ("\1CD001"), whose system id says "PLAYSTATION" on a PS1 disc
 *     and "PSP GAME" on a UMD;
 *   raw image (2352-byte sectors, a single-track .bin or a raw .iso): the same
 *     descriptor inside sector 16, after the 12-byte sync and the header;
 *   PBP: "\0PBP", whose PARAM.SFO says CATEGORY "ME" for a PlayStation disc
 *     converted to run on a PSP (what PCSX ReARMed reads) and "EG"/"MG" for a
 *     real PSP program.
 *
 * ⛔ A PS2 DVD also says "PLAYSTATION". It is told apart by size: a CD holds
 * at most about 360 000 sectors, and the descriptor's volume size above
 * kCdMaxSectors is a DVD. PvdIsPs2 below is the same test with the size
 * line crossed the other way, so oga_system_for_file and the backend
 * resolver claim a PS2 .iso for the PlayStation 2 (Core/ps2_core.cpp). A
 * PS2 disc whose header cannot be read still falls through to the name
 * rules and reaches the PSP, which refuses it in words. */
const uint32_t kCdMaxSectors = 450000;

uint32_t Le32(const uint8_t* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

uint16_t Le16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

bool PvdIsPs1(const uint8_t* d, size_t n, size_t off)
{
    if (off + 84 > n) return false;
    const uint8_t* v = d + off;
    if (v[0] != 1 || memcmp(v + 1, "CD001", 5) != 0) return false;
#ifdef SABOTAGE_PS1_SNIFF_ANY_DISC
    return true;
#endif
    if (memcmp(v + 8, "PLAYSTATION", 11) != 0) return false;
    return Le32(v + 80) <= kCdMaxSectors;
}

/* The PS2 twin: same descriptor, DVD-sized volume. Disjoint from PvdIsPs1
 * by the size line, never both. */
bool PvdIsPs2(const uint8_t* d, size_t n, size_t off)
{
    if (off + 84 > n) return false;
    const uint8_t* v = d + off;
    if (v[0] != 1 || memcmp(v + 1, "CD001", 5) != 0) return false;
    if (memcmp(v + 8, "PLAYSTATION", 11) != 0) return false;
    return Le32(v + 80) > kCdMaxSectors;
}

/* PARAM.SFO's CATEGORY, compared to `want` (two letters). */
bool SfoCategoryIs(const uint8_t* sfo, size_t n, const char* want)
{
    if (n < 20 || memcmp(sfo, "\0PSF", 4) != 0) return false;
    uint32_t keys = Le32(sfo + 8), data = Le32(sfo + 12), count = Le32(sfo + 16);
    if (count > 256) return false;
    for (uint32_t i = 0; i < count; i++)
    {
        size_t e = 20 + (size_t) i * 16;
        if (e + 16 > n) return false;
        size_t k = (size_t) keys + Le16(sfo + e);
        size_t v = (size_t) data + Le32(sfo + e + 12);
        if (k + 9 > n || memcmp(sfo + k, "CATEGORY", 9) != 0) continue;
        return v + 2 <= n && memcmp(sfo + v, want, 2) == 0;
    }
    return false;
}

/* The three names that are decided by content, not by name alone. */
bool ExtIsSniffed(const char* ext)
{
    return strcmp(ext, ".iso") == 0 || strcmp(ext, ".pbp") == 0 || strcmp(ext, ".bin") == 0;
}

} // namespace

extern "C" {

const OgaSystem* oga_system_for_path(const char* path)
{
    char ext[8];
    NormalizeExt(path, ext, (int) sizeof(ext));
#ifdef SABOTAGE_UNKNOWN_IS_DS
    /* The bug this file exists to prevent: an unrecognised file boots the DS. */
    if (ext[0] == '\0') return &kSystems[0];
#endif
    if (ext[0] == '\0') return NULL;
    const OgaSystem* sys = FindByExt(ext);
#ifdef SABOTAGE_UNKNOWN_IS_DS
    if (!sys) return &kSystems[0];
#endif
    return sys;
}

const OgaSystem* oga_system_for_extension(const char* ext)
{
    if (!ext || ext[0] == '\0') return NULL;

    /* Accept "nds" and ".nds" alike: callers get this wrong in both
     * directions and it is not worth an error. */
    char norm[8];
    int i = 0;
    if (ext[0] != '.') norm[i++] = '.';
    for (const char* p = ext; *p && i < (int) sizeof(norm) - 1; p++, i++)
        norm[i] = (char) tolower((unsigned char) *p);
    norm[i] = '\0';

    return FindByExt(norm);
}

const OgaSystem* oga_system_by_id(OgaSystemId id)
{
    if (id == OGA_SYS_UNKNOWN) return NULL;
    for (int i = 0; i < kSystemCount; i++)
        if (kSystems[i].id == id) return &kSystems[i];
    return NULL;
}

const OgaSystem* const* oga_all_systems(int* out_count)
{
    /* ⛔ A table of POINTERS, not the struct array cast to one. Casting
     * `const OgaSystem[N]` to `const OgaSystem* const*` makes all[i] read struct
     * i's first 8 bytes as an address -- for OgaSystem that is the id enum, so
     * the caller dereferences address 0x1. ASan caught it; a cast that "looks
     * like an array of pointers" is not one. */
    static const OgaSystem* const kPointers[] = {
        &kSystems[0], &kSystems[1], &kSystems[2], &kSystems[3],
        &kSystems[4], &kSystems[5], &kSystems[6], &kSystems[7],
        &kSystems[8], &kSystems[9], &kSystems[10],
        &kSystems[11], &kSystems[12], &kSystems[13], &kSystems[14],
        &kSystems[15], &kSystems[16],
    };
    static_assert(sizeof(kPointers) / sizeof(kPointers[0]) ==
                      sizeof(kSystems) / sizeof(kSystems[0]),
                  "kPointers and kSystems have drifted apart; every row needs an entry");

    if (out_count) *out_count = kSystemCount;
    return kPointers;
}

bool oga_system_is_runnable(const OgaSystem* sys)
{
#ifdef SABOTAGE_ALL_CLAIM_RUNNABLE
    return sys != NULL;
#else
    return sys != NULL && sys->backend == OGA_BACKEND_READY;
#endif
}

const char* oga_unsupported_reason(const char* path)
{
    static char buf[256];

    const OgaSystem* sys = oga_system_for_path(path);

    if (!sys)
    {
        /* Unknown extension. Say the extension out loud: the player knows what
         * file they picked, and "unsupported file" alone tells them nothing. */
        char ext[8];
        NormalizeExt(path, ext, (int) sizeof(ext));
        if (ext[0] == '\0')
            snprintf(buf, sizeof(buf), "That file has no extension this app recognises.");
        else
            snprintf(buf, sizeof(buf), "%s files are not supported yet.", ext + 1);
        return buf;
    }

    switch (sys->backend)
    {
    case OGA_BACKEND_READY:
        /* Callers should not ask; if they do, do not claim a problem. */
        snprintf(buf, sizeof(buf), "%s.", sys->name);
        break;
    case OGA_BACKEND_PLANNED:
        /* The console is known and a core is chosen but not built in. Saying
         * which console it is beats a generic refusal: the player learns the
         * file was understood. */
#ifdef SABOTAGE_REASON_IS_GENERIC
        snprintf(buf, sizeof(buf), "That game is not supported yet.");
#else
        snprintf(buf, sizeof(buf),
                 "%s games are not playable in this build yet. This file is a %s game.",
                 sys->name, sys->name);
#endif
        break;
    case OGA_BACKEND_NONE:
    default:
        snprintf(buf, sizeof(buf),
                 "%s games cannot be played in this build.", sys->name);
        break;
    }
    return buf;
}

/* ------------------------------------------------------------------------
 * The C-ABI accessors declared in Sources/CPokeCore/include/pokecore.h.
 *
 * They are thin readers of the table above, and they live HERE rather than in
 * the app glue so that "what a console is" has exactly one definition. The app
 * imports these through CPokeCore, the only header Swift sees of the core.
 */

int oga_system_id(const void* sys)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    return s ? (int) s->id : (int) OGA_SYS_UNKNOWN;
}

const char* oga_system_name(const void* sys)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    return (s && s->name) ? s->name : "";
}

int oga_system_face_buttons(const void* sys,
                            const char** titles, const char** symbols,
                            const char** hints, int* raws, int max)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    if (!s || !s->faceButtons || max <= 0) return 0;
    int n = s->faceButtonCount < max ? s->faceButtonCount : max;
    for (int i = 0; i < n; i++)
    {
        if (titles)  titles[i]  = s->faceButtons[i].title;
        if (symbols) symbols[i] = s->faceButtons[i].symbol;
        if (hints)   hints[i]   = s->faceButtons[i].hint;
        if (raws)    raws[i]    = s->faceButtons[i].raw;
    }
    return n;
}

int oga_system_screen_count(const void* sys)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    return s ? s->screenCount : 0;
}

bool oga_system_has_shoulders(const void* sys)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    return s && s->hasShoulders;
}

int oga_system_analog_sticks(const void* sys)
{
    const OgaSystem* s = (const OgaSystem*) sys;
    return s ? s->analogSticks : 0;
}

/* ⛔ Casting `const OgaSystem* const*` to `const void* const*` is the SAME class
 * of bug that already cost this file once: the older oga_all_systems returned
 * the struct array cast to a pointer array, so callers dereferenced address 0x1.
 * Here the array really IS an array of pointers (see kPointers above), so the
 * cast is sound — and the static_assert up there keeps the two from drifting. */
const void* const* oga_all_system_handles(int* out_count)
{
    int count = 0;
    const OgaSystem* const* all = oga_all_systems(&count);
    static const void* ptrs[32];
    static_assert(sizeof(kSystems) / sizeof(kSystems[0]) <= 32,
                  "the registry outgrew the C-ABI pointer mirror");
    for (int i = 0; i < count; i++) ptrs[i] = (const void*) all[i];
    if (out_count) *out_count = count;
    return ptrs;
}

bool oga_disc_header_is_ps1(const uint8_t* d, size_t n)
{
    if (!d) return false;

    /* A PBP: decided by its PARAM.SFO, which the header points at. */
    if (n >= 0x28 && memcmp(d, "\0PBP", 4) == 0)
    {
        size_t sfo = Le32(d + 8), next = Le32(d + 12);
        if (next > n) next = n;
        if (sfo >= next) return false;
        return SfoCategoryIs(d + sfo, next - sfo, "ME");
    }

    /* Cooked: 2048-byte sectors, sector 16 at 0x8000. */
    if (PvdIsPs1(d, n, 0x8000)) return true;

    /* Raw: 2352-byte sectors, sector 16 at 0x9300, behind its sync and header
     * (16 bytes in mode 1, 24 in mode 2 — a PS1 disc is mode 2). */
    static const uint8_t kSync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    const size_t raw = 16 * 2352;
    if (n >= raw + 16 && memcmp(d + raw, kSync, 12) == 0)
        return PvdIsPs1(d, n, raw + (d[raw + 15] == 2 ? 24 : 16));

    return false;
}

bool oga_file_is_ps1_disc(const char* path)
{
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    static uint8_t buf[OGA_DISC_HEADER_BYTES];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return oga_disc_header_is_ps1(buf, n);
}

bool oga_disc_header_is_ps2(const uint8_t* d, size_t n)
{
    if (!d) return false;
    /* Cooked only: a PS2 DVD image is a 2048-byte-sector file. (A CHD has
     * no descriptor in its first bytes at all, so a PS2 CHD is never
     * claimed here; it routes by the resolver's own CHD rule instead.) */
    return PvdIsPs2(d, n, 0x8000);
}

bool oga_file_is_ps2_disc(const char* path)
{
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    static uint8_t buf[OGA_DISC_HEADER_BYTES];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return oga_disc_header_is_ps2(buf, n);
}

const OgaSystem* oga_system_for_file(const char* path)
{
    char ext[8];
    NormalizeExt(path, ext, (int) sizeof(ext));
    if (ExtIsSniffed(ext) && oga_file_is_ps1_disc(path))
        return oga_system_by_id(OGA_SYS_PS1);
    if (ExtIsSniffed(ext) && oga_file_is_ps2_disc(path))
        return oga_system_by_id(OGA_SYS_PS2);
    return oga_system_for_path(path);
}

} // extern "C"
