/*
 * alttp_rooms.h — what a dungeon room holds, as facts.
 */
#ifndef OGA_ALTTP_ROOMS_H
#define OGA_ALTTP_ROOMS_H

#include <stdint.h>

namespace oga {

struct RoomNote {
    uint8_t chests;      // how many chests the room holds
    bool big;            // at least one of them is a big chest
    const char* note;    // the editor's one-line puzzle note, or nullptr
};

// nullptr when the room has nothing to say (no chests and no note), and for a
// room index outside the game's 0..319 table.
const RoomNote* AlttpRoomNote(uint16_t room);

} // namespace oga

#endif
