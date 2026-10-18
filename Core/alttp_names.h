/*
 * alttp_names.h — what to call an overworld area.
 *
 * The game has no area-name table (nothing in the decompilation names a screen),
 * so this is a curated table keyed by the overworld screen/area index. Unknown
 * areas return nullptr and the adapter speaks the number instead. Never invent a
 * name for an area that is not in this table.
 */
#ifndef OGA_ALTTP_NAMES_H
#define OGA_ALTTP_NAMES_H

#include <stdint.h>

namespace oga {

/* area is overworld_area_index: 0x00-0x3F light world, 0x40-0x7F dark world. */
const char* AlttpAreaName(uint8_t area);

} // namespace oga

#endif
