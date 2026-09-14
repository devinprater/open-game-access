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

static const Adapter* const kAdapters[] = {
    &kFireEmblemShadowDragon,
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
