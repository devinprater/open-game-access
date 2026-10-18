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
#include "alttp_text.h"
#include "alttp_names.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

const Host* g_host = nullptr;
bool g_attached = false;
unsigned g_lastMessage = 0xFFFFu;

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
constexpr uint32_t MAIN_MODULE        = 0x7E0010u;  /* main_module_index */
constexpr uint32_t SAVED_MODULE       = 0x7E010Cu;  /* saved_module_for_menu */
constexpr uint32_t DIALOGUE_INDEX     = 0x7E1CF0u;  /* dialogue_message_index */
constexpr uint32_t TEXT_BUFFER        = 0x7F1200u;  /* messaging_text_buffer */
/* Menus (measured 2026-10-17, docs/design/alttp-memory.md). */
constexpr uint32_t SUBMODULE          = 0x7E0011u;  /* submodule_index */
constexpr uint32_t HUD_MAP_STATE      = 0x7E0200u;  /* overworld_map_state */
constexpr uint32_t HUD_CUR_ITEM       = 0x7E0202u;  /* hud_cur_item */
constexpr uint32_t CHOICE_BOX         = 0x7E1CE8u;  /* choice_in_multiselect_box */
constexpr uint32_t ITEM_BASE          = 0x7EF340u;  /* link_item_bow ... */
constexpr uint32_t SWORD_TYPE         = 0x7EF359u;  /* link_sword_type */
constexpr uint32_t SHIELD_TYPE        = 0x7EF35Au;  /* link_shield_type */
constexpr uint32_t ARMOR_TYPE         = 0x7EF35Bu;  /* link_armor */

/* Interface submodules from the decomp's kMessagingSubmodules. */
enum : uint8_t {
    SUB_HUD = 1,          /* Hud_Module_Run: the item menu while module 14 is up */
    SUB_SAVE_MENU = 11,   /* Module0E_0B_SaveMenu: SELECT's continue/save box */
};

enum class Menu { None, Item, Save };
Menu g_menu = Menu::None;
uint16_t g_lastHudItem = 0xFFFFu;
uint16_t g_lastChoice = 0xFFFFu;

/* Module numbers from the decomp's kMainRouting (src/misc.c). */
enum : uint8_t {
    MOD_TITLE = 0, MOD_FILE_SELECT = 1, MOD_NAME_ENTRY = 4, MOD_LOAD_FILE = 5,
    MOD_DUNGEON = 7, MOD_OVERWORLD_LOAD = 8, MOD_OVERWORLD = 9,
    MOD_INTERFACE = 14, MOD_GAME_OVER = 18, MOD_SAVE_MENU = 23,
};

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

    /* ⛔ THE MEASURED BOOT GARBAGE IS INSIDE A NAIVE RANGE. At frame 0, before
     * the game initialises its RAM, the probe read capacity 0x7F and health 0x56
     * with non-zero coordinates — every "sane range" check passed on bytes that
     * are not a game. The game's own maximums are 0x18 (three hearts) through
     * 0xA0 (twenty), in steps of EIGHT (a heart container is 8 eighths), so the
     * step is the discriminator: 0x7F is not in that set.
     * See docs/design/alttp-memory.md, measured 2026-10-17. */
    if (capacity < 0x18 || capacity > 0xA0) return false;
    if (((capacity - 0x18) % 8) != 0) return false;
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
             "crc:777aac2f health=%u/%u area=%u screen=%u room=%u indoors=%u magic=%u rupees=%u x=%u y=%u frame=%u hud=%u choice=%u",
             (unsigned) u8(HEALTH_CURRENT), (unsigned) u8(HEALTH_CAPACITY),
             (unsigned) u16(OVERWORLD_AREA), (unsigned) u16(OVERWORLD_SCREEN),
             (unsigned) u16(DUNGEON_ROOM), (unsigned) u8(PLAYER_INDOORS),
             (unsigned) u8(MAGIC), (unsigned) u16(RUPEES),
             (unsigned) u16(LINK_X), (unsigned) u16(LINK_Y),
             (unsigned) u8(FRAME_COUNTER),
             (unsigned) u8(HUD_CUR_ITEM), (unsigned) u8(CHOICE_BOX));
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
    g_lastMessage = 0xFFFFu;
    g_menu = Menu::None;
    g_lastHudItem = 0xFFFFu;
    g_lastChoice = 0xFFFFu;
}

/* Module 14 is the interface/HUD layer; the real screen is in
 * saved_module_for_menu. Naming a screen from the wrong one is how a menu read
 * as "indoors". See docs/design/alttp-memory.md. */
