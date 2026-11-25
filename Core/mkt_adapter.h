/*
 * mkt_adapter.h — the Mortal Kombat Trilogy (PlayStation) reader's memory map.
 *
 * Every non-zero field in kShippedMap (Core/mkt_adapter.cpp) was MEASURED on the owner's own
 * copy of the USA disc (SLUS-00330), run headless in the repo's PCSX ReARMed core, by diffing
 * RAM across known states and then confirming the address in more states. How each one was
 * confirmed is written in docs/reverse-engineering/mortal-kombat-trilogy.md. A field left 0 is
 * UNSET, and an unset field switches its own feature off: this reader never guesses.
 *
 * The map is plain data so the host test (Core/mkt_adapter_test.cpp) can hand the adapter a
 * fake map over fake RAM and prove every feature, including the ones whose real address is
 * still unmeasured.
 */
#ifndef OGA_MKT_ADAPTER_H
#define OGA_MKT_ADAPTER_H

#include <stdint.h>

namespace oga {

/// The screens the reader tells apart, by the pair of bytes at screen_a_addr and screen_b_addr.
/// (Measured: the pair is stable inside every one of those screens and different between them.)
enum MktScreen {
    kMktScreenOther = 0,
    kMktScreenMain,       // the four-box main menu
    kMktScreenOptions,    // the options hub: five icons
    kMktScreenKonfigure,  // Game Konfigure, the list of settings
    kMktScreenSelect,     // character select
    kMktScreenFight,      // a round (and its pause menu, and the gap between rounds)
    kMktScreenContinue,   // "Press start to continue"
    kMktScreenCount
};

struct MktMap {
    // ---- which screen is up ---------------------------------------------------------
    uint32_t screen_a_addr;      // a byte; 0 = unmapped, no menu feature runs
    uint32_t screen_b_addr;      // a second byte that tells the screens apart

    // ---- menus (bytes, 0-based) -------------------------------------------------------
    uint32_t main_cursor_addr;       // 0 Mortal Kombat, 1 2 on 2, 2 8 Player, 3 Options
    uint32_t options_cursor_addr;    // 0 Game, 1 Kontrol, 2 Secret, 3 Sound and Music, 4 Exit
    uint32_t konfigure_cursor_addr;  // 0..11, the rows of Game Konfigure, Exit last
    uint32_t difficulty_addr;        // Game Konfigure difficulty, 1 easy 2 medium 3 hard
    uint32_t select_cursor_addr;     // the highlighted slot on character select
    uint32_t select_table_addr;      // 32 halfwords: slot -> fighter id (read from RAM, so no copy here)

    // ---- the fight --------------------------------------------------------------------
    /// A byte that reads `fight_value` exactly while a round is being fought, between its
    /// rounds, and under its pause; 0 on every menu, select and loading screen.
    uint32_t fight_addr;
    uint8_t  fight_value;

    uint32_t p1_id_addr;         // fighter ids (bytes); the player and the foe
    uint32_t p2_id_addr;

    // health (halfwords, 0 = empty, health_full = a full bar)
    uint32_t p1_health_addr;
    uint32_t p2_health_addr;
    uint16_t health_full;

    // round wins (words); a bout is best of three
    uint32_t p1_wins_addr;
    uint32_t p2_wins_addr;

    // Position. The fighters' game objects MOVE between fights (which of two slots is whose
    // changes), so the address of each fighter's object is read through a pointer, then the
    // x position (a signed halfword, whole units) at x_offset into the object.
    uint32_t p1_obj_ptr_addr;
    uint32_t p2_obj_ptr_addr;
    uint32_t x_offset;
    /// The gap, in the game's own units, that counts as touching and as the far end of the
    /// arena. The beacon's pace runs between them.
    int32_t  gap_near;
    int32_t  gap_far;

    // ---- the pause ----------------------------------------------------------------------
    /// A byte that reads `pause_value` exactly while the game is paused. 0 = unmapped.
    uint32_t pause_addr;
    uint8_t  pause_value;
};

/// The values of the screen id pair for each screen, as measured. Shared with the test.
struct MktScreenId { uint8_t a, b; };
extern const MktScreenId kMktScreenIds[kMktScreenCount];   // kMktScreenOther unused

/// The map the shipped adapter reads. A host test may swap in its own; pass nullptr to go
/// back to the shipped one.
void MktSetMap(const MktMap* map);
const MktMap* MktCurrentMap(void);

} // namespace oga

#endif // OGA_MKT_ADAPTER_H
