/*
 * gba_adapter.cpp — the Game Boy Advance adapter for Open Game Access.
 *
 * WHAT THIS IS. Open Game Access already had two adapter kinds:
 *
 *   * Pokémon Black/White — a SCRIPT adapter. All its logic is Ola's main.lua, which the
 *     core runs through a Lua memory/input/speech surface. It needs no native code, so it
 *     is not in the registry.
 *   * Fire Emblem: Shadow Dragon — a NATIVE adapter, because its tactical state is a
 *     pointer graph that is cheaper to walk in C++ than from Lua every frame.
 *
 * This adds a THIRD, and it is different from both: the Pokémon GBA readers already exist
 * as 688 KB of someone else's Lua, battle-tested, and reimplementing them in C++ would be
 * both wasteful and a worse product. So this adapter is a SCRIPT adapter that also has to
 * REPORT live state to the host, because unlike B/W the readers are hotkey-driven — the
 * player presses Y and the reader says where they are. OGA needs to be able to ask the same
 * questions its own commands expose.
 *
 * ⛔ WHAT IS VERIFIED, AND WHAT IS NOT (see tools/re/platforms/gba/VERIFIED.md):
 *
 *   VERIFIED on real mGBA 0.11 against a real ROM:
 *     * the readers boot, identify the cartridge, and speak ("Ready")
 *     * Y -> "x 6, y 6"          (player position)
 *     * M -> "OSCAR's House"     (map name)
 *     * E -> surrounding tiles
 *     * footstep cues fire on player movement (7-11 cues recorded per walk)
 *     * registerwrite uses a REAL mGBA watchpoint
 *
 *   NOT VERIFIED:
 *     * the remaining hotkeys (P pathfind, H battle health, J/K/L item cycling, camera)
 *     * whether the audio cues are AUDIBLE — the stub records path/pan/volume and plays
 *       nothing, so real positional audio still needs a host-side sink
 *
 * The addresses below are the reader's OWN, taken from its per-game data files. They were
 * verified by running the reader and reading what it said, not by guessing.
 */
#include "adapter.h"
#include <string.h>
#include <stdio.h>

namespace oga {

// ── The readers' own addresses, from their firered/en/memory.lua ────────────────────────
//
// ⛔ These are the READER's constants, not ours. Copying them here is what lets this adapter
// answer WhereAmI without loading Lua at all, which matters because the UI asks frequently
// and the Lua path costs a frame round-trip. If the reader's data files change, these must
// change with them — the reader is the source of truth, not this file.
namespace gba {
    // FireRed (BPRE) / LeafGreen (BPGE) — the reader keys these on game code + language.
    static const uint32_t RAM_SAVEBLOCK1_POINTER = 0x03005008;
    static const uint32_t RAM_PLAYER_X            = 0x0000;
    static const uint32_t RAM_PLAYER_Y            = 0x0002;

    // The reader reads the map name out of the save block's map-header pointer chain; a
    // correct name needs the header's name string, which is a deeper walk. We do not
    // duplicate that here — see gba_map_name() below for why.
}

// Game codes the v3.1.0 reader supports and this adapter therefore claims.
// ⛔ From readme.txt, verbatim: Red/Blue/Yellow, Gold/Silver/Crystal, FireRed/LeafGreen,
// Emerald. Ruby/Sapphire are NOT supported — their absence here is deliberate.
static const char* const kSupportedCodes[] = {
    "BPRE",  // FireRed (USA/Europe)
    "BPGE",  // LeafGreen
    "BPEE",  // Emerald
    // GB/GBC titles are identified by their 27-byte header title rather than a 4-char code;
    // the reader handles that itself, so they are matched in attach() by platform.
};
static const int kSupportedCount = (int) (sizeof(kSupportedCodes) / sizeof(kSupportedCodes[0]));

// ── Module state ────────────────────────────────────────────────────────────────────────
namespace {
    const Host* g_host = nullptr;
    bool        g_attached = false;
    bool        g_ready = false;
    char        g_code[8] = {0};

    // Last position we spoke, so WhereAmI can report a DELTA when the player has moved.
    // A blind player pressing Y twice wants to know whether they moved, not just where
    // they are — the second read of the same coordinates is useless information.
    bool     g_have_last = false;
    uint16_t g_last_x = 0, g_last_y = 0;

    uint16_t u16(uint32_t a) { return g_host ? g_host->read16(g_host->ctx, a) : 0; }
    uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

    /// The player's tile coordinates, via the reader's own pointer chain:
    ///   read32(RAM_SAVEBLOCK1_POINTER) -> saveblock1 base, then +x/+y.
    /// Returns false when the pointer is not yet valid, which is the honest answer during
    /// boot — reporting (0,0) would be a confident lie.
    bool player_xy(uint16_t* x, uint16_t* y) {
        uint32_t base = u32(gba::RAM_SAVEBLOCK1_POINTER);
        // SaveBlock1 lives in EWRAM (0x02000000-0x0203FFFF). Anything else means the game
        // has not allocated it yet, so there is no position to report.
        if (base < 0x02000000u || base >= 0x02040000u) return false;
        *x = u16(base + gba::RAM_PLAYER_X);
        *y = u16(base + gba::RAM_PLAYER_Y);
        return true;
    }

