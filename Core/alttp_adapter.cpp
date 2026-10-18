/*
 * alttp_adapter.cpp — The Legend of Zelda: A Link to the Past (SNES, USA 1.0).
 *
 * The reader for the SNES console in Core/snes_core.cpp. It reads the game's own
 * work RAM and speaks through the announcement queue; it never writes gameplay
 * state.
 *
 * ⛔ IDENTITY IS THE CRC. A Link to the Past is a 1991 cartridge with no usable
 * game code, so the registry key is "crc:777aac2f", which is what snes_core.cpp
 * writes into its code_out. Matching a different dump is impossible; a hack or a
 * translation has a different CRC and therefore no reader at all.
 *
 * ⛔ ADDRESSES COME FROM THE DECOMPILATION'S NAMES AND ARE CONFIRMED LIVE.
 * docs/design/alttp-memory.md records the value observed in each state. An
 * address that has not been confirmed is not spoken from.
 *
 * Task 1 of docs/plans/alttp-reader.md: identity, validation, and the debug dump.
 */
#include "adapter.h"

#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

const Host* g_host = nullptr;
bool g_attached = false;

/* ---- the work-RAM map (zelda3 src/variables.h, g_ram is banks 7E/7F) ---- */
constexpr uint32_t RAM_BASE          = 0x7E0000u;
constexpr uint32_t RAM_SIZE          = 0x20000u;
constexpr uint32_t FRAME_COUNTER     = 0x7E001Au;  /* frame_counter */
constexpr uint32_t PLAYER_INDOORS    = 0x7E001Bu;  /* player_is_indoors */
constexpr uint32_t LINK_Y            = 0x7E0020u;  /* link_y_coord */
constexpr uint32_t LINK_X            = 0x7E0022u;  /* link_x_coord */
constexpr uint32_t OVERWORLD_SCREEN  = 0x7E008Au;  /* overworld_screen_index */
constexpr uint32_t DUNGEON_ROOM      = 0x7E00A0u;  /* dungeon_room_index */
constexpr uint32_t OVERWORLD_AREA    = 0x7E040Au;  /* overworld_area_index */
constexpr uint32_t RUPEES            = 0x7EF362u;  /* link_rupees_actual */
constexpr uint32_t HEALTH_CAPACITY   = 0x7EF36Cu;  /* link_health_capacity */
constexpr uint32_t HEALTH_CURRENT    = 0x7EF36Du;  /* link_health_current */
constexpr uint32_t MAGIC             = 0x7EF36Eu;  /* link_magic_power */

uint8_t u8(uint32_t a)
{
    return (g_host && g_host->read8) ? g_host->read8(g_host->ctx, a) : 0;
}

uint16_t u16(uint32_t a)
{
    return (g_host && g_host->read16) ? g_host->read16(g_host->ctx, a) : 0;
}

bool InRam(uint32_t a, int n)
{
    return a >= RAM_BASE && (uint64_t) a + (uint64_t) n <= (uint64_t) RAM_BASE + RAM_SIZE;
}

/* ⛔ THE REFUSAL RULE. A capacity of 0 is a console that has not loaded a save
 * yet; health above capacity, or coordinates at the origin, are the same class
 * of "not a playable state". Speaking from any of them is the failure this
 * function exists to prevent. */
bool StateSane()
{
    if (!g_host) return false;
    if (!InRam(HEALTH_CAPACITY, 1) || !InRam(LINK_X, 2) || !InRam(LINK_Y, 2)) return false;

    uint8_t capacity = u8(HEALTH_CAPACITY);
    uint8_t current = u8(HEALTH_CURRENT);
    uint16_t x = u16(LINK_X);
    uint16_t y = u16(LINK_Y);

    /* 0x18 = three hearts, the smallest real maximum; 0xA0 = twenty hearts,
     * the largest the game can hold. Outside that, the read is not ALttP. */
    if (capacity < 0x18 || capacity > 0xA0) return false;
    if (current > capacity) return false;
    if (x == 0 && y == 0) return false;
    return true;
}

void Say(const char* text, const char* group, Priority pri)
{
    if (!text || !*text) return;
    if (!AdapterNoteSpoken(text) && pri != Priority::High) return;

    if (g_host && g_host->announce_q)
        announce(g_host->announce_q,
                 Announcement{text, pri, group, nullptr, 0, 0, -1},
                 g_host->now_ms);
    else if (g_host && g_host->speak)
        g_host->speak(g_host->ctx, text, pri == Priority::High);
}

void Log(const char* text)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, text);
}

void DumpState()
{
    char line[256];
    snprintf(line, sizeof(line),
             "crc:777aac2f health=%u/%u area=%u screen=%u room=%u indoors=%u magic=%u rupees=%u x=%u y=%u frame=%u",
             (unsigned) u8(HEALTH_CURRENT), (unsigned) u8(HEALTH_CAPACITY),
             (unsigned) u16(OVERWORLD_AREA), (unsigned) u16(OVERWORLD_SCREEN),
             (unsigned) u16(DUNGEON_ROOM), (unsigned) u8(PLAYER_INDOORS),
             (unsigned) u8(MAGIC), (unsigned) u16(RUPEES),
             (unsigned) u16(LINK_X), (unsigned) u16(LINK_Y),
             (unsigned) u8(FRAME_COUNTER));
    Log(line);
}

bool AlttpAttach(const Host* host)
{
    if (!host) return false;
    g_host = host;
    g_attached = true;
    return true;
}

void AlttpDetach()
{
    g_host = nullptr;
    g_attached = false;
}

void AlttpFrame()
{
    /* Task 1 has no per-frame work. Dialogue arrives in Task 7. */
}

bool AlttpReady()
{
    return g_attached && StateSane();
}

void AlttpCommand(Command cmd)
{
    if (cmd == Command::DumpState) { DumpState(); return; }

    /* ⛔ SILENT UNTIL THE GAME HAS STATE. A player command answered from a
     * booting console is the "bunch of numbers" failure the design forbids; the
     * UI does not offer the controls while ready() is false. */
    if (!AlttpReady()) return;
}

} // namespace

/* ⛔ A namespace-scope `const` object has INTERNAL linkage in C++ unless it is
 * declared extern first. Without this line the registry and every test see an
 * undefined symbol, which reads as a missing adapter. */
extern const Adapter kZeldaLinkToThePast;
const Adapter kZeldaLinkToThePast = {
    "alttp",
    "The Legend of Zelda: A Link to the Past",
    "crc:777aac2f",
    AlttpAttach,
    AlttpFrame,
    AlttpCommand,
    AlttpReady,
    AlttpDetach,
};

} // namespace oga
