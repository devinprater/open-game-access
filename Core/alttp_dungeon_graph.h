/*
 * alttp_dungeon_graph.h - the dungeon room graph, generated data.
 *
 * Per room (dungeon_room_index 0..319), the directed travel edges the
 * game's own door and header data prove: doors confirmed from both
 * sides, staircases confirmed from both ends, and open-pit drops.
 * Generated offline by scripts/gen-alttp-dungeon-graph.py from the
 * decompilation's room database; no room data ships, only the edges.
 */
#ifndef OGA_ALTTP_DUNGEON_GRAPH_H
#define OGA_ALTTP_DUNGEON_GRAPH_H

#include <stdint.h>

namespace oga {

struct AlttpRoomEdge {
    uint16_t to;    /* destination room */
    uint8_t kind;   /* 0 free door, 1 shutter, 2 small-key door,
                       3 big-key door, 4 bombable, 5 stairs, 6 pit */
    uint8_t dir;    /* door direction 0N 1S 2W 3E; 0xFF stairs, 0xFE pit */
    uint8_t pos;    /* door position along the wall, 0..11 */
};

/* The room's travel edges; returns the count and sets *out. */
int AlttpRoomEdges(uint16_t room, const AlttpRoomEdge** out);

/* The connected component the room belongs to (its smallest room id). */
uint16_t AlttpRoomComponent(uint16_t room);

/* Points of interest in the room: bit 1 entrance, bit 2 big chest,
 * bit 4 boss room. */
uint8_t AlttpRoomPoi(uint16_t room);

/* Whether the room's doors are gated: 0 no, 1 cleared-room, 2 switch. */
uint8_t AlttpRoomGate(uint16_t room);

} // namespace oga

#endif
