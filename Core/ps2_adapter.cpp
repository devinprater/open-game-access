/*
 * ps2_adapter.cpp — the Sony PlayStation 2 adapter entry. A CONSOLE ROW, NOT A READER.
 *
 * Same settled shape as the PlayStation row (see ps1_adapter.cpp): the console
 * has a home in the adapter registry now that its backend does
 * (Core/ps2_core.cpp — a dlopen()ed libretro core, Play! or ARMSX2), and its
 * reader state is stated plainly rather than implied.
 *
 * It has NO reads and NO commands: no PlayStation 2 game's memory map has
 * been measured against live RAM in this project yet, and an adapter that
 * invented addresses would speak confident nonsense to a blind player. So
 * `attach` REFUSES, the UI shows no reader controls, and the game simply
 * plays.
 *
 * WHERE A REAL PLAYSTATION 2 READER GOES. Like the PlayStation, a PS2 disc
 * carries a real identity: the boot serial in SYSTEM.CNF ("SLUS20998"),
 * which ps2_load_disc reports and pokecore stores as the game code. So a
 * reader for one game is a NEW adapter whose game_code is that serial (the
 * punctuation dropped, as the core stores it); pokecore's LoadPs2Rom looks
 * the serial up in the registry first and falls back to this row only when
 * no reader matches. Its reads come through the Host path, which the
 * backend serves from EE RAM (the KUSEG/KSEG windows and the 0x2/0x3
 * uncached aliases) and, when the core publishes it, the scratchpad.
 *
 * game_code is EMPTY here on purpose: this row must never match a disc.
 */
#include "adapter.h"

namespace oga {

namespace {

bool Ps2Attach(const Host* host)
{
    // ⛔ REFUSES ON PURPOSE: returning true would make poke_adapter_ready()
    // report a reader that cannot read.
    (void) host;
    return false;
}

} // namespace

// Field order follows Core/adapter.h. The `extern` is required: a
// namespace-scope `const` has internal linkage without it.
extern const Adapter kSonyPlayStation2;
const Adapter kSonyPlayStation2 = {
    "ps2",
    "PlayStation 2",
    "",          // never matches a disc; game readers register the disc serial
    Ps2Attach,
    nullptr,     // no per-frame work
    nullptr,     // no commands
    nullptr,     // never ready
    nullptr,     // nothing to detach
    nullptr,     // cue_snapshot
    "Console only — no game-specific speech yet",
    "This entry runs PlayStation 2 games through the PS2 core (Play!, or "
    "ARMSX2 when a BIOS is present). No PlayStation 2 game has a reader "
    "yet, so games load without speech or cues: playable, but not "
    "accessible.",
};

} // namespace oga
