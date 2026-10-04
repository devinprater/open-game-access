/*
 * systems_test.cpp — host test for the system registry.
 *
 * The rules worth pinning are the ones that would fail SILENTLY: an unknown
 * extension resolving to a real console (a .gba booting melonDS), an extension
 * matching the wrong system (.iso is both a PSP and a PS1 container), a
 * console advertising buttons it does not have, or a system being listed as
 * runnable when no core in this build can load it.
 *
 * No emulator, no ROM, no platform.
 */
#include "systems.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
static int g_checks = 0;

static void ok(const char* what)  { g_checks++; printf("  ok   %s\n", what); }
static void bad(const char* what, const std::string& got)
{
    g_checks++; g_fail++;
    printf("  FAIL %s  (got: %s)\n", what, got.c_str());
}

static void ExpectSys(const char* path, OgaSystemId want, const char* what)
{
    const OgaSystem* s = oga_system_for_path(path);
    if (s && s->id == want) ok(what);
    else bad(what, s ? std::string(s->name) : std::string("null"));
}

static void ExpectNull(const char* path, const char* what)
{
    const OgaSystem* s = oga_system_for_path(path);
    if (!s) ok(what);
    else bad(what, std::string(s->name));
}

int main()
{
    printf("== extension -> system\n");

    ExpectSys("/roms/pokemon.nds",        OGA_SYS_DS,  "a .nds is the DS");
    ExpectSys("/roms/pokemon.gba",        OGA_SYS_GBA, "a .gba is the Game Boy Advance");
    ExpectSys("/roms/tetris.gb",          OGA_SYS_GB,  "a .gb is the Game Boy");
    ExpectSys("/roms/zelda.gbc",          OGA_SYS_GB,  "a .gbc is the Game Boy");
    ExpectSys("/roms/dissidia.iso",       OGA_SYS_PSP, "a .iso is a disc image");
    ExpectSys("/roms/mario64.z64",        OGA_SYS_N64, "a .z64 is the Nintendo 64");
    ExpectSys("/roms/smb.nes",            OGA_SYS_NES, "a .nes is the NES");
    ExpectSys("/roms/sonic.md",           OGA_SYS_GENESIS, "a .md is a Genesis ROM");
    ExpectSys("/roms/sonic.gen",          OGA_SYS_GENESIS, "a .gen is a Genesis ROM");
    ExpectSys("/roms/sonic.smd",          OGA_SYS_GENESIS, "a .smd is a Genesis ROM");
    ExpectSys("/roms/shenmue.cdi",        OGA_SYS_DREAMCAST, "a .cdi is a Dreamcast disc");
    ExpectSys("/roms/shenmue.gdi",        OGA_SYS_DREAMCAST, "a .gdi is a Dreamcast disc");
    ExpectSys("/roms/fe-awakening.3ds",   OGA_SYS_3DS, "a .3ds is a 3DS ROM");
    ExpectSys("/roms/fe-fates.cia",       OGA_SYS_3DS, "a .cia is a 3DS title");
    /* ⛔ A disc-image extension shared by two consoles must resolve to ONE of
     * them, deterministically, and that one must be the console whose core
     * actually reads it. .chd is listed for PS1 and NOT for Dreamcast for this
     * reason: first match in the table wins, so listing it twice would promise
     * Dreamcast support that can never be reached. */
    ExpectSys("/roms/ff7.chd",            OGA_SYS_PS1, "a .chd resolves to the PlayStation, not the Dreamcast");

    printf("\n== case and path handling\n");
    ExpectSys("/roms/POKEMON.NDS",         OGA_SYS_DS,  "uppercase extensions match");
    ExpectSys("/roms/Pokemon.GbA",         OGA_SYS_GBA, "mixed case matches");
    ExpectSys("C:\\roms\\pokemon.nds",    OGA_SYS_DS,  "a Windows path matches");
    ExpectSys("pokemon.nds",              OGA_SYS_DS,  "a bare filename matches");
    ExpectSys("/a.nds.dir/pokemon.gba",   OGA_SYS_GBA, "the LAST dot wins over an earlier one");
    /* ⛔ This is the case the separator rule actually exists for. The rule is
     * `if (dot < lastSep) return`, which only fires when the LAST dot in the
     * path is BEFORE the last separator -- that is, the file has no extension of
     * its own and the only dot belongs to a directory name. Without it, a file
     * called "save" inside "/roms/pokemon.nds.dir/" resolves to the DS from the
     * DIRECTORY name and boots the wrong console. */
    ExpectNull("/roms/pokemon.nds.dir/save", "a file with no extension does not inherit one from its directory");

    printf("\n== the silent-wrong-console rule\n");
    /* The bug this whole file exists to prevent: an unrecognised file must NOT
     * resolve to a console, or it gets loaded into the wrong emulator. */
    ExpectNull("/roms/save.sav",     "a .sav is not a console");
    ExpectNull("/roms/notes.txt",    "a .txt is not a console");
    ExpectNull("/roms/image.png",    "a .png is not a console");
    ExpectNull("/roms/noext",        "a file with no extension is not a console");
    ExpectNull("/roms/trailingdot.", "a trailing dot is not an extension");
    ExpectNull("",                   "an empty path is not a console");
    ExpectNull(nullptr,              "a null path is not a console");

    printf("\n== by-extension form, with and without the dot\n");
    {
        const OgaSystem* a = oga_system_for_extension("nds");
        const OgaSystem* b = oga_system_for_extension(".nds");
        const OgaSystem* c = oga_system_for_extension("NDS");
        if (a && b && c && a->id == OGA_SYS_DS && b->id == OGA_SYS_DS && c->id == OGA_SYS_DS)
            ok("both forms and either case give the DS");
        else
            bad("both forms and either case give the DS", "one of them did not");
    }
    if (!oga_system_for_extension("zzz")) ok("an unknown extension is null");
    else bad("an unknown extension is null", "it matched something");

    printf("\n== hardware facts\n");
    {
        const OgaSystem* ds = oga_system_by_id(OGA_SYS_DS);
        if (ds && ds->screenCount == 2) ok("the DS has two screens");
        else bad("the DS has two screens", ds ? std::to_string(ds->screenCount) : "null");
        if (ds && ds->hasTouch) ok("the DS has a touchscreen");
        else bad("the DS has a touchscreen", "no");
        if (ds && ds->faceButtonCount == 4) ok("the DS has four face buttons");
        else bad("the DS has four face buttons", ds ? std::to_string(ds->faceButtonCount) : "null");

        /* A blind player cannot tell a greyed-out button from a broken one, so
         * a console must not advertise a button its hardware lacks. */
        const OgaSystem* gb = oga_system_by_id(OGA_SYS_GB);
        if (gb && gb->faceButtonCount == 2)
            ok("the Game Boy lists two face buttons, no X/Y");
        else
            bad("the Game Boy lists two face buttons, no X/Y",
                gb ? std::to_string(gb->faceButtonCount) : "null");
        if (gb && !gb->hasShoulders) ok("the Game Boy has no shoulder buttons");
        else bad("the Game Boy has no shoulder buttons", "it claimed shoulders");
        if (gb && gb->screenCount == 1) ok("the Game Boy has one screen");
        else bad("the Game Boy has one screen", gb ? std::to_string(gb->screenCount) : "null");

        const OgaSystem* gba = oga_system_by_id(OGA_SYS_GBA);
        if (gba && gba->hasShoulders && gba->faceButtonCount == 2)
            ok("the GBA has shoulders but still only two face buttons");
        else
            bad("the GBA has shoulders but still only two face buttons", "wrong");

        const OgaSystem* psp = oga_system_by_id(OGA_SYS_PSP);
        if (psp && psp->faceButtonCount == 4 && !psp->hasTouch)
            ok("the PSP has four face buttons and no touchscreen");
        else
            bad("the PSP has four face buttons and no touchscreen", "wrong");
    }

    printf("\n== the two-screen consoles are exactly the DS family\n");
    {
        int count = 0;
        const OgaSystem* const* all = oga_all_systems(&count);
        if (count > 0 && all) ok("the registry is not empty");
        else bad("the registry is not empty", "empty");

        /* Two screens is a real hardware fact of a small set of consoles --
         * the DS and the 3DS -- and it is what the screen picker keys on. A
         * third console claiming two screens is a bug, and so is one of these
         * two LOSING a screen. */
        bool dsHas2 = false, threeDsHas2 = false;
        bool extraTwoScreen = false;
        for (int i = 0; i < count; i++)
        {
            if (all[i]->screenCount != 2) continue;
            if (all[i]->id == OGA_SYS_DS) dsHas2 = true;
            else if (all[i]->id == OGA_SYS_3DS) threeDsHas2 = true;
            else extraTwoScreen = true;
        }
        if (dsHas2 && threeDsHas2 && !extraTwoScreen)
            ok("the DS and the 3DS are the two-screen consoles, and only they are");
        else
            bad("the DS and the 3DS are the two-screen consoles, and only they are",
                extraTwoScreen ? "a third console claims two screens" : "one of them lost a screen");

        /* Every system must have a usable row: a name, and extensions. */
        bool wellFormed = true;
        std::string why;
        for (int i = 0; i < count; i++)
        {
            const OgaSystem* s = all[i];
            if (!s->name || !*s->name) { wellFormed = false; why = "unnamed system"; }
            if (!s->extensions || !s->extensions[0]) { wellFormed = false; why = "no extensions"; }
            if (s->faceButtonCount > 0 && !s->faceButtons) { wellFormed = false; why = "count without a table"; }
            if (s->id == OGA_SYS_UNKNOWN) { wellFormed = false; why = "UNKNOWN in the table"; }
        }
        if (wellFormed) ok("every row has a name, extensions and a coherent button table");
        else bad("every row has a name, extensions and a coherent button table", why);

        /* Ids must be unique: a duplicate would make oga_system_by_id return
         * the first and silently mislabel the second. */
        bool uniqueIds = true;
        for (int i = 0; i < count && uniqueIds; i++)
            for (int j = i + 1; j < count; j++)
                if (all[i]->id == all[j]->id) { uniqueIds = false; break; }
        if (uniqueIds) ok("every system id is unique");
        else bad("every system id is unique", "a duplicate was found");
    }

    printf("\n== consoles with a core that is not linked in yet\n");
    {
        /* These rows exist so the picker can NAME the console. They must not
         * claim to be playable, or a blind player gets a game that boots to
         * nothing. */
        const OgaSystemId planned[] = { OGA_SYS_NES, OGA_SYS_SNES, OGA_SYS_N64,
                                        OGA_SYS_PS1, OGA_SYS_GENESIS,
                                        OGA_SYS_DREAMCAST, OGA_SYS_3DS };
        bool anyRunnable = false;
        for (OgaSystemId id : planned)
        {
            const OgaSystem* s = oga_system_by_id(id);
            if (!s) { anyRunnable = true; break; }   /* missing row = fail below */
            if (oga_system_is_runnable(s)) anyRunnable = true;
        }
        if (!anyRunnable) ok("every console listed with a chosen core is not yet playable");
        else bad("every console listed with a chosen core is not yet playable",
                 "one claimed to be runnable");

        const OgaSystem* gen = oga_system_by_id(OGA_SYS_GENESIS);
        if (gen && gen->faceButtonCount == 3 && !gen->hasShoulders)
            ok("the Genesis has three face buttons and no shoulders");
        else bad("the Genesis has three face buttons and no shoulders", "wrong");

        const OgaSystem* dc = oga_system_by_id(OGA_SYS_DREAMCAST);
        if (dc && dc->analogSticks == 1 && dc->faceButtonCount == 4)
            ok("the Dreamcast has four face buttons and one analog stick");
        else bad("the Dreamcast has four face buttons and one analog stick", "wrong");

        const OgaSystem* n3 = oga_system_by_id(OGA_SYS_3DS);
        if (n3 && n3->screenCount == 2 && n3->hasTouch && n3->analogSticks == 1)
            ok("the 3DS has two screens, a touch screen and a stick");
        else bad("the 3DS has two screens, a touch screen and a stick", "wrong");
    }

    printf("\n== runnable means a core is actually in this build\n");
    {
        const OgaSystem* ds = oga_system_by_id(OGA_SYS_DS);
        const OgaSystem* gba = oga_system_by_id(OGA_SYS_GBA);
        const OgaSystem* psp = oga_system_by_id(OGA_SYS_PSP);
        const OgaSystem* n64 = oga_system_by_id(OGA_SYS_N64);

        if (oga_system_is_runnable(ds) && oga_system_is_runnable(gba) &&
            oga_system_is_runnable(psp))
            ok("DS, GBA and PSP report their cores present");
        else
            bad("DS, GBA and PSP report their cores present", "one reported not ready");

        /* N64 must NOT claim to be runnable: no core for it exists here. */
        if (n64 && !oga_system_is_runnable(n64))
            ok("the N64 does not claim to be playable");
        else
            bad("the N64 does not claim to be playable", "it claimed runnable");

        if (!oga_system_is_runnable(nullptr))
            ok("a null system is not runnable");
        else
            bad("a null system is not runnable", "it claimed runnable");
    }

    printf("\n== the refusal explains itself\n");
    {
        /* An unknown file names the extension. */
        const char* r = oga_unsupported_reason("/roms/thing.xyz");
        if (r && strstr(r, "xyz")) ok("an unknown extension is named in the message");
        else bad("an unknown extension is named in the message", r ? r : "null");

        /* A known console with no core says WHICH console. */
        const char* n = oga_unsupported_reason("/roms/mario64.z64");
        if (n && strstr(n, "Nintendo 64")) ok("a not-yet-playable console is named");
        else bad("a not-yet-playable console is named", n ? n : "null");

        /* Always a string, never null, for any input. */
        if (oga_unsupported_reason(nullptr) && oga_unsupported_reason("")) ok("always returns a string");
        else bad("always returns a string", "null");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
