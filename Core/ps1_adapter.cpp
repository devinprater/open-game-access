/*
 * ps1_adapter.cpp — the Sony PlayStation adapter entry. A CONSOLE ROW, NOT A READER.
 *
 * Same settled shape as the Genesis, NES and N64 rows (see genesis_adapter.cpp
 * for the full reasoning): the console has a home in the adapter registry now
 * that its backend does (Core/ps1_core.cpp, PCSX ReARMed), and its reader
 * state is stated plainly rather than implied.
 *
 * It has NO reads and NO commands: no PlayStation game's memory map has been
 * measured against live RAM in this project yet, and an adapter that invented
 * addresses would speak confident nonsense to a blind player. So `attach`
 * REFUSES, the UI shows no reader controls, and the game simply plays.
 *
 * WHERE A REAL PLAYSTATION READER GOES. Unlike the Genesis, a PlayStation disc
 * carries a real identity: the boot serial in SYSTEM.CNF ("SLUS00594"), which
 * ps1_load_disc reports and pokecore stores as the game code. So a reader for
 * one game is a NEW adapter whose game_code is that serial (the dash dropped,
 * as the core stores it); pokecore's LoadPs1Rom looks the serial up in the
 * registry first and falls back to this row only when no reader matches. Its
 * reads come through the Host path, which the backend serves from main RAM
 * (0x80000000, all mirrors) and the scratchpad (0x1F800000).
 *
 * game_code is EMPTY here on purpose: this row must never match a disc.
 */
#include "adapter.h"

namespace oga {

namespace {

bool Ps1Attach(const Host* host)
{
    // ⛔ REFUSES ON PURPOSE: returning true would make poke_adapter_ready()
    // report a reader that cannot read.
    (void) host;
    return false;
}

} // namespace

// Field order follows Core/adapter.h. The `extern` is required: a
// namespace-scope `const` has internal linkage without it.
extern const Adapter kSonyPlayStation;
const Adapter kSonyPlayStation = {
    "ps1",
    "PlayStation",
    "",          // never matches a disc; game readers register the disc serial
    Ps1Attach,
    nullptr,     // no per-frame work
    nullptr,     // no commands
    nullptr,     // never ready
    nullptr,     // nothing to detach
    nullptr,     // cue_snapshot
    "Console only — no game-specific speech yet",
    "This entry runs PlayStation games through PCSX ReARMed. No PlayStation "
    "game has a reader yet, so games load without speech or cues: playable, "
    "but not accessible.",
};

} // namespace oga
