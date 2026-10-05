/*
 * n64_adapter.cpp — the Nintendo 64 adapter. A SEAM THAT REFUSES, like the NES one.
 *
 * ⛔ WHY IT EXISTS AT ALL: the user asked for a StarFox adapter, and the honest
 * answer is that this console has TWO independent blockers, not one. Writing the
 * refusal down is how the next person avoids re-investigating it.
 *
 * BLOCKER 1 — NO CORE. There is no N64 backend: no Core/n64_core.cpp, and systems.cpp
 * reports the console as OGA_BACKEND_PLANNED. Same situation as NES.
 *
 * BLOCKER 2 — AND THE MOD ITSELF CANNOT BE PORTED, EVEN WITH A CORE. The StarFox 64
 * accessibility work (ohylli/blind-starship) is NOT an emulator mod. It is a fork of
 * HarbourMasters/Starship, the StarFox 64 DECOMPILATION PORT: the game's own source
 * code re-implemented and compiled natively for PC. It speaks through PRISM
 * (ethindp/prism), a cross-platform screen-reader library, and renders its spatial
 * cues as true 3D binaural (HRTF) audio via Steam Audio.
 *
 *   * There is no ROM and no memory image for an adapter to read, because the game is
 *     not being emulated.
 *   * Its own README limits gameplay accessibility to TRAINING MODE.
 *   * Its cue design (see docs/research/accessibility-mods-survey.md §2c) is the part
 *     worth copying, not its code.
 *
 * ⛔ SO A STARFOX ADAPTER WOULD BE A NEW N64 LUA READER written against an N64 core,
 * with blind-starship as the DESIGN REFERENCE. That is a project, and it is gated on
 * blocker 1. Until then this refuses, which the UI correctly presents as "no reader".
 *
 * game_code is empty for the same reason as the NES adapter: an N64 ROM's header
 * carries a different layout and the registry's four-character-code match at 0x0C
 * does not apply, so selection must come from the system registry, not a code.
 */
#include "adapter.h"

namespace oga {

namespace {

bool N64Attach(const Host* host)
{
    // Refuses on purpose: no core AND no portable reader. Returning true would make
    // poke_adapter_ready() report a reader that cannot read.
    (void) host;
    return false;
}

} // namespace

extern const Adapter kNintendo64;

const Adapter kNintendo64 = {
    "n64",
    "Nintendo 64",
    "",
    N64Attach,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace oga
