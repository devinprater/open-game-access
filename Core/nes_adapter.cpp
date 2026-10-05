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

// ============================================================================
// ZELDA 1 ACCESS — measured 2026-10-05, so the port is a known quantity.
//
// The mod the user wants (GADeuvall2000/Zelda1Access) is a BizHawk Lua reader, not a
// compiled mod: 10 Lua files, ~340 KB, in a 237,806-byte zip. Its module names are
// self-documenting and its shape is now recorded:
//
//     Navigation.lua   192 KB  all announcements, hotkeys, timing
//     Worldmap.lua      38 KB  overworld data (pure data)
//     Dungeons.lua      15 KB  dungeon names + room label formatting
//     GameData.lua      11 KB  byte->name decoding, AND ROM-structure decoding
//                              (NPC dialog text lives in ROM, not RAM)
//     Waypoints.lua            user waypoints, JSON on disk
//     VisitedRooms.lua         progress, dungeon_visited.json on disk
//
// ⛔ ITS ENTIRE EMULATOR API SURFACE IS ONE FUNCTION. Counted across all 10 files:
//     memory.read_u8      135 uses
//     mainmemory          121
//     console.log           5
//     gui.text              4
//     emu.frameadvance      1
//     savestate             1
// with ZERO memory.registerexec, zero write hooks and zero register reads. It is a
// POLL-AND-DECODE reader -- the opposite design from the Pokemon reader's ~35 exec
// hooks -- and it still reaches a complete two-quest playthrough. A reader does not
// have to hook; the cheap design is proven.
//
// ⛔ AND OGA'S SHIM ALREADY PROVIDES IT. bizhawk_compat.lua defines
// mainmemory.read_u8 / read_u16_le / read_u32_le / write_u8 / a range reader. The only
// mismatch is RAM_BASE: the shim hardcodes 0x02000000 (the DS mapping) and NES is a
// flat 64 KB space. So porting this reader needs a per-console RAM_BASE, not a new
// shim -- and nothing in the Lua layer mentions NES, FCEUX or Mesen yet.
//
// Its memory map is 27 distinct read_u8 addresses in NES RAM (0x0010-0x06FF, plus ROM
// reads at 0x6BB2, 0x687E, 0x68FE for text).
//
// Speech and non-speech audio run in a SEPARATE PowerShell process (SoundBridge) that
// must stay alive, carrying footsteps and radar cues as well as speech. OGA has no
// equivalent out-of-process audio path; that is an app-level gap, not an adapter one.
//
// SO WHAT LANDS HERE WHEN THE BACKEND DOES: nothing like the adapters above. This one
// answers the host's own questions (position, what-am-I-on) natively so the UI can
// report state without a Lua round-trip, and lets the PORTED SCRIPT do the narration.
// The script is the reader; the adapter is the native fast path.
// ============================================================================

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
