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
 * ⛔ DO NOT over-generalise this yet. It has exactly one native adapter
 * (Fire Emblem) and one script adapter (Pokémon). The shapes below are what those
 * two need; anything more is guesswork until a third game proves it.
 */
#ifndef OGA_ADAPTER_H
#define OGA_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

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
};

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
