/*
 * adapter.h — the game-adapter seam for Open Game Access.
 *
 * WHY THIS EXISTS: today the app hard-codes one reader (Ola's Pokémon main.lua).
 * Adding Fire Emblem without a boundary would mean game checks sprinkled through
 * pokecore.cpp, and every future game would make that worse. This is the smallest
 * honest version of the boundary: a registry keyed on the ROM's game code, plus a
 * per-frame hook. It is deliberately thin — the interfaces below are the ones
 * Fire Emblem actually needed, not a speculative framework.
 *
 * The finding that makes this safe (see docs/current-architecture.md §7): nothing
 * in the adapter path is entangled with Pokémon. A fresh NDS is built per ROM, the
 * script is per-core, and the frame/speech/log callbacks are per-core pointers. So
 * adapters can be selected at ROM-load time with no change to the Pokémon path.
 *
 * ⛔ DO NOT over-generalise this. Five native adapters (registered in
 * adapters.cpp) plus the Lua script path use it; every member below exists
 * because one of them needed it. Add a member only when a real game proves it.
 */
#ifndef OGA_ADAPTER_H
#define OGA_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

#include "announce.h"

namespace oga {

/// What an adapter may ask the host to do. Kept as function pointers rather than
/// a base class so an adapter stays testable on the host with a stub host.
struct Host {
    /// Emulator reads. Both are bounds-checked by the host; an adapter may assume
    /// any address outside Main RAM returns 0 rather than faulting.
    uint8_t  (*read8 )(void* ctx, uint32_t addr);
    uint16_t (*read16)(void* ctx, uint32_t addr);
    uint32_t (*read32)(void* ctx, uint32_t addr);
    /// Speak. `interrupt=false` queues behind whatever is speaking.
    void (*speak)(void* ctx, const char* utf8, bool interrupt);
    /// Monotonic clock, ms. The core stamps it before every adapter call
    /// (command, on_frame); host tests stamp it by hand. Queue-routed
    /// speech reads it as "now".
    uint64_t now_ms;
    /// The core's announcement queue, or nullptr in hosts that predate it.
    AnnounceQueue* announce_q;
    /// Migrated adapters route speech through oga::announce() and read the
    /// queue from here; Host::speak stays the direct wire for adapters not
    /// yet migrated (and for the Lua script path, which never shares a ROM
    /// with a migrated adapter).
    /// Silent developer line, for the app's reading log.
    void (*log)(void* ctx, const char* utf8);
    /// Press/release an emulated console button (adapter drives the REAL game,
    /// it does not teleport units or mutate gameplay state).
    void (*set_button)(void* ctx, int ds_button, bool down);
    void* ctx;
};

/// One accessibility query, mapped to whatever the player's input device sends.
enum class Command {
    WhereAmI,
    NextAlly,
    PrevAlly,
    NextEnemy,
    PrevEnemy,
    NextUnactedAlly,
    DumpState,      // the attachable debug dump
    // Menu navigation (append-only: raw values are the C ABI shared with Swift).
    // Sent by the host alongside D-pad taps; adapters that are not on a tracked
    // menu ignore them silently. MenuState logs "MENU <name>" via Host::log.
    MenuState,
    MenuNext,
    MenuPrev,
    MenuLeft,   // D-pad left on a tracked menu: previous value + speak
    MenuRight,  // D-pad right on a tracked menu: next value + speak
    // Player-in-the-loop tracked menus (append-only: raw values are the C ABI
    // shared with Swift; never reorder). The game opens these with a single
    // button whose screen has no RAM signature, so the player taps the button
    // on entry and on exit and the adapter tracks the cursor from MenuNext/Prev.
    CustToggle, // Dissidia story-map Customize: toggle tracking + speak row 1
    CharToggle, // Dissidia main-menu Triangle character select: toggle + row 1
    QuickOn,  // Dissidia battle: Quickmove marker appeared (rising edge)
    QuickOff, // Dissidia battle: Quickmove marker gone (re-arms QuickOn)
    ExReady,    // Dissidia battle: EX gauge full (rising edge, detector)
    ExSpent,    // Dissidia battle: EX gauge no longer full (re-arms ExReady)
    ExActive,   // Dissidia battle: player entered EX Mode (app input-echo)
    ExEnded,    // Dissidia battle: EX Mode over (gauge drained or Burst done)
    ExBurstGo,  // Dissidia EX Mode: HP attack landed, Square prompt is live
    ExQteUp,    // Dissidia EX Burst QTE: speak direction (no dedup)
    ExQteDown,  // Dissidia EX Burst QTE: speak direction (no dedup)
    ExQteLeft,  // Dissidia EX Burst QTE: speak direction (no dedup)
    ExQteRight, // Dissidia EX Burst QTE: speak direction (no dedup)
    ExQteCircle,   // Dissidia EX Burst QTE: face-button prompt (no dedup)
    ExQteSquare,   // Dissidia EX Burst QTE: face-button prompt (no dedup)
    ExQteTriangle, // Dissidia EX Burst QTE: face-button prompt (no dedup)
    ExQteCross,    // Dissidia EX Burst QTE: face-button prompt (no dedup)
    ExBurstGoMash, // Dissidia EX Burst (mash type, e.g. Garland Soul of Chaos): mash prompt
    ExBurstLevel,  // Dissidia EX Burst (mash type): power level up (no dedup)
    // Universal PPSSPP OSK reader (append-only: raw values are the C ABI
    // shared with Swift; never reorder). Every PSP game that needs text entry
    // calls the same system OSK (sceUtilityOsk), so one echo engine
    // (Core/osk_echo.h) serves all of them; each game wires its own
    // entry/exit gates. OskToggle is a player button (tap on entry and exit,
    // like CustToggle); the rest are auto-forwarded by the host on game-pad
    // down-edges and ignored by every adapter that is not tracking an OSK.
    OskToggle, // OSK open: start echo tracking + speak entry (player-tapped)
    OskType,   // Cross on the OSK: type the highlighted key + speak
    OskDelete, // Circle on the OSK: delete last char + speak
    OskSpace,  // Square on the OSK: type a space + speak
    OskShift,  // Select on the OSK: toggle case table + speak
    OskFinish, // Start on the OSK: finish entry, speak the final name
    StopSpeech, // Player stop key: clear the announcement queue + stop the
                // platform voice (append-only; raw values are the C ABI).
    // Spoken-history walk. Like StopSpeech these are CORE-INTERNAL: pokecore.cpp
    // intercepts them before dispatch, so no adapter implements them and none needs to
    // know they exist. They are here rather than in a Swift-only path because the
    // history lives in the announcement queue (the only thing that knows what actually
    // reached the platform), so the ring is testable by announce-test.sh.
    RepeatNewest, // Player repeat key: speak the most recent spoken line again
    RepeatOlder,  // Player repeat-prev key: step one line further back
};

/// Per-frame battle snapshot for host-side audio cues (item 5 spike).
///
/// PLAIN DATA, NO STRINGS, NO SPEECH. The adapter fills this from the same reads its
/// on-demand commands use; the app's synth turns it into sound. The adapter never beeps
/// itself, never writes RAM, never speaks here — feedback only, never aim/damage/movement.
///
/// WHY A STRUCT AND NOT A COMMAND: poke_command() speaks answers through the queue, which
/// paces one line at a time. A 10 Hz beacon cannot go through speech at all — it needs a
/// silent poll the synth reads every tick. NULL member = "this adapter has no cue data".
struct CueSnapshot {
    bool  battle;    // fighters resolve: we are in a battle
    bool  locked;    // the enemy is locked
    bool  is_core;   // reserved; a core lock is IMPOSSIBLE in Dissidia (see note below)
    float dist;      // world units, self to enemy; valid only when locked
    bool  core_gain; // one frame: the player's EX gauge JUMPED (absorbed EX Force/Core)
    // ---- TRAILING ADDITIONS (value-initialize to false/0, so existing 5-field
    // initializers keep compiling; see the trailing-member rule below). ----
    /// A FREE-FLIGHT field with no lock mechanic (Another Road's story mode). The game
    /// does not aim the player, so unlike `locked` this cue must carry DIRECTION.
    bool  field;
    /// Bearing to the target in DEGREES, clockwise from the direction the player is
    /// ALREADY MOVING. 0 = straight ahead, 90 = to the right. This is a measured quantity:
    /// the adapter derives the player's heading from its own recent positions, because the
    /// game stores no facing anywhere (see dbzar-field-mode.md). Valid only when `field`
    /// and `kind != 0`. STALE-BUT-LAST: when the player is stationary the last known
    /// heading is kept, and the cue says so by leaving `heading_live` false.
    float bearing_deg;
    /// What the bearing points at. 0 = nothing, 1 = enemy, 2 = city, 3 = ally.
    /// IDENTITY IS A SEPARATE FIELD BECAUSE IT MUST NOT BE CARRIED BY THE BEARING.
    int   kind;
    /// False when bearing_deg was computed from a stale heading (the player has not moved
    /// recently), so the host can soften or omit the cue rather than point confidently.
    bool  heading_live;
};

// TWO CORE SIGNALS, because the evidence supports both (2026-10-05), with an honest
// correction attached:
//   is_core  -- the lock ring reaches an EX Core and the adapter labels it (list-verified).
//               The project's own section-114 note recorded this being observed live, and the
//               game's design is to lock a core and dash to it. An intermediate pass claimed
//               this was unreachable; that pass searched only the decompiled reports and
//               mis-reported a savestate sweep, so the claim was withdrawn. The branch is the
//               ORIGINAL behaviour, restored.
//   core_gain -- a jump in the EX gauge between frames. The gauge [[fighter+0x51C]+0x14]
//               (full 10000.0) is written by battle event 0x3C, a pickup-gain event, so a
//               jump is an absorption (EX Force or EX Core). Independent of targeting, so it
//               fires even when the player never locks. Measured ordinary gains top out at
//               +192 (see EX_GAIN_THRESHOLD), so the threshold keeps it off noise.

struct Adapter {
    const char* id;              // "fe11"
    const char* display_name;    // "Fire Emblem: Shadow Dragon"
    /// ROM game code from the header (0x0C..0x0F), NUL-terminated, e.g. "YFEE".
    /// Empty means "this adapter is selected some other way" (the Lua adapter).
    const char* game_code;