uint8_t RealModule()
{
    uint8_t mod = u8(MAIN_MODULE);
    return mod == MOD_INTERFACE ? u8(SAVED_MODULE) : mod;
}

bool AlttpReady();

void SpeakItemMenu(bool withPrefix, bool withEquipment);
void SpeakSaveMenu(bool opening);

/* Module 14 with submodule 1 is the item menu, and module 14 with submodule 11
 * is SELECT's continue/save box. Both were measured on the public save; neither
 * can be told from the module number alone. */
Menu CurrentMenu()
{
    if (u8(MAIN_MODULE) != MOD_INTERFACE) return Menu::None;
    uint8_t sub = u8(SUBMODULE);
    if (sub == SUB_HUD) return Menu::Item;
    if (sub == SUB_SAVE_MENU) return Menu::Save;
    return Menu::None;
}

void AlttpFrame()
{
    if (!g_attached || !AlttpReady()) { g_menu = Menu::None; return; }

    Menu menu = CurrentMenu();

    if (menu != g_menu) {
        g_menu = menu;
        g_lastHudItem = 0xFFFFu;
        g_lastChoice = 0xFFFFu;
        if (menu == Menu::Item) {
            g_lastHudItem = u8(HUD_CUR_ITEM);
            SpeakItemMenu(true, true);
        } else if (menu == Menu::Save) {
            g_lastChoice = u8(CHOICE_BOX);
            SpeakSaveMenu(true);
        }
        /* Leaving a menu says nothing: the game's own transition is enough. */
        return;
    }

    /* ⛔ A MENU OWNS THE FRAME. The save box's message index changes with the
     * cursor (390 -> 9 -> 10), and its buffer is cursor decoration, not prose;
     * the dialogue reader must not speak it. The index is marked as seen so
     * closing the menu cannot replay that decoration as a new line. */
    if (menu != Menu::None) {
        g_lastMessage = u16(DIALOGUE_INDEX);
        if (menu == Menu::Item) {
            uint8_t hud = u8(HUD_CUR_ITEM);
            if (hud != g_lastHudItem) {
                g_lastHudItem = hud;
                SpeakItemMenu(false, false);
            }
        } else if (menu == Menu::Save) {
            uint8_t choice = u8(CHOICE_BOX);
            if (choice != g_lastChoice) {
                g_lastChoice = choice;
                SpeakSaveMenu(false);
            }
        }
        return;
    }

    /* ⛔ THE GAME HAS ALREADY DECODED ITS TEXT. messaging_text_buffer holds font
     * tile indices for the current message; a new index means new words. */
    uint16_t msg = u16(DIALOGUE_INDEX);
    if (msg == g_lastMessage) return;
    g_lastMessage = msg;
    if (msg == 0) return;

    uint8_t buf[128];
    for (int i = 0; i < (int) sizeof(buf); i++) {
        uint32_t v = 0;
        if (!g_host->read8) return;
        v = g_host->read8(g_host->ctx, TEXT_BUFFER + (uint32_t) i);
        buf[i] = (uint8_t) v;
    }

    char text[192];
    int n = AlttpDecodeText(buf, (int) sizeof(buf), text, (int) sizeof(text));

    /* ⛔ MENU DECORATION IS NOT SPEECH. Message 9 and 10 are cursor glyphs, and
     * they decode to punctuation alone; a line with no letter or digit is never
     * worth saying. */
    bool words = false;
    for (int i = 0; i < n; i++)
        if (isalnum((unsigned char) text[i])) { words = true; break; }
    if (words) Say(text, "dialogue", Priority::Normal);
}

bool AlttpReady()
{
    return g_attached && StateSane();
}

/* Where am I, in the game's own numbers until Task 4 gives them names.
 * The coordinates are deliberately not spoken: a raw X/Y means nothing to a
 * player, and the useful version of that answer is guidance, which is a later
 * plan. */
/* 0..999 in words, for rupees. Small and boring on purpose: a generic
 * number-to-words library is a dependency this adapter does not need. */
void NumberWords(unsigned n, char* out, size_t cap)
{
    static const char* const kSmall[] = {
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
        "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen"
    };
    static const char* const kTens[] = {
        "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
    };

    if (n < 20) { snprintf(out, cap, "%s", kSmall[n]); return; }
    if (n < 100) {
        if (n % 10 == 0) snprintf(out, cap, "%s", kTens[n / 10]);
        else snprintf(out, cap, "%s-%s", kTens[n / 10], kSmall[n % 10]);
        return;
    }
    unsigned hundreds = n / 100, rest = n % 100;
    if (rest == 0) { snprintf(out, cap, "%s hundred", kSmall[hundreds]); return; }
    char rest_words[48];
    NumberWords(rest, rest_words, sizeof(rest_words));
    snprintf(out, cap, "%s hundred %s", kSmall[hundreds], rest_words);
}

