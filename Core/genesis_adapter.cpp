/*
 * genesis_adapter.cpp — the Sega Genesis adapter entry. A CONSOLE ROW, NOT A READER.
 *
 * ⛔ READ THIS BEFORE ADDING TO IT. This entry exists so the Genesis has a home
 * in the adapter registry now that its backend does (Core/genesis_core.cpp,
 * Genesis Plus GX) — the same settled order as the NES and N64 rows: the
 * console is named in the registry, and its reader state is stated plainly
 * rather than implied.
 *
 * It has NO reads and NO commands, and that is not an oversight:
 *
 *   * no Genesis game's memory map has been measured against live RAM in this
 *     project yet, and an adapter that invented addresses would speak
 *     confident nonsense to a blind player — the one failure mode this
 *     project treats as worse than silence;
 *   * so `attach` REFUSES. The core treats that as "no reader": the UI shows
 *     no reader controls, and the game simply plays, which is the truth.
 *
 * WHAT LANDS HERE WHEN A READER DOES: the first Genesis reader gets its
 * addresses from a live-ROM pass like every other adapter here, replaces the
 * refusal with real reads, and this row's coverage/notes are rewritten to say
 * what it speaks. Until then the row's job is honesty, not narration.
 *
 * game_code is EMPTY, as on the NES row: Genesis ROMs carry a product serial
 * in their header, not the four-character code at 0x0C the registry matches
 * DS/PSP adapters by, so selection is by extension through the system
 * registry (pokecore's LoadGenesisRom selects this entry directly).
 */
#include "adapter.h"

namespace oga {

namespace {

bool GenesisAttach(const Host* host)
{
    // ⛔ REFUSES ON PURPOSE, and the refusal is load-bearing: it is what keeps
    // a console with no reader from presenting reader controls. Returning true
    // here would make poke_adapter_ready() report a reader that cannot read.
    (void) host;
    return false;
}

} // namespace

// Field order follows Core/adapter.h: id, display_name, game_code, attach,
// on_frame, command, ready, detach.
//
// ⛔ THE `extern` IS REQUIRED, NOT COSMETIC — see the note on the NES row in
// nes_adapter.cpp: a namespace-scope `const` has internal linkage by default,
// and without the extern this definition is invisible to adapters.cpp.
extern const Adapter kSegaGenesis;
const Adapter kSegaGenesis = {
    "genesis",
    "Sega Genesis",
    "",          // no four-character game code exists on the Genesis
    GenesisAttach,
    nullptr,     // no per-frame work
    nullptr,     // no commands
    nullptr,     // never ready: there is nothing to be ready with
    nullptr,     // nothing to detach
    nullptr,     // cue_snapshot
    "Console only — no game-specific speech yet",
    "This entry runs Genesis and Mega Drive games through Genesis Plus GX. "
    "No Genesis game has a reader yet, so games load without speech or cues: "
    "playable, but not accessible.",
};

} // namespace oga
