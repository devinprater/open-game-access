/*
 * alttp_walk.h - what the A Link to the Past auto walker can ask of the
 * live game beyond the route graphs (docs/design/auto-walk.md).
 */
#ifndef OGA_ALTTP_WALK_H
#define OGA_ALTTP_WALK_H

#include <stdint.h>

namespace oga {

/* The push-block census for one dungeon room, read from the game's own
 * live table (movable_block_datas at $7E:F940). Returns how many push
 * blocks the room holds right now, or -1 when that cannot be known
 * (no game attached, Link outdoors, or the table never initialised).
 * `tiles`, when non-null, receives up to `cap` block positions as
 * 16-pixel tile indices in the room's 32x32 grid (row * 32 + column).
 * A walker treats -1 as costly: unknown is never "no blocks".
 * Defined in alttp_adapter.cpp. */
int AlttpPushBlocksInRoom(uint16_t room, uint16_t* tiles, int cap);

} // namespace oga

#endif