/* The item menu's value byte. hud 16 is the bottle slot in the original
 * inventory, and it stores a bottle index rather than an item count; 21-24 are
 * the newer inventory's bottles. Neither is read as an item level. */
uint8_t HudItemValue(uint8_t hud)
{
    if (hud == 0 || hud == 16 || hud > 20) return 0;
    return u8(ITEM_BASE + (uint32_t) (hud - 1));
}

void SpeakItemMenu(bool withPrefix, bool withEquipment)
{
    uint8_t hud = u8(HUD_CUR_ITEM);
    const char* name = AlttpItemName(hud, HudItemValue(hud));
    char item[128];

    if (hud == 0) {
        snprintf(item, sizeof(item), "No item chosen.");
    } else if (name && hud <= 20) {
        /* The menu draws four columns; the cursor's row and column are worth
         * saying because they are the only spatial fact a blind player has. */
        char row[16], column[16];
        NumberWords((unsigned) ((hud - 1) / 4 + 1), row, sizeof(row));
        NumberWords((unsigned) ((hud - 1) % 4 + 1), column, sizeof(column));
        snprintf(item, sizeof(item), "%s, row %s, column %s.", name, row, column);
    } else if (name) {
        snprintf(item, sizeof(item), "%s.", name);
    } else {
        /* ⛔ AN UNKNOWN SLOT SPEAKS ITS NUMBER. Calling it empty would be a
         * guess about a slot this table has never seen. */
        snprintf(item, sizeof(item), "Item %u.", (unsigned) hud);
    }

    char line[256];
    snprintf(line, sizeof(line), "%s%s", withPrefix ? "Item menu. " : "", item);

    if (withEquipment) {
        const char* parts[3] = {
            AlttpSwordName(u8(SWORD_TYPE)),
            AlttpShieldName(u8(SHIELD_TYPE)),
            AlttpArmorName(u8(ARMOR_TYPE)),
        };
        char eq[128] = {0};
        size_t used = 0;
        for (const char* p : parts) {
            if (!p) continue;
            size_t n = strlen(p);
            if (used + n + 3 >= sizeof(eq)) break;
            if (used) { eq[used++] = ','; eq[used++] = ' '; }
            memcpy(eq + used, p, n);
            used += n;
            eq[used] = 0;
        }
        if (eq[0]) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof(line) - n, " %s.", eq);
        }
    }

    Say(line, "itemmenu", Priority::High);
}

const char* SaveChoiceName(uint8_t choice)
{
    switch (choice) {
        case 0: return "Continue game";
        case 1: return "Save and quit";
        default: return nullptr;
    }
}

void SpeakSaveMenu(bool opening)
{
    uint8_t choice = u8(CHOICE_BOX);
    const char* name = SaveChoiceName(choice);
    char line[192];

    if (name) {
        if (opening)
            snprintf(line, sizeof(line),
                     "Save menu. Continue game, or save and quit. %s selected.", name);
        else
            snprintf(line, sizeof(line), "%s selected.", name);
    } else {
        if (opening)
            snprintf(line, sizeof(line),
                     "Save menu. Continue game, or save and quit. Choice %u selected.",
                     (unsigned) choice);
        else
            snprintf(line, sizeof(line), "Choice %u selected.", (unsigned) choice);
    }
    Say(line, "savemenu", Priority::High);
}

