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

static const Adapter* const kAdapters[] = {
    &kFireEmblemShadowDragon,
    &kGameBoyAdvance,
    &kDragonBallZSaiyans,
    &kDissidiaFinalFantasy,
    &kDragonQuestIX,
};

const Adapter* const* all_adapters(int* count)
{
    if (count) *count = (int) (sizeof(kAdapters) / sizeof(kAdapters[0]));
    return kAdapters;
}

/// `once` suppression (BT2 Speaker.say(once=True)): consecutive identical
/// lines never speak twice. A repeated query re-press ("where is the foe?")
/// re-announces only when the text CHANGED (foe moved, HP band crossed);
/// otherwise the second press stays silent instead of parroting stale state.
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