    /// Called once after the ROM is loaded and the console has booted far enough
    /// to have allocated its structures. Return false if this is not the game the
    /// adapter expected — the registry then falls through to the next adapter.
    bool (*attach)(const Host* host);

    /// Optional per-frame work (e.g. resuming a Lua coroutine).
    void (*on_frame)(void);

    /// Handle one accessibility command.
    void (*command)(Command cmd);

    /// True when the adapter currently has usable game state. The UI uses this to
    /// decide whether its controls are meaningful — and it is the reason a blind
    /// player is never read a stream of garbage before a map finishes loading.
    bool (*ready)(void);

    void (*detach)(void);

    /// Optional silent battle snapshot for the host-side cue synth (may be NULL;
    /// trailing member so existing 8-field initializers keep compiling — the rest
    /// value-initializes to nullptr). Return false when there is no snapshot to give
    /// (no battle, or the state is not readable right now); the synth stays silent.
    /// Must be read-only and speech-free: the same BattleFighters()/lock reads the
    /// on-demand commands already use, repackaged without Say().
    bool (*cue_snapshot)(CueSnapshot* out);
};

/// Look up an adapter by ROM game code. Returns nullptr when none matches, which
/// is the normal case for a game nobody has written an adapter for yet.
const Adapter* find_by_game_code(const char* code);

/// The registered adapters. Order matters only for adapters that share a code.
const Adapter* const* all_adapters(int* count);

/// Consecutive-duplicate speech suppression; see adapters.cpp.
bool AdapterNoteSpoken(const char* s);
void AdapterSpeechReset(void);

} // namespace oga

#endif // OGA_ADAPTER_H
