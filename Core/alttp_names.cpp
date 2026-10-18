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

} // namespace oga