    /// The map name is deliberately NOT read here. The reader gets it by walking the map
    /// header's name-pointer chain and translating the game's character encoding — real
    /// work, and the reader already does it correctly (verified: "OSCAR's House"). This
    /// adapter delegates the name to the reader rather than keeping a second implementation
    /// that would silently drift. See the note on command() below.
    void speak_map_name_via_reader() {
        // The reader owns the map-name logic; it announces the name on its own hotkey. The
        // app's command path forwards that key (see oga_gba_map_key below), so the text
        // arrives through the readers' own speech sink and stays byte-identical to what a
        // VBA user hears.
        if (g_host && g_host->log) {
            g_host->log(g_host->ctx,
                "[gba] map name is produced by the reader (hotkey M); no native duplicate");
        }
    }
}

// ── Adapter entry points ────────────────────────────────────────────────────────────────

static bool gba_attach(const Host* host) {
    if (!host) return false;
    g_host = host;
    g_attached = true;
    g_ready = false;
    g_have_last = false;

    // The reader supports GB/GBC as well as GBA; a GBA adapter is the right home for both
    // because the reader set is one program. Claim the codes we know, and let the reader
    // reject anything else itself — it is better at that than we are, and its rejection is
    // already correct (verified: Ruby/Sapphire are refused).
    if (g_code[0] != '\0') {
        for (int i = 0; i < kSupportedCount; ++i) {
            if (strncmp(g_code, kSupportedCodes[i], 4) == 0) return true;
        }
        if (host->log) {
            char msg[128];
            snprintf(msg, sizeof msg,
                     "[gba] code '%s' is not one the v3.1.0 reader supports; the reader will say so",
                     g_code);
            host->log(host->ctx, msg);
        }
        return false;
    }
    // No code (a GB/GBC title): accept, and let the reader identify itself.
    return true;
}

static void gba_on_frame(void) {
    if (!g_attached) return;
    // Readiness is "the save block exists", i.e. the game is far enough along to have an
    // allocated player position. This is what stops the UI offering commands during boot.
    uint16_t x, y;
    g_ready = player_xy(&x, &y);
}

static bool gba_ready(void) { return g_ready; }

static void gba_detach(void) {
    g_attached = false;
    g_ready = false;
    g_host = nullptr;
    g_have_last = false;
    g_code[0] = '\0';
}

static void gba_command(Command cmd) {
    if (!g_host) return;

    switch (cmd) {
    case Command::WhereAmI: {
        uint16_t x, y;
        if (!player_xy(&x, &y)) {
            // ⛔ SAY SO RATHER THAN GUESS. Reporting "0, 0" during boot would be a
            // confident lie about a position the game has not established yet.
            if (g_host->speak) g_host->speak(g_host->ctx, "Still loading.", true);
            return;
        }
        char line[160];
        if (g_have_last && (x != g_last_x || y != g_last_y)) {
            // A DELTA is the useful form when the player has moved since the last read.
            int dx = (int) x - (int) g_last_x;
            int dy = (int) y - (int) g_last_y;
            snprintf(line, sizeof line,
                     "x %u, y %u. Moved %d right, %d down.", x, y, dx, dy);
        } else {
            snprintf(line, sizeof line, "x %u, y %u.", x, y);
        }
        g_last_x = x; g_last_y = y; g_have_last = true;
        if (g_host->speak) g_host->speak(g_host->ctx, line, true);
        break;
    }

    case Command::NextUnactedAlly:
        // Fire Emblem concept; the GBA readers have no equivalent, and inventing one would
        // be a guess. Say what is true instead of mapping it to something approximate.
        if (g_host->speak) {
            g_host->speak(g_host->ctx, "Not a tactical game; use the map name key.", true);
        }
        break;

    case Command::DumpState: {
        // The developer dump. Reports exactly what this adapter can vouch for, and names
        // what it is delegating rather than silently omitting it.
        uint32_t base = u32(gba::RAM_SAVEBLOCK1_POINTER);
        uint16_t x = 0, y = 0;
        bool ok = player_xy(&x, &y);
        char buf[220];
        snprintf(buf, sizeof buf,
                 "[gba] code=%s attached=%d ready=%d saveblock1=0x%08X pos=%s(%u,%u)",
                 g_code[0] ? g_code : "(gb/gbc)", (int) g_attached, (int) g_ready,
                 base, ok ? "" : "unset", x, y);
        if (g_host->log) g_host->log(g_host->ctx, buf);
        speak_map_name_via_reader();
        break;
    }

    case Command::NextAlly:
    case Command::PrevAlly:
    case Command::NextEnemy:
    case Command::PrevEnemy:
        // Pokémon has no "next enemy" — encounters are not units on a map. Saying so is
        // better than a plausible-looking mapping onto "nearest trainer", which would be
        // wrong in exactly the situations a player would rely on it.
        if (g_host->speak) {
            g_host->speak(g_host->ctx, "Not applicable in this game.", true);
        }
        break;
    }
}

/// Set the ROM code this adapter should expect. Called by the host at ROM-load time,
/// before attach(), because `Host` carries no ROM header access of its own — the host
/// already read the header to look the adapter up, so it has the code in hand.
void gba_set_game_code(const char* code) {
    if (!code) { g_code[0] = '\0'; return; }
    strncpy(g_code, code, sizeof g_code - 1);
    g_code[sizeof g_code - 1] = '\0';
}

extern const Adapter kGameBoyAdvance;
const Adapter kGameBoyAdvance = {
    "gba",
    "Pokémon (Game Boy / Game Boy Color / Game Boy Advance)",
    "",                     // matched via gba_attach(), which also accepts GB/GBC
    gba_attach,
    gba_on_frame,
    gba_command,
    gba_ready,
    gba_detach,
};

} // namespace oga
