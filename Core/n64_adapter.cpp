/*
 * n64_adapter.cpp — the Nintendo 64 adapter. A SEAM THAT REFUSES, like the NES one.
 *
 * ⛔ WHY IT EXISTS AT ALL: the user asked for a StarFox adapter, and the honest
 * answer is that this console has TWO independent blockers, not one. Writing the
 * refusal down is how the next person avoids re-investigating it.
 *
 * BLOCKER 1 — NO CORE. RESOLVED: Core/n64_core.cpp runs Mupen64Plus-Next (software
 * path) and pokecore's LoadN64Rom selects this row. What remains is the reader: no
 * N64 game's memory map has been measured against live RAM in this project yet, and
 * an adapter that invented addresses would speak confident nonsense. n64_read
 * (big-endian CPU view of RDRAM, KSEG0/KSEG1/physical) is the read path a reader
 * will use, as genesis_read is on the Genesis.
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
 * ⛔ SO A STARFOX ADAPTER WOULD BE A NEW N64 LUA READER written against the N64 core,
 * with blind-starship as the DESIGN REFERENCE. That is a project of its own. Until a
 * reader lands this refuses, which the UI correctly presents as "no reader".
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
    // Refuses on purpose: the core runs, but no N64 reader exists. Returning true would make
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
    nullptr,     // cue_snapshot
    "Console only — no game-specific speech yet",
    "This entry is the Nintendo 64 console. Games play on Mupen64Plus-Next, "
    "and no N64 game has an adapter yet.",
};

} // namespace oga
