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

/* The current dungeon, from cur_palace_index_x2 (0x7E040C, an even id per the
 * decomp's map table; 0xFF means no dungeon). nullptr for 0xFF and unknown ids. */
const char* AlttpDungeonName(uint8_t index);

/* The bottle submenu's contents, indexed by link_bottle_info (1-8). These are
 * the game's own names, decoded from kHudBottlesItemText in hud.c with the same
 * L(x) = 0x2550 + x - 'A' the game uses: MUSHROOM, BOTTLE (the empty bottle),
 * LIFE, MEDICINE, MAGIC, CURE,ALL, FAERIE, BEE / GOOD BEE. nullptr for 0 or an
 * unknown value, and the adapter then speaks the number. */
const char* AlttpBottleName(uint8_t info);

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
