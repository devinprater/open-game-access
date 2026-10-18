/*
 * alttp_names.cpp — a small curated area table.
 *
 * Sources, stated so a later reader can check them:
 *  - 0x2C Link's House: MEASURED. The public save whose own description says
 *    "Start From Link's House" loads at area 44 (0x2C) and, once outdoors,
 *    reports exactly that area.
 *  - 0x13 Sanctuary, 0x14 Graveyard, 0x1B Hyrule Castle, 0x21 Kakariko Village,
 *    0x39 Desert of Mystery, 0x3A Desert Passage: the map squares named in The
 *    Cutting Room Floor's overworld development tables.
 *  - The dark-world Death Mountain, Skull Woods and Turtle Rock squares come
 *    from the same tables (0x42-0x44/0x4B/0x4C, 0x45/0x46/0x4D/0x4E,
 *    0x40/0x41/0x48/0x49, 0x47).
 *
 * ⛔ DO NOT GUESS THE REST. An unknown area speaks its number; a wrong name sends
 * a blind player the wrong way, which is worse than a number.
 */
#include "alttp_names.h"

namespace oga {

namespace {
struct Entry { uint8_t area; const char* name; };

const Entry kNames[] = {
    { 0x13, "Sanctuary" },
    { 0x14, "Graveyard" },
    { 0x1B, "Hyrule Castle" },
    { 0x21, "Kakariko Village" },
    { 0x2C, "Link's House" },
    { 0x39, "Desert of Mystery" },
    { 0x3A, "Desert Passage" },

    /* Dark world. */
    { 0x40, "Skull Woods" },
    { 0x41, "Skull Woods" },
    { 0x48, "Skull Woods" },
    { 0x49, "Skull Woods" },
    { 0x42, "Death Mountain" },
    { 0x43, "Death Mountain" },
    { 0x44, "Death Mountain" },
    { 0x4B, "Death Mountain" },
    { 0x4C, "Death Mountain" },
    { 0x45, "Death Mountain East" },
    { 0x46, "Death Mountain East" },
    { 0x4D, "Death Mountain East" },
    { 0x4E, "Death Mountain East" },
    { 0x47, "Turtle Rock" },
};
} // namespace

const char* AlttpAreaName(uint8_t area)
{
    for (const Entry& e : kNames)
        if (e.area == area) return e.name;
    return nullptr;
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