void SpeakStatus()
{
    /* ⛔ NO GAME, NO HEARTS. The file select, the title and name entry leave the
     * last file's health in work RAM, and every one of those values is inside a
     * naive range check. Speaking yesterday's hearts on the file select is the
     * "bunch of numbers" failure with a friendly voice. */
    uint8_t mod = RealModule();
    if (mod == MOD_FILE_SELECT || mod == MOD_TITLE || mod == MOD_NAME_ENTRY) {
        Say("No game in progress.", "status", Priority::High);
        return;
    }

    unsigned eighths = u8(HEALTH_CURRENT);
    char line[192];

    /* ⛔ ZERO HEARTS IS AN ANSWER, not a failed read. Going quiet at the moment
     * the player is dying is the worst possible time to be silent. */
    if (eighths == 0) { Say("Zero hearts.", "status", Priority::High); return; }

    unsigned whole = eighths / 8;
    unsigned rem = eighths % 8;
    char hearts[80];
    if (whole == 1 && rem == 0) {
        snprintf(hearts, sizeof(hearts), "One heart.");
    } else {
        char nw[48];
        NumberWords(whole, nw, sizeof(nw));
        if (nw[0]) nw[0] = (char) toupper((unsigned char) nw[0]);
        if (rem == 4) snprintf(hearts, sizeof(hearts), "%s and a half hearts.", nw);
        else snprintf(hearts, sizeof(hearts), "%s hearts.", nw);
    }

    unsigned magic = u8(MAGIC);
    char magic_part[48] = {0};
    if (magic > 0) {
        if (magic >= 0x80) snprintf(magic_part, sizeof(magic_part), " Magic full.");
        else if (magic >= 0x60) snprintf(magic_part, sizeof(magic_part), " Magic three quarters.");
        else if (magic >= 0x40) snprintf(magic_part, sizeof(magic_part), " Magic half.");
        else snprintf(magic_part, sizeof(magic_part), " Magic a quarter.");
    }

    unsigned rupees = u16(RUPEES);
    char rupee_words[64];
    NumberWords(rupees, rupee_words, sizeof(rupee_words));
    if (rupee_words[0]) rupee_words[0] = (char) toupper((unsigned char) rupee_words[0]);
    snprintf(line, sizeof(line), "%s%s %s %s", hearts, magic_part, rupee_words,
             rupees == 1 ? "rupee." : "rupees.");
    Say(line, "status", Priority::High);
}

void SpeakWhereAmI()
{
    /* ⛔ A MENU IS THE ANSWER WHILE IT IS UP. Module 14 resolves through
     * saved_module_for_menu, so without this the item menu would answer with
     * the room underneath it. */
    Menu menu = CurrentMenu();
    if (menu == Menu::Item) { SpeakItemMenu(true, false); return; }
    if (menu == Menu::Save) { SpeakSaveMenu(true); return; }

    char line[96];
    uint8_t mod = RealModule();

    /* The screen comes from the module table first; the room only when the game
     * says it is an interior. */
    if (mod == MOD_FILE_SELECT)      { Say("File select.", "whereami", Priority::High); return; }
    if (mod == MOD_NAME_ENTRY)       { Say("Name entry.", "whereami", Priority::High); return; }
    if (mod == MOD_SAVE_MENU)        { Say("Save menu.", "whereami", Priority::High); return; }
    if (mod == MOD_GAME_OVER)        { Say("Game over.", "whereami", Priority::High); return; }
    if (mod == MOD_TITLE)            { Say("Title screen.", "whereami", Priority::High); return; }
    if (mod == MOD_LOAD_FILE)        { Say("Loading a file.", "whereami", Priority::High); return; }

    if (u8(PLAYER_INDOORS) != 0) {
        /* ⛔ MEASURED: a house uses the same room index as a dungeon room (the
         * probe read room 260 indoors at Link's house). "Dungeon room" would be
         * wrong there, and the index alone does not say which dungeon. */
        snprintf(line, sizeof(line), "Indoors, room %u.", (unsigned) u16(DUNGEON_ROOM));
    } else {
        unsigned area = u16(OVERWORLD_AREA);
        const char* name = AlttpAreaName((uint8_t) area);
        if (name) {
            if (area >= 0x40)
                snprintf(line, sizeof(line), "Outside in the dark world, %s.", name);
            else
                snprintf(line, sizeof(line), "Outside, %s.", name);
        } else if (area >= 0x40) {
            snprintf(line, sizeof(line), "Outside in the dark world, area %u.", area);
        } else {
            snprintf(line, sizeof(line), "Outside, area %u, screen %u.",
                     area, (unsigned) u16(OVERWORLD_SCREEN));
        }
    }
    Say(line, "whereami", Priority::High);
}

void AlttpCommand(Command cmd)
{
    if (cmd == Command::DumpState) { DumpState(); return; }

    /* ⛔ SILENT UNTIL THE GAME HAS STATE. A player command answered from a
     * booting console is the "bunch of numbers" failure the design forbids; the
     * UI does not offer the controls while ready() is false. */
    if (!AlttpReady()) return;

    switch (cmd) {
        case Command::WhereAmI: SpeakWhereAmI(); break;
        case Command::Status:   SpeakStatus(); break;
        default: break;
    }
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
