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

/* The item menu's slots. hud is hud_cur_item (1-20 the equipment grid, 21-24
 * bottles in the newer inventory); value is the byte at that slot. Returns
 * nullptr for a slot this table does not name, and the adapter then speaks the
 * number rather than inventing a name. */
const char* AlttpItemName(uint8_t hud, uint8_t value);

/* The equipment box at the top of the item menu (link_sword_type and friends).
 * nullptr for 0 (nothing equipped) and for an unknown value. */
const char* AlttpSwordName(uint8_t value);
const char* AlttpShieldName(uint8_t value);
const char* AlttpArmorName(uint8_t value);

} // namespace oga

#endif
