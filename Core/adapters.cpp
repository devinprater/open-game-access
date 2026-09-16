/*
 * adapters.cpp — the adapter registry.
 *
 * Only two adapters exist today and they are different in kind, which is the
 * honest current picture:
 *
 *   * Pokémon Black/White — a SCRIPT adapter. All its logic is Ola's main.lua,
 *     bundled as an asset; the core needs nothing from it beyond the Lua memory/
 *     input/speech surface it already provides. It is not registered here because
 *     it needs no native code at all.
 *   * Fire Emblem: Shadow Dragon — a NATIVE adapter, because its tactical state is
 *     a pointer graph (gMapStateManager -> cursor, gUnitList array of records) that
 *     is far cheaper to read in C++ than to walk from Lua every frame.
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

static const Adapter* const kAdapters[] = {
    &kFireEmblemShadowDragon,
    &kGameBoyAdvance,
    &kDragonBallZSaiyans,
};

const Adapter* const* all_adapters(int* count)
{
    if (count) *count = (int) (sizeof(kAdapters) / sizeof(kAdapters[0]));
    return kAdapters;
}

const Adapter* find_by_game_code(const char* code)
{
    if (!code || !*code) return nullptr;
    int n = 0;
    const Adapter* const* list = all_adapters(&n);
    for (int i = 0; i < n; i++) {
        const Adapter* a = list[i];
        if (!a || !a->game_code || !*a->game_code) continue;
        if (strncmp(code, a->game_code, 4) == 0) return a;
    }
    return nullptr;
}

} // namespace oga
