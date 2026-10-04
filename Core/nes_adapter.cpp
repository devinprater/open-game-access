/*
 * nes_adapter.cpp — the NES adapter. A SEAM, deliberately not a reader yet.
 *
 * ⛔ READ THIS BEFORE ADDING TO IT. This adapter exists so the Nintendo
 * Entertainment System has a home in the code BEFORE its backend does. It has NO
 * reads, and that is not an oversight:
 *
 *   * the Mesen core is ADMITTED (84/84 TUs compile for aarch64-linux-android26 —
 *     scripts/nes-feasibility.sh) but NOT INTEGRATED: there is no
 *     Core/nes_core.cpp, so there is no console to read from;
 *   * an adapter that invented addresses would speak confident nonsense to a
 *     blind player, which is the one failure mode this project treats as worse
 *     than silence. Every other adapter here (FE, DBZ, DQ9, Dissidia) earned its
 *     addresses from live RAM before they were committed.
 *
 * So `attach` REFUSES. The core treats that as "no reader", which is the truth:
 * the UI shows no reader controls, and the Lua layer (where a game has one) is
 * still the accessibility path.
 *
 * WHAT LANDS HERE WHEN THE BACKEND DOES:
 *   The first target is the Zelda accessibility mod. Like the Pokémon GB/GBA
 *   readers, that is expected to be a SCRIPT adapter — the host provides memory
 *   reads and speech, the mod owns the game knowledge — in which case this
 *   adapter's job is the same as kGameBoyAdvance: answer the host's own commands
 *   (position, what-am-I-on) natively so the UI can report state without a Lua
 *   round-trip, and let the script do the narration.
 */
#include "adapter.h"

namespace oga {

namespace {

bool NesAttach(const Host* host)
{
    // ⛔ REFUSES ON PURPOSE, and the refusal is load-bearing: it is what keeps a
    // console with no backend from presenting reader controls. Returning true
    // here would make poke_adapter_ready() report a reader that cannot read.
    (void) host;
    return false;
}

} // namespace

// Field order follows Core/adapter.h: id, display_name, game_code, attach,
// on_frame, command, ready, detach.
//
// game_code is EMPTY and that is required, not laziness: the registry matches an
// adapter by the ROM header's four-character code at 0x0C, and a NES ROM has none
// — an iNES header carries a mapper number and a PRG size instead. An adapter with
// a non-empty code here could never be selected for the right reason, and a wrong
// code would select it for the wrong game. Selection is by extension through the
// system registry, so this adapter becomes reachable when the backend hands it a
// console.
// ⛔ THE `extern` IS REQUIRED, NOT COSMETIC. A namespace-scope `const` object has
// INTERNAL linkage in C++ by default, so this definition alone produced a local
// symbol (_ZN3ogaL28kNintendoEntertainmentSystemE) and every test linking
// Core/adapters.cpp failed with "undefined reference to
// oga::kNintendoEntertainmentSystem". The other adapters pair an `extern`
// declaration with their definition for exactly this reason.
extern const Adapter kNintendoEntertainmentSystem;
const Adapter kNintendoEntertainmentSystem = {
    "nes",
    "Nintendo Entertainment System",
    "",          // no four-character game code exists on the NES
    NesAttach,
    nullptr,     // no per-frame work
    nullptr,     // no commands
    nullptr,     // never ready: there is nothing to be ready with
    nullptr,     // nothing to detach
};

} // namespace oga
