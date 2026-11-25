/*
 * mkt_adapter.h — the Mortal Kombat Trilogy (PlayStation) reader's memory map.
 *
 * ⛔ EVERY FIELD STARTS UNSET, AND AN UNSET FIELD SWITCHES ITS FEATURE OFF. No address in this
 * map has been measured against live RAM yet (the project rule: an adapter that invented
 * addresses would speak confident nonsense to a blind player). Published GameShark codes give
 * candidates (docs/reverse-engineering/mortal-kombat-trilogy.md lists them), and the DumpState
 * command logs what those candidates hold, but a candidate is not a measurement. Each field is
 * filled from a run on the owner's phone, through the debug channel's `search` and `watch`.
 *
 * The map is plain data so the host test (Core/mkt_adapter_test.cpp) can hand the adapter a
 * fake one over fake RAM and prove every feature, while the shipped map stays empty.
 */
#ifndef OGA_MKT_ADAPTER_H
#define OGA_MKT_ADAPTER_H

#include <stdint.h>

namespace oga {

constexpr int kMktMaxPauseItems = 8;

struct MktMap {
    // ---- the fight gate ---------------------------------------------------------------
    /// A byte that reads `fight_value` exactly while a round is being fought (not on the
    /// versus screen, not during a fatality). 0 = unmapped: no fight feature runs.
    uint32_t fight_addr;
    uint8_t  fight_value;

    // ---- health (halfwords, 0 = empty) ------------------------------------------------
    uint32_t p1_health_addr;   // the player's side (P1)
    uint32_t p2_health_addr;   // the foe (P2)
    uint16_t health_full;      // what a full bar reads (GameShark codes suggest 0xA6)

    // ---- position along the arena (for closeness and side) ------------------------
    uint32_t p1_x_addr;
    uint32_t p2_x_addr;
    uint8_t  x_width;          // 2 or 4 bytes
    bool     x_signed;
    /// The gap, in the game's own units, that counts as touching and as the far end of the
    /// arena. The beacon's pace runs between them.
    int32_t  gap_near;
    int32_t  gap_far;

    // ---- the pause menu -----------------------------------------------------------
    /// A byte that reads `pause_value` exactly while the game is paused. 0 = unmapped.
    uint32_t pause_addr;
    uint8_t  pause_value;
    /// The highlighted row (a byte, 0-based). 0 = unmapped: "Paused." is said, rows are not.
    uint32_t pause_cursor_addr;
    /// The rows' words exactly as the screen shows them, top to bottom.
    const char* pause_items[kMktMaxPauseItems];
    int      pause_count;
};

/// The map the shipped adapter reads: every field zero until measured. A host test may swap
/// in its own; pass nullptr to go back to the shipped one.
void MktSetMap(const MktMap* map);
const MktMap* MktCurrentMap(void);

} // namespace oga

#endif // OGA_MKT_ADAPTER_H
