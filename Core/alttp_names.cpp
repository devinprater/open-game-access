/*
 * alttp_names.cpp — the overworld area names and the item menu's names.
 *
 * Sources, stated so a later reader can check them:
 *  - 0x2C Link's House: MEASURED. The public save whose own description says
 *    "Start From Link's House" loads at area 44 (0x2C) and, once outdoors,
 *    reports exactly that area.
 *  - The rest of the overworld table is the community `ow_names` table from the
 *    yba.lua auto-tracker (milde.no/public/alttp/yba.lua), keyed by the same
 *    live `overworld_area_index` this adapter reads. Abbreviations are expanded
 *    (HC, EP, GT, TR, SW, TT, PoD), a few joke labels are replaced with
 *    descriptive ones, and "dark" is dropped from dark-world names because the
 *    adapter already says "in the dark world".
 *  - ⛔ THE OLD TABLE HAD 0x21 FOR KAKARIKO AND 0x39 FOR THE DESERT. That was
 *    development-map numbering, not the live area index; the tracker table (and
 *    the measured 0x2C) says 0x18 and 0x30. The wrong ids were removed, not
 *    kept beside the right ones.
 *
 * ⛔ DO NOT GUESS THE REST. An unknown area speaks its number; a wrong name sends
 * a blind player the wrong way, which is worse than a number.
 */
#include "alttp_names.h"

