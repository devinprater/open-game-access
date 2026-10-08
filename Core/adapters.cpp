/*
 * adapters.cpp — the adapter registry.
 *
 * Adapters come in two kinds:
 *
 *   * SCRIPT — Pokémon Black/White. All its logic is Ola's main.lua, bundled as
 *     an asset; the core needs nothing from it beyond the Lua memory/input/speech
 *     surface it already provides. It is not registered here because it needs no
 *     native code at all.
 *   * NATIVE — the five registered below. Fire Emblem set the pattern: its
 *     tactical state is a pointer graph (gMapStateManager -> cursor, gUnitList
 *     array of records) that is far cheaper to read in C++ than to walk from Lua
 *     every frame.
 *
 * ⛔ The registry is keyed on the ROM header's game code (0x0C..0x0F) so an
 * adapter is selected at ROM-load time and the Pokémon path is untouched.
 */
#include "adapter.h"
#include <string.h>
#include <stdio.h>

namespace oga {

// Implemented in fe_access.cpp — the Fire Emblem adapter's command surface.
// Declared here rather than in the header so adapter.h stays game-agnostic.
extern const Adapter kFireEmblemShadowDragon;
// Implemented in gba_adapter.cpp — the Game Boy / GBC / GBA adapter. A SCRIPT adapter in
// kind (the readers are existing Lua), but it also answers the host's own commands natively
// so the UI can report position without a Lua round-trip. It also accepts GB/GBC titles,
// which the reader identifies by their 27-byte header title rather than a 4-char game code.
extern const Adapter kGameBoyAdvance;
// Implemented in dbz_adapter.cpp — Dragon Ball Z: Attack of the Saiyans (BRPE). A
// NATIVE adapter: the party is a NUL-terminated pointer array into a 0x24C-stride
// character record array, and each member's HP/Ki sit at fixed offsets in the record.
// Every address was confirmed against the game's own Status screens, not inferred.
extern const Adapter kDragonBallZSaiyans;
// Implemented in dissidia_adapter.cpp — Dissidia Final Fantasy (PSP, ULUS10437).
// The largest adapter: title/setup and main menus (RAM cursors, frame-polled),
// YES/NO dialogs, battle + EX Burst prompts, the story board, the PPSSPP OSK
// name entry and story-dialogue speakers. Status and open RE are in TODO.md and
// docs/reverse-engineering/dissidia-final-fantasy.md.
extern const Adapter kDissidiaFinalFantasy;

// Implemented in dq9_adapter.cpp — Dragon Quest IX: Sentinels of the Starry
// Skies (US, YDQE). A NATIVE scaffold over the mod's own documented addresses
// (third-party/DQ9-Access): WhereAmI, party cycling and DumpState. Menus,
// battles and travel stay mod (Lua) territory until a live-ROM pass confirms
// them through this front.
extern const Adapter kDragonQuestIX;

// ⛔ IMPLEMENTED? NO — AND THE REGISTRY SAYS SO. kNintendoEntertainmentSystem
// exists so the NES has a home in the code BEFORE its backend does, which is the
// order this repo has settled on (the system registry lists a console before its
// core; the adapter registry can do the same). Its `attach` returns false, so the
// core refuses cleanly and nothing speaks a guess.
//
// What must NOT happen is this becoming a silently dead entry that looks
// integrated. When Core/nes_core.cpp lands, this adapter gets its real reads and
// the registry row moves PLANNED -> READY in the same commit.
// Implemented in dbzar_adapter.cpp -- Dragon Ball Z: Shin Budokai - Another Road (PSP,
// ULUS10234). Its STORY MODE is a FIELD MODE: the player flies a wide map and must keep
// the cities on it alive, so the reader speaks city health (the game's own +0x20/+0x24
// figure), worst city first, and the nearest city in distance bands. Its addresses were
// read from LIVE memory, not from the file: EBOOT.dec is relocatable, so the static
// lui/addiu immediates in the decompile are not the runtime bases.
extern const Adapter kDragonBallZAnotherRoad;

extern const Adapter kNintendoEntertainmentSystem;

// ⛔ SAME RULE AS THE NES ROW, AND FOR TWO REASONS RATHER THAN ONE.
// kNintendo64 exists so the console has a home before its backend does. Two
// independent blockers keep its `attach` refusing, and both are recorded in
// Core/n64_adapter.cpp:
//   1. no N64 core (systems.cpp reports it OGA_BACKEND_PLANNED), and
//   2. the accessible StarFox 64 work is a DECOMPILATION PORT (blind-starship, a fork
//      of HarbourMasters/Starship), not an emulator mod -- so there is no ROM image to
//      read even with a core. See docs/research/accessibility-mods-survey.md.
// A StarFox reader for this app would be a NEW N64 Lua reader, gated on blocker 1.
extern const Adapter kNintendo64;

static const Adapter* const kAdapters[] = {
    &kFireEmblemShadowDragon,
    &kGameBoyAdvance,
    &kDragonBallZSaiyans,
    &kDissidiaFinalFantasy,
    &kDragonBallZAnotherRoad,
    &kDragonQuestIX,
    &kNintendoEntertainmentSystem,
    &kNintendo64,
};

const Adapter* const* all_adapters(int* count)
{
    if (count) *count = (int) (sizeof(kAdapters) / sizeof(kAdapters[0]));
    return kAdapters;
}

/// `once` suppression (BT2 Speaker.say(once=True)): consecutive identical
/// AUTOMATIC lines never speak twice. Player-requested (High) lines are only
/// noted here, never suppressed: the adapters' Say() lets High through, per
/// announcement-queue.md ("High items bypass dedup: the player asked"), so
/// Where am I right after an identical automatic line still answers.
/// Tests reset this alongside their NSPOKEN counters via AdapterSpeechReset.
static char g_lastSpoken[256] = {0};
static bool g_haveLast = false;

bool AdapterNoteSpoken(const char* s)
{
    if (!s || !*s) return true;
    if (g_haveLast && strcmp(g_lastSpoken, s) == 0) return false;
    strncpy(g_lastSpoken, s, sizeof(g_lastSpoken) - 1);
    g_lastSpoken[sizeof(g_lastSpoken) - 1] = 0;
    g_haveLast = true;
    return true;
}

void AdapterSpeechReset(void)
{
    g_lastSpoken[0] = 0;
    g_haveLast = false;
}

const Adapter* find_by_game_code(const char* code)
{
    if (!code || !*code) return nullptr;
    int n = 0;
    const Adapter* const* list = all_adapters(&n);
    for (int i = 0; i < n; i++) {
        const Adapter* a = list[i];
        if (!a || !a->game_code || !*a->game_code) continue;
        // FULL-string compare, not a 4-char prefix: NDS codes are exactly 4
        // chars so this is identical for them, but PSP IDs share prefixes
        // (every USA game starts ULUS) and a prefix match would hand every
        // PSP game to whichever PSP adapter sorts first.
        if (strcmp(code, a->game_code) == 0) return a;
    }
    return nullptr;
}

} // namespace oga