namespace oga {

namespace {
struct Entry { uint8_t area; const char* name; };

const Entry kNames[] = {
    /* Light world. */
    { 0x00, "Lost Woods" },
    { 0x02, "Northeast house" },
    { 0x03, "Spectacle Rock" },
    { 0x05, "Death Mountain East" },
    { 0x07, "Turtle Rock" },
    { 0x0A, "Death Mountain cave" },
    { 0x0F, "Zora's Domain waterfall" },
    { 0x10, "Lost Woods entrance" },
    { 0x11, "Fortune teller's house" },
    { 0x12, "Teleport lake" },
    { 0x13, "Sanctuary" },
    { 0x14, "Graveyard" },
    { 0x15, "River" },
    { 0x16, "Witch's hut" },
    { 0x17, "Waterfall" },
    { 0x18, "Kakariko Village" },
    { 0x1A, "Forest" },
    { 0x1B, "Hyrule Castle" },
    { 0x1D, "Castle bridge" },
    { 0x1E, "Eastern Palace" },
    { 0x22, "Blacksmith's house" },
    { 0x25, "Octorok field" },
    { 0x28, "Fence maze" },
    { 0x29, "Kakariko library" },
    { 0x2A, "Haunted grove" },
    { 0x2B, "Flute clearing" },
    { 0x2C, "Link's House" },
    { 0x2D, "Bridge to Hyrule Castle" },
    { 0x2E, "South of Eastern Palace" },
    { 0x2F, "Peg circle" },
    { 0x30, "Desert of Mystery" },
    { 0x32, "Desert bluffs" },
    { 0x33, "Near the swamp" },
    { 0x34, "Great Swamp north" },
    { 0x35, "Lake Hylia" },
    { 0x37, "Ice Rod cave" },
    { 0x3A, "Sleeping man" },
    { 0x3B, "Great Swamp south" },
    { 0x3C, "Great Swamp south-east" },
    { 0x3F, "Lake Hylia south-east" },

    /* Dark world. The adapter already says "dark world", so these do not repeat
     * it. */
    { 0x40, "Skull Woods" },
    { 0x42, "Northeast house" },
    { 0x43, "Ganon's Tower" },
    { 0x45, "Death Mountain East" },
    { 0x47, "Turtle Rock" },
    { 0x4A, "Magic Cape cave" },
    { 0x4F, "Mysterious pond" },
    { 0x50, "Outside Skull Woods" },
    { 0x51, "Fortune teller's house" },
    { 0x52, "Small lake" },
    { 0x53, "Sanctuary" },
    { 0x54, "Graveyard" },
    { 0x55, "Waterway" },
    { 0x56, "Witch's hut" },
    { 0x57, "Lake Hylia shore" },
    { 0x58, "Thieves' Town" },
    { 0x5A, "Unnamed house" },
    { 0x5B, "Pyramid of Power" },
    { 0x5D, "Broken bridge" },
    { 0x5E, "Hedge maze" },
    { 0x62, "Locked chest house" },
    { 0x65, "Octorok field" },
    { 0x68, "Shovel game" },
    { 0x69, "Arrow game" },
    { 0x6A, "Haunted grove" },
    { 0x6B, "Outside the haunted grove" },
    { 0x6C, "Link's House" },
    { 0x6D, "Peg bridge" },
    { 0x6E, "Outside the hedge maze" },
    { 0x6F, "Peg circle" },
    { 0x70, "Misery Mire" },
    { 0x72, "Outside Misery Mire" },
    { 0x73, "Outside the swamp" },
    { 0x74, "Swamp north" },
    { 0x75, "Frozen Lake Hylia" },
    { 0x77, "Black ice cave" },
    { 0x7A, "Sleeping man" },
    { 0x7B, "Swamp" },
    { 0x7C, "Swamp south-east" },
    { 0x7F, "Waterfall" },
};
} // namespace

const char* AlttpAreaName(uint8_t area)
{
    for (const Entry& e : kNames)
        if (e.area == area) return e.name;
    return nullptr;
}

/* The dungeon ids the game puts in cur_palace_index_x2, from the decompilation's
 * own map table (dungeon index x 2):
 *   0x00 Sewer Passage, 0x02 Hyrule Castle, 0x04 Eastern Palace,
 *   0x06 Desert Palace, 0x08 Hyrule Castle (second map), 0x0A Swamp Palace,
 *   0x0C Palace of Darkness, 0x0E Misery Mire, 0x10 Skull Woods,
 *   0x12 Ice Palace, 0x14 Tower of Hera, 0x16 Thieves' Town (Gargoyle's Domain),
 *   0x18 Turtle Rock, 0x1A Ganon's Tower.
 *
 * MEASURED 2026-10-17: 0xFF at Link's house, which is the "no dungeon" sentinel
 * the decomp tests for. The dungeon values themselves are sourced from that
 * table, not yet observed by walking into one.
 *
 * ⛔ 0 is a REAL dungeon (the sewers), not "no dungeon". Only 0xFF means none. */
const char* AlttpDungeonName(uint8_t index)
{
    switch (index) {
        case 0x00: return "Sewer Passage";
        case 0x02: return "Hyrule Castle";
        case 0x04: return "Eastern Palace";
        case 0x06: return "Desert Palace";
        case 0x08: return "Hyrule Castle";
        case 0x0A: return "Swamp Palace";
        case 0x0C: return "Palace of Darkness";
        case 0x0E: return "Misery Mire";
        case 0x10: return "Skull Woods";
        case 0x12: return "Ice Palace";
        case 0x14: return "Tower of Hera";
        case 0x16: return "Thieves' Town";
        case 0x18: return "Turtle Rock";
        case 0x1A: return "Ganon's Tower";
        default: return nullptr;
    }
}

/* The item menu's 20 equipment slots, in the game's own order (the order of
 * link_item_bow..link_item_mirror, hud.c's inventory pointers, and the 4-wide
 * grid the menu draws). Names are the game's item names; the levels are the
 * values hud.c's own graphics tables distinguish (bow 5 frames, boomerang 3,
 * mushroom 3, flute 4, sword 5, shield 4, armour 5).
 *
 * ⛔ The bottle slot is hud 16 in the original inventory and 21-24 in the newer
 * one; neither is an equipment count, so a zero value there says nothing about
 * whether bottles are empty. */
const char* AlttpItemName(uint8_t hud, uint8_t value)
{
    switch (hud) {
        case 1:  return "Bow";
        case 2:  return value >= 2 ? "Magical boomerang" : "Boomerang";
        case 3:  return value >= 2 ? "Long shot" : "Hookshot";
        case 4:  return "Bombs";
        case 5:  return value >= 2 ? "Magic powder" : "Mushroom";
        case 6:  return "Fire rod";
        case 7:  return "Ice rod";
        case 8:  return "Bombos medallion";
        case 9:  return "Ether medallion";
        case 10: return "Quake medallion";
        case 11: return "Lamp";
        case 12: return "Hammer";
        case 13: return value == 1 ? "Shovel" : "Flute";
        case 14: return "Bug net";
        case 15: return "Book of Mudora";
        case 16: return "Bottles";
        case 17: return "Cane of Somaria";
        case 18: return "Cane of Byrna";
        case 19: return "Magic cape";
        case 20: return "Magic mirror";
        case 21: case 22: case 23: case 24: return "Bottle";
        default: return nullptr;
    }
}

const char* AlttpSwordName(uint8_t value)
{
    switch (value) {
        case 1: return "Fighter's Sword";
        case 2: return "Master Sword";
        case 3: return "Tempered Sword";
        case 4: return "Golden Sword";
        default: return nullptr;
    }
}

const char* AlttpShieldName(uint8_t value)
{
    switch (value) {
        case 1: return "Fighter's Shield";
        case 2: return "Fire Shield";
        case 3: return "Mirror Shield";
        default: return nullptr;
    }
}

const char* AlttpArmorName(uint8_t value)
{
    switch (value) {
        case 1: return "Green Mail";
        case 2: return "Blue Mail";
        case 3: return "Red Mail";
        default: return nullptr;
    }
}

} // namespace oga
