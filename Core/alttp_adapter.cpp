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
#include "alttp_rooms.h"
#include "alttp_graph.h"
#include "alttp_dungeon_graph.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

const Host* g_host = nullptr;
bool g_attached = false;
unsigned g_lastMessage = 0xFFFFu;
/* Message text is identified by its CONTENT, not only by its index: one message
 * can have several pages, and a page advance keeps the index while rewriting the
 * buffer. The two-frame settle keeps a half-written page from being spoken. */
uint32_t g_lastTextHash = 0;
uint32_t g_spokenTextHash = 0;
/* The story stage the guide cycle was last aimed at. 0xFE is "never aimed", so
 * the first press after attach points at the current objective. */
uint8_t g_objectiveArea = 0xFE;
/* The world the guide cycle was last aimed in (0 light, 1 dark); switching
 * worlds re-aims the cycle like a story-stage change does. */
uint8_t g_guideWorld = 0xFF;
/* The dungeon component the indoor guide cycle was last aimed in, and the
 * cycle position within it (big chest, boss, entrance). */
uint16_t g_indoorComponent = 0xFFFF;
unsigned g_indoorIndex = 0;

/* ---- the work-RAM map (zelda3 src/variables.h, g_ram is banks 7E/7F) ---- */
constexpr uint32_t RAM_BASE          = 0x7E0000u;
constexpr uint32_t RAM_SIZE          = 0x20000u;
constexpr uint32_t FRAME_COUNTER     = 0x7E001Au;  /* frame_counter */
constexpr uint32_t PLAYER_INDOORS    = 0x7E001Bu;  /* player_is_indoors */
constexpr uint32_t LINK_Y            = 0x7E0020u;  /* link_y_coord */
constexpr uint32_t LINK_X            = 0x7E0022u;  /* link_x_coord */
constexpr uint32_t OVERWORLD_SCREEN  = 0x7E008Au;  /* overworld_screen_index */
constexpr uint32_t DUNGEON_ROOM      = 0x7E00A0u;  /* dungeon_room_index */
constexpr uint32_t DUNGEON_INDEX     = 0x7E040Cu;  /* cur_palace_index_x2, low byte */
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
constexpr uint32_t BOTTLE_INDEX       = 0x7EF34Fu;  /* link_item_bottle_index, 1-4 */
constexpr uint32_t BOTTLE_INFO        = 0x7EF35Cu;  /* link_bottle_info[4] */
constexpr uint32_t PROGRESS_FLAGS     = 0x7EF3C6u;  /* sram_progress_flags */
constexpr uint32_t PROGRESS_INDICATOR = 0x7EF3C5u;  /* sram_progress_indicator */
constexpr uint32_t PENDANTS           = 0x7EF374u;  /* link_which_pendants */
constexpr uint32_t CRYSTALS           = 0x7EF37Au;  /* link_which_crystals */
/* The overworld tile-attribute map (overworld_tileattr) and the scroll
 * window it is addressed through (overworld_offset_base/mask x/y). The
 * guide flood-fills the current screen from these, exactly as the game's
 * own Overworld_GetTileAttributeAtLocation addresses them. */
constexpr uint32_t TILEATTR           = 0x7E2000u;  /* overworld_tileattr */
constexpr uint32_t OW_BASE_Y          = 0x7E0708u;  /* overworld_offset_base_y */
constexpr uint32_t OW_MASK_Y          = 0x7E070Au;  /* overworld_offset_mask_y */
constexpr uint32_t OW_BASE_X          = 0x7E070Cu;  /* overworld_offset_base_x */
constexpr uint32_t OW_MASK_X          = 0x7E070Eu;  /* overworld_offset_mask_x */
constexpr uint32_t KEYS               = 0x7EF36Fu;  /* link_num_keys */
constexpr uint32_t BIG_KEY            = 0x7EF366u;  /* link_bigkey, one bit per dungeon */
constexpr uint32_t COMPASS            = 0x7EF364u;  /* link_compass, one bit per dungeon */
constexpr uint32_t DUNGEON_MAP        = 0x7EF368u;  /* link_dungeon_map, one bit per dungeon */

/* The file select (module 1). Its slots are NOT in work RAM: select_file.c draws
 * them straight out of the cartridge's save RAM, which the core exposes at the
 * LoROM window 0x700000. Cursor is selectfile_R16 (0x7E00C8). */
constexpr uint32_t FILE_CURSOR        = 0x7E00C8u;  /* selectfile_R16 */
constexpr uint32_t SRAM_BASE          = 0x700000u;  /* SNES_SRAM_BASE */
constexpr uint32_t SRAM_SLOT          = 0x500u;
constexpr uint32_t SRAM_EXISTS        = 0x3E5u;     /* 0x55AA when the slot is used */
constexpr uint32_t SRAM_NAME          = 0x3D9u;     /* 6 x 16-bit glyphs */
constexpr uint32_t SRAM_HEALTH        = 0x36Cu;     /* heart capacity, eighths */
constexpr uint32_t SRAM_SWORD         = 0x359u;
constexpr uint32_t SRAM_SHIELD        = 0x35Au;

/* Interface submodules from the decomp's kMessagingSubmodules. */
enum : uint8_t {
    SUB_HUD = 1,          /* Hud_Module_Run: the item menu while module 14 is up */
    SUB_SAVE_MENU = 11,   /* Module0E_0B_SaveMenu: SELECT's continue/save box */
};

enum class Menu { None, Item, Save, FileSelect };
Menu g_menu = Menu::None;
uint16_t g_lastHudItem = 0xFFFFu;
uint16_t g_lastChoice = 0xFFFFu;
uint16_t g_lastFileCursor = 0xFFFFu;
uint16_t g_lastArea = 0xFFFFu;   /* last outdoors area SPOKEN, for area entry */
bool g_bottleMenu = false;       /* the item menu's bottle submenu is up */
uint16_t g_lastBottle = 0xFFFFu;

/* Guidance: the index into kGuideTargets the player last heard. The player
 * cycles with Command::Guide; it is not saved, so it restarts at the first
 * landmark whenever the adapter attaches. */
unsigned g_guideIndex = 0;

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

/* FNV-1a over the first 96 bytes of the message buffer. 96 covers the longest
 * page the box shows; the tail belongs to the next page or the next message. */
uint32_t TextHash()
{
    if (!g_host || !g_host->read8) return 0;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 96; i++) {
        hash ^= (uint32_t) g_host->read8(g_host->ctx, TEXT_BUFFER + (uint32_t) i);
        hash *= 16777619u;
    }
    return hash;
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
    char line[320];
    snprintf(line, sizeof(line),
             "crc:777aac2f health=%u/%u area=%u screen=%u room=%u indoors=%u magic=%u rupees=%u x=%u y=%u frame=%u hud=%u choice=%u guide=%u prog=%u flags=%02x pend=%u",
             (unsigned) u8(HEALTH_CURRENT), (unsigned) u8(HEALTH_CAPACITY),
             (unsigned) u16(OVERWORLD_AREA), (unsigned) u16(OVERWORLD_SCREEN),
             (unsigned) u16(DUNGEON_ROOM), (unsigned) u8(PLAYER_INDOORS),
             (unsigned) u8(MAGIC), (unsigned) u16(RUPEES),
             (unsigned) u16(LINK_X), (unsigned) u16(LINK_Y),
             (unsigned) u8(FRAME_COUNTER),
             (unsigned) u8(HUD_CUR_ITEM), (unsigned) u8(CHOICE_BOX),
             (unsigned) g_guideIndex,
             (unsigned) u8(PROGRESS_INDICATOR), (unsigned) u8(PROGRESS_FLAGS),
             (unsigned) u8(PENDANTS));
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
    g_lastFileCursor = 0xFFFFu;
    g_lastArea = 0xFFFFu;
    g_bottleMenu = false;
    g_lastBottle = 0xFFFFu;
    g_guideIndex = 0;
    g_lastTextHash = 0;
    g_spokenTextHash = 0;
    g_objectiveArea = 0xFE;
    g_guideWorld = 0xFF;
    g_indoorComponent = 0xFFFF;
    g_indoorIndex = 0;
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
void SpeakFileSelect(bool withPrefix);
void SpeakBottle(bool opening);

/* Module 14 with submodule 1 is the item menu, and module 14 with submodule 11
 * is SELECT's continue/save box. Both were measured on the public save; neither
 * can be told from the module number alone. */
Menu CurrentMenu()
{
    uint8_t mod = u8(MAIN_MODULE);
    if (mod == MOD_FILE_SELECT) {
        /* ⛔ THE FADE-IN IS NOT THE SCREEN. Measured: while the file select is
         * being set up (submodule 1), 0x7E00C8 holds garbage (216, 77, 155, 85)
         * and only settles to the real cursor when submodule reaches 4/5. A
         * reader that trusts the cursor during setup announces "Erase file" over
         * a screen that is still drawing. */
        return u8(SUBMODULE) >= 4 ? Menu::FileSelect : Menu::None;
    }
    if (mod != MOD_INTERFACE) return Menu::None;
    uint8_t sub = u8(SUBMODULE);
    if (sub == SUB_HUD) return Menu::Item;
    if (sub == SUB_SAVE_MENU) return Menu::Save;
    return Menu::None;
}

void AlttpFrame()
{
    if (!g_attached) return;

    Menu menu = CurrentMenu();

    /* ⛔ THE FILE SELECT COMES BEFORE `ready()`. Its slots are in save RAM, and
     * the work-RAM health that ready() validates belongs to no game yet. */
    if (menu == Menu::FileSelect) {
        g_lastMessage = u16(DIALOGUE_INDEX);
        g_lastTextHash = TextHash();
        if (menu != g_menu) {
            g_menu = menu;
            g_lastFileCursor = u8(FILE_CURSOR);
            SpeakFileSelect(true);
            return;
        }
        uint8_t cursor = u8(FILE_CURSOR);
        if (cursor != g_lastFileCursor) {
            g_lastFileCursor = cursor;
            SpeakFileSelect(false);
        }
        return;
    }

    if (!AlttpReady()) { g_menu = Menu::None; return; }

    if (menu != g_menu) {
        g_menu = menu;
        g_lastHudItem = 0xFFFFu;
        g_lastChoice = 0xFFFFu;
        g_bottleMenu = false;
        g_lastBottle = 0xFFFFu;
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
        g_lastTextHash = TextHash();
        if (menu == Menu::Item) {
            uint8_t hud = u8(HUD_CUR_ITEM);

            /* ⛔ THE BOTTLE SLOT OPENS A SECOND MENU. hud 16 is the original
             * inventory's bottle slot; once its submenu is up (map state 7+),
             * hud_cur_item stays 16 while link_item_bottle_index moves 1-4.
             * Watching only the item cursor would leave this menu silent. */
            if (hud == 16 && u8(HUD_MAP_STATE) >= 7) {
                uint8_t index = u8(BOTTLE_INDEX);
                if (!g_bottleMenu || index != g_lastBottle) {
                    bool opening = !g_bottleMenu;
                    g_bottleMenu = true;
                    g_lastBottle = index;
                    SpeakBottle(opening);
                }
                return;
            }
            g_bottleMenu = false;

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

    /* ⛔ INDOORS CLEARS THE MEMORY, so the first outdoor frame after a house or a
     * dungeon announces the area the player is entering. The first outdoor area
     * after a load also speaks: landing somewhere and hearing it beats silence. */
    if (u8(PLAYER_INDOORS) != 0) {
        g_lastArea = 0xFFFFu;
    } else if (RealModule() == MOD_OVERWORLD) {
        uint16_t area = u16(OVERWORLD_AREA);
        if (area != g_lastArea) {
            g_lastArea = area;
            const char* name = AlttpAreaName((uint8_t) area);
            char line[96];
            if (name) {
                if (area >= 0x40)
                    snprintf(line, sizeof(line), "Outside in the dark world, %s.", name);
                else
                    snprintf(line, sizeof(line), "Outside, %s.", name);
            } else if (area >= 0x40) {
                snprintf(line, sizeof(line), "Outside in the dark world, area %u.", area);
            } else {
                snprintf(line, sizeof(line), "Outside, area %u.", area);
            }
            Say(line, "area", Priority::Normal);
        }
    }

    /* ⛔ THE GAME HAS ALREADY DECODED ITS TEXT, AND ONE MESSAGE CAN HAVE MANY
     * PAGES. The index names the message, not the page, so a page advance keeps
     * it while rewriting the buffer; watching only the index drops every page
     * after the first. The content hash is the real signal, with one frame of
     * settle so a half-written page is never spoken. */
    uint32_t hash = TextHash();
    /* ⛔ A ZERO INDEX IS NO MESSAGE. An empty buffer decodes to a run of "A"
     * glyphs, so content alone would speak nonsense between messages. */
    uint16_t msg = u16(DIALOGUE_INDEX);
    if (msg == 0) { g_lastTextHash = hash; g_spokenTextHash = hash; return; }
    if (hash != g_lastTextHash) { g_lastTextHash = hash; return; }
    if (hash == g_spokenTextHash) return;

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
     * worth saying. Mark it seen so the frame loop does not retry it. */
    bool words = false;
    for (int i = 0; i < n; i++)
        if (isalnum((unsigned char) text[i])) { words = true; break; }

    g_spokenTextHash = hash;
    g_lastMessage = u16(DIALOGUE_INDEX);
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

void SpeakBottle(bool opening)
{
    uint8_t index = u8(BOTTLE_INDEX);
    char line[128];
    const char* prefix = opening ? "Bottle menu. " : "";

    if (index < 1 || index > 4) {
        snprintf(line, sizeof(line), "%sNo bottle selected.", prefix);
        Say(line, "itemmenu", Priority::High);
        return;
    }

    char ordinal[16];
    NumberWords(index, ordinal, sizeof(ordinal));

    uint8_t info = u8(BOTTLE_INFO + (uint32_t) (index - 1));
    const char* name = AlttpBottleName(info);
    if (name)
        snprintf(line, sizeof(line), "%sBottle %s, %s.", prefix, ordinal, name);
    else
        snprintf(line, sizeof(line), "%sBottle %s, contents %u.", prefix, ordinal,
                 (unsigned) info);
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

/* The file select's name field. Measured on the public save: its four glyphs
 * decode to "Link" (0x0B, 0xC0, 0x47, 0x44). The game stores a transformed
 * name-entry tile, chr = (t & 0xFFF0) * 2 + (t & 0xF); invert it, then use the
 * game's font order. 0xA9 pads the field and ends the name.
 *
 * ⛔ AN UNREADABLE NAME IS NOT GUESSED. One glyph this table does not know makes
 * the whole name unavailable, and the slot is spoken without it. */
bool FileNameFromSram(uint32_t base, char* out, int cap)
{
    int n = 0;
    for (int i = 0; i < 6; i++) {
        uint16_t v = u16(base + SRAM_NAME + (uint32_t) i * 2u);
        if (v == 0xA9 || v == 0x0000) break;

        uint16_t low = v & 0xFu;
        if (((v - low) & 1u) != 0) return false;
        uint16_t t = (uint16_t) (((v - low) / 2u) | low);

        char c;
        if (t <= 0x19)                     c = (char) ('A' + t);
        else if (t <= 0x33)                c = (char) ('a' + (t - 0x1A));
        else if (t >= 0x34 && t <= 0x3D)   c = (char) ('0' + (t - 0x34));
        else if (t == 0x60)                c = 'i';   /* the name font's dotted i */
        else if (t == 0x61)                c = 'j';
        else return false;

        if (n >= cap - 1) break;
        out[n++] = c;
    }
    out[n] = 0;
    return n > 0;
}

void SpeakFileSelect(bool withPrefix)
{
    uint8_t cursor = u8(FILE_CURSOR);
    char line[192];
    const char* prefix = withPrefix ? "File select. " : "";

    if (cursor >= 3) {
        snprintf(line, sizeof(line), "%s%s.", prefix,
                 cursor == 3 ? "Copy file" : "Erase file");
        Say(line, "fileselect", Priority::High);
        return;
    }

    char number[16];
    NumberWords((unsigned) cursor + 1u, number, sizeof(number));

    uint32_t base = SRAM_BASE + (uint32_t) cursor * SRAM_SLOT;
    if (u16(base + SRAM_EXISTS) != 0x55AAu) {
        snprintf(line, sizeof(line), "%sFile %s, empty.", prefix, number);
        Say(line, "fileselect", Priority::High);
        return;
    }

    char name[8] = {0};
    bool named = FileNameFromSram(base, name, (int) sizeof(name));

    unsigned capacity = u8(base + SRAM_HEALTH);
    char hearts[48] = {0};
    if (capacity >= 0x18) {
        char words[16];
        NumberWords(capacity / 8u, words, sizeof(words));
        words[0] = (char) toupper((unsigned char) words[0]);
        snprintf(hearts, sizeof(hearts), " %s %s.", words,
                 capacity / 8u == 1 ? "heart" : "hearts");
    }

    const char* sword = AlttpSwordName(u8(base + SRAM_SWORD));
    const char* shield = AlttpShieldName(u8(base + SRAM_SHIELD));
    char equip[96] = {0};
    if (sword && shield) snprintf(equip, sizeof(equip), " %s, %s.", sword, shield);
    else if (sword)      snprintf(equip, sizeof(equip), " %s.", sword);
    else if (shield)     snprintf(equip, sizeof(equip), " %s.", shield);

    if (named)
        snprintf(line, sizeof(line), "%sFile %s, %s.%s%s", prefix, number, name, hearts, equip);
    else
        snprintf(line, sizeof(line), "%sFile %s.%s%s", prefix, number, hearts, equip);

    Say(line, "fileselect", Priority::High);
}

void SpeakStatus()
{
    /* ⛔ NO GAME, NO HEARTS. The file select, the title and name entry leave the
     * last file's health in work RAM, and every one of those values is inside a
     * naive range check. Speaking yesterday's hearts on the file select is the
     * "bunch of numbers" failure with a friendly voice. */
    uint8_t mod = RealModule();
    if (mod == MOD_FILE_SELECT) { SpeakFileSelect(false); return; }
    if (mod == MOD_TITLE || mod == MOD_NAME_ENTRY) {
        Say("No game in progress.", "status", Priority::High);
        return;
    }

    unsigned eighths = u8(HEALTH_CURRENT);
    char line[256];

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

    /* ---- the dungeon pouch -------------------------------------------------
     * ⛔ ONLY INSIDE A REAL DUNGEON. 0xFF is "no dungeon"; 0 is the sewer
     * passage and a real dungeon. The three flags are bitfields with one bit per
     * dungeon id, and the id is cur_palace_index_x2 / 2. Nothing here is spoken
     * outdoors or in a house, where those bits belong to no dungeon. */
    char dungeon_part[112] = {0};
    if (u8(PLAYER_INDOORS) != 0) {
        uint8_t raw = u8(DUNGEON_INDEX);
        if (raw != 0xFF) {
            unsigned id = (unsigned) raw / 2u;
            if (id < 16u) {
                unsigned keys = u8(KEYS);
                char key_words[48];
                if (keys == 0) {
                    snprintf(key_words, sizeof(key_words), "no keys");
                } else {
                    NumberWords(keys, key_words, sizeof(key_words));
                    size_t used = strlen(key_words);
                    snprintf(key_words + used, sizeof(key_words) - used, "%s",
                             keys == 1 ? " key" : " keys");
                }
                if (key_words[0]) key_words[0] = (char) toupper((unsigned char) key_words[0]);
                bool big = ((u16(BIG_KEY) >> id) & 1u) != 0;
                bool map = ((u16(DUNGEON_MAP) >> id) & 1u) != 0;
                bool compass = ((u16(COMPASS) >> id) & 1u) != 0;
                snprintf(dungeon_part, sizeof(dungeon_part), " %s, %s, %s, %s.",
                         key_words,
                         big ? "big key" : "no big key",
                         map ? "map" : "no map",
                         compass ? "compass" : "no compass");
            }
        }
    }

    snprintf(line, sizeof(line), "%s%s %s %s%s", hearts, magic_part, rupee_words,
             rupees == 1 ? "rupee." : "rupees.", dungeon_part);
    Say(line, "status", Priority::High);
}

void SpeakWhereAmI()
{
    /* ⛔ A MENU IS THE ANSWER WHILE IT IS UP. Module 14 resolves through
     * saved_module_for_menu, so without this the item menu would answer with
     * the room underneath it. */
    Menu menu = CurrentMenu();
    if (menu == Menu::FileSelect) { SpeakFileSelect(true); return; }
    if (menu == Menu::Item) { SpeakItemMenu(true, false); return; }
    if (menu == Menu::Save) { SpeakSaveMenu(true); return; }

    char line[256];
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
         * probe read room 260 indoors at Link's house). The dungeon id is what
         * separates them: 0xFF means none, and only then does the room number
         * stand alone. 0 is the sewers, a real dungeon. */
        const char* dungeon = AlttpDungeonName(u8(DUNGEON_INDEX));
        if (dungeon)
            snprintf(line, sizeof(line), "Inside %s, room %u.", dungeon, (unsigned) u16(DUNGEON_ROOM));
        else
            snprintf(line, sizeof(line), "Indoors, room %u.", (unsigned) u16(DUNGEON_ROOM));

        /* ⛔ ONLY A REAL DUNGEON GETS A ROOM NOTE. Houses share the same room
         * index space, and the table would otherwise lend a house a dungeon
         * room's chest. The contents are derived facts from the decompilation's
         * room database, not read from the cartridge. */
        if (dungeon) {
            const RoomNote* note = AlttpRoomNote((uint16_t) u16(DUNGEON_ROOM));
            if (note) {
                size_t used = strlen(line);
                if (note->chests == 1) {
                    snprintf(line + used, sizeof(line) - used, " One %schest.",
                             note->big ? "big " : "");
                } else if (note->chests > 1) {
                    char nw[32];
                    NumberWords(note->chests, nw, sizeof(nw));
                    if (nw[0]) nw[0] = (char) toupper((unsigned char) nw[0]);
                    snprintf(line + used, sizeof(line) - used, " %s chests.", nw);
                }
                if (note->note) {
                    used = strlen(line);
                    snprintf(line + used, sizeof(line) - used, " %s.", note->note);
                }
            }
        }
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

/* ---- guidance -------------------------------------------------------------
 * ⛔ A ROUTE IS A CLAIM, SO IT IS ONLY SPOKEN WHEN IT IS PROVEN. The walking
 * graph in alttp_graph.cpp is generated offline from the game's own tile
 * assembly (scripts/gen-alttp-graph.py): per screen, the walkable regions
 * and the border gaps between them, ledge drops included as one-way edges.
 * The guide flood-fills the CURRENT screen from the game's tile-attribute
 * RAM with the same walkable table and the same region numbering, so the
 * region Link stands in is measured at the moment of the press. When that
 * measurement cannot be trusted (the live fill disagrees with the table's
 * region count), a route is spoken only if every region of the screen that
 * can reach the target agrees on the same legs. Anything less falls back
 * to a bearing, said as a bearing: "No walking route known; it lies ...".
 * Caves, doors and portals are not graph edges; a landmark the walking
 * graph cannot reach gets the bearing fallback, never an invented path.
 *
 * The screen grid comes from the game's own conversion in overworld.c:
 *   area = ((y & 0x1E00) >> 5) | ((x & 0x1E00) >> 8)
 * which halves to row * 8 + column, and each screen is 0x200 world units.
 * MEASURED anchor: Link's House is area 0x2C = row 5, column 4, and the public
 * save's Link stands at x=2232, y=2792, inside that cell.
 */
constexpr int SCREEN_UNITS = 0x200;

struct GuideTarget { uint8_t area; const char* name; };

/* The landmarks a player actually walks to, per world. */
const GuideTarget kLightTargets[] = {
    { 0x2C, "Link's House" },
    { 0x13, "Sanctuary" },
    { 0x1B, "Hyrule Castle" },
    { 0x18, "Kakariko Village" },
    { 0x1E, "Eastern Palace" },
    { 0x30, "Desert of Mystery" },
    { 0x35, "Lake Hylia" },
    { 0x05, "Death Mountain East" },
    { 0x00, "Lost Woods" },
};
const unsigned kLightTargetCount = sizeof(kLightTargets) / sizeof(kLightTargets[0]);

const GuideTarget kDarkTargets[] = {
    { 0x5B, "Pyramid of Power" },
    { 0x5E, "Palace of Darkness" },
    { 0x7B, "Swamp Palace" },
    { 0x40, "Skull Woods" },
    { 0x58, "Thieves' Town" },
    { 0x75, "Ice Palace" },
    { 0x70, "Misery Mire" },
    { 0x47, "Turtle Rock" },
    { 0x43, "Ganon's Tower" },
};
const unsigned kDarkTargetCount = sizeof(kDarkTargets) / sizeof(kDarkTargets[0]);

/* Screen y grows downward, so a positive dy is south. A ratio test keeps the
 * cardinal directions from wobbling on small offsets. */
const char* CompassWords(int dx, int dy)
{
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    bool north = dy < 0;
    bool east = dx > 0;

    if (ax * 3 < ay) return north ? "north" : "south";
    if (ay * 3 < ax) return east ? "east" : "west";
    if (north) return east ? "north-east" : "north-west";
    return east ? "south-east" : "south-west";
}

/* "north-west, about three screens" for the target area's base cell. The octile
 * span is exact enough for "about" and needs no sqrt. Returns `out`. The dark
 * world mirrors the light grid, so the same arithmetic serves both. */
const char* BearingText(uint8_t targetArea, char* out, size_t cap)
{
    int column = (int) (targetArea & 7);
    int row = (int) ((targetArea >> 3) & 7);
    int tx = column * SCREEN_UNITS + SCREEN_UNITS / 2;
    int ty = row * SCREEN_UNITS + SCREEN_UNITS / 2;
    int dx = tx - (int) u16(LINK_X);
    int dy = ty - (int) u16(LINK_Y);
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;

    int span = (ax > ay ? ax : ay) + (ax < ay ? ax : ay) / 2;
    unsigned screens = (unsigned) ((span + SCREEN_UNITS / 2) / SCREEN_UNITS);

    char count[40];
    if (screens == 0) {
        snprintf(count, sizeof(count), "less than a screen");
    } else if (screens == 1) {
        snprintf(count, sizeof(count), "one screen");
    } else {
        NumberWords(screens, count, sizeof(count));
        size_t used = strlen(count);
        snprintf(count + used, sizeof(count) - used, " screens");
    }
    snprintf(out, cap, "%s, about %s", CompassWords(dx, dy), count);
    return out;
}

/* ---- the walking router -------------------------------------------------- */

struct RouteLeg { uint8_t side; uint8_t third; uint8_t ledge; uint8_t toScreen; };
constexpr int kMaxRouteLegs = 48;

/* The walkable region under Link's feet on the given screen (0..127),
 * flood-filled from the game's own tile-attribute RAM with the generated
 * walkable table. Region numbering matches the generator: row-major seed
 * order, four-connectivity. Returns -1 when the measurement cannot be
 * trusted: Link off the cell, on no walkable tile, or the live fill's
 * region count disagrees with the generated table (a redraw in flight, or
 * tiles the game has changed since the table was made). */
int LiveRegion(uint8_t screen)
{
    int cell = screen & 63;
    int cellX = (cell & 7) * SCREEN_UNITS;
    int cellY = ((cell >> 3) & 7) * SCREEN_UNITS;
    uint16_t by = u16(OW_BASE_Y), my = u16(OW_MASK_Y);
    uint16_t bx = u16(OW_BASE_X), mx = u16(OW_MASK_X);
    bool direct = (my == 0 || mx == 0);

    static bool walk[1024];
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            uint32_t idx;
            if (direct) {
                idx = (uint32_t) (ry * 64 + rx);
            } else {
                int px8 = (cellX + rx * 16 + 8) / 8;
                int py = cellY + ry * 16 + 8;
                uint32_t t = (uint32_t) (((py - (int) by) & (int) my) * 8 +
                                         ((px8 - (int) bx) & (int) mx));
                idx = t >> 1;
            }
            walk[ry * 32 + rx] = AlttpMap16Walkable(u16(TILEATTR + idx * 2));
        }
    }

    static int16_t region[1024];
    static uint16_t stack[1024];
    for (int i = 0; i < 1024; i++) region[i] = -1;
    int next = 0;
    for (int ry = 0; ry < 32; ry++) {
        for (int rx = 0; rx < 32; rx++) {
            int seed = ry * 32 + rx;
            if (region[seed] >= 0 || !walk[seed]) continue;
            int sp = 0;
            stack[sp++] = (uint16_t) seed;
            region[seed] = (int16_t) next;
            while (sp > 0) {
                int cur = stack[--sp];
                int cx = cur & 31, cy = cur >> 5;
                static const int kDx[4] = { 1, -1, 0, 0 };
                static const int kDy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; d++) {
                    int nx = cx + kDx[d], ny = cy + kDy[d];
                    if (nx < 0 || nx >= 32 || ny < 0 || ny >= 32) continue;
                    int ni = ny * 32 + nx;
                    if (region[ni] >= 0 || !walk[ni]) continue;
                    region[ni] = (int16_t) next;
                    stack[sp++] = (uint16_t) ni;
                }
            }
            next++;
        }
    }
    if (next != (int) AlttpScreenRegionCount(screen)) return -1;

    int lx = ((int) u16(LINK_X) - cellX) / 16;
    int ly = ((int) u16(LINK_Y) - cellY) / 16;
    if (lx < 0 || lx >= 32 || ly < 0 || ly >= 32) return -1;
    int r = region[ly * 32 + lx];
    if (r < 0) {
        /* On a door mat or a ledge lip: borrow a neighbour's region. */
        for (int dy = -1; dy <= 1 && r < 0; dy++)
            for (int dx = -1; dx <= 1 && r < 0; dx++) {
                int nx = lx + dx, ny = ly + dy;
                if (nx >= 0 && nx < 32 && ny >= 0 && ny < 32)
                    r = region[ny * 32 + nx];
            }
    }
    return r;
}

/* Dijkstra over (screen, region) states on the generated gap graph. A gap
 * costs 100, plus 50 when it is a ledge drop, so at equal length a walking
 * route beats a dropping one. Fills legs in travel order and returns the
 * leg count, 0 when the start screen is already in the target area, or -1
 * when the area cannot be reached. */
int FindRoute(uint8_t fromScreen, int fromRegion, uint8_t targetArea,
              RouteLeg* legs, int maxLegs)
{
    constexpr int kRegions = 80;
    constexpr int kStates = 128 * kRegions;
    static int dist[kStates];
    static int prevState[kStates];
    static RouteLeg prevLeg[kStates];
    static int heapDist[kStates];
    static int heapState[kStates];

    if (fromRegion < 0 || fromRegion >= kRegions) return -1;
    for (int i = 0; i < kStates; i++) { dist[i] = -1; prevState[i] = -1; }
    int start = fromScreen * kRegions + fromRegion;
    dist[start] = 0;
    int hn = 0;
    heapDist[hn] = 0; heapState[hn] = start; hn++;
    int goal = -1;
    while (hn > 0) {
        /* pop the cheapest */
        int bi = 0;
        for (int i = 1; i < hn; i++)
            if (heapDist[i] < heapDist[bi] ||
                (heapDist[i] == heapDist[bi] && heapState[i] < heapState[bi]))
                bi = i;
        int d = heapDist[bi], s = heapState[bi];
        heapDist[bi] = heapDist[hn - 1]; heapState[bi] = heapState[hn - 1]; hn--;
        if (d != dist[s]) continue;
        int screen = s / kRegions, region = s % kRegions;
        if (AlttpAreaOfScreen((uint8_t) screen) == targetArea) { goal = s; break; }
        const AlttpGap* gaps = nullptr;
        int n = AlttpScreenGaps((uint8_t) screen, &gaps);
        for (int i = 0; i < n; i++) {
            const AlttpGap& g = gaps[i];
            if (g.region != region) continue;
            int ns = screen + g.dScreen;
            if (ns < 0 || ns >= 128 || g.toRegion >= kRegions) continue;
            int nstate = ns * kRegions + g.toRegion;
            int nd = d + 100 + (g.ledge ? 50 : 0);
            if (dist[nstate] < 0 || nd < dist[nstate]) {
                dist[nstate] = nd;
                prevState[nstate] = s;
                prevLeg[nstate] = RouteLeg{ g.side, g.third, g.ledge, (uint8_t) ns };
                heapDist[hn] = nd; heapState[hn] = nstate; hn++;
            }
        }
    }
    if (goal < 0) return -1;
    int count = 0;
    for (int s = goal; prevState[s] >= 0; s = prevState[s]) count++;
    if (count > maxLegs) return -1;
    int i = count;
    for (int s = goal; prevState[s] >= 0; s = prevState[s]) legs[--i] = prevLeg[s];
    return count;
}

/* "Route: north one screen, west two screens, then south one screen,
 * hopping down the ledge, into Kakariko Village." Consecutive legs in the
 * same direction (and the same ledge flag) merge into one counted leg; the
 * first leg names which end of the border to leave by when the gap is not
 * in the border's middle third. */
void RenderRoute(const RouteLeg* legs, int n, const char* targetName,
                 char* out, size_t cap)
{
    struct Merged { uint8_t side; uint8_t third; uint8_t ledge; int count; };
    Merged m[kMaxRouteLegs];
    int mn = 0;
    for (int i = 0; i < n; i++) {
        if (mn > 0 && m[mn - 1].side == legs[i].side && m[mn - 1].ledge == legs[i].ledge)
            m[mn - 1].count++;
        else
            m[mn++] = Merged{ legs[i].side, legs[i].third, legs[i].ledge, 1 };
    }
    static const char* kDir[4] = { "north", "south", "west", "east" };
    char buf[320];
    size_t used = 0;
    used += (size_t) snprintf(buf + used, sizeof(buf) - used, "Route: ");
    for (int i = 0; i < mn; i++) {
        bool last = (i == mn - 1);
        if (i > 0) used += (size_t) snprintf(buf + used, sizeof(buf) - used, ", ");
        if (last && mn > 1) used += (size_t) snprintf(buf + used, sizeof(buf) - used, "then ");
        char count[24];
        if (m[i].count == 1) {
            snprintf(count, sizeof(count), "one screen");
        } else {
            char w[16];
            NumberWords((unsigned) m[i].count, w, sizeof(w));
            snprintf(count, sizeof(count), "%s screens", w);
        }
        used += (size_t) snprintf(buf + used, sizeof(buf) - used, "%s %s",
                                  kDir[m[i].side], count);
        if (m[i].ledge)
            used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                      ", hopping down the ledge");
        if (i == 0 && m[i].third != 1) {
            const char* end = (m[i].side <= 1)
                ? (m[i].third == 0 ? "west end" : "east end")
                : (m[i].third == 0 ? "north end" : "south end");
            used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                      " from near the %s", end);
        }
        if (last)
            used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                      " into %s.", targetName);
    }
    snprintf(out, cap, "%s", buf);
}

/* The direction sentence for a target area from where Link stands: a proven
 * walking route, or the bearing fallback said as a fallback. */
void DirectionText(uint8_t targetArea, const char* targetName, char* out, size_t cap)
{
    uint8_t raw = u8(OVERWORLD_SCREEN);
    uint8_t screen = (uint8_t) (raw & 0x7F);
    RouteLeg legs[kMaxRouteLegs];
    int n = -1;
    if (raw < 128 && AlttpAreaOfScreen(screen) != targetArea) {
        int region = LiveRegion(screen);
        if (region >= 0) {
            n = FindRoute(screen, region, targetArea, legs, kMaxRouteLegs);
        } else {
            /* The live fill cannot be trusted: speak a route only when
             * every region of this screen that can reach the target at all
             * agrees on the same legs. */
            int count = (int) AlttpScreenRegionCount(screen);
            RouteLeg best[kMaxRouteLegs];
            int bestN = -1;
            bool agreed = true;
            for (int r = 0; r < count; r++) {
                RouteLeg l2[kMaxRouteLegs];
                int n2 = FindRoute(screen, r, targetArea, l2, kMaxRouteLegs);
                if (n2 < 0) continue;
                if (bestN < 0) {
                    bestN = n2;
                    memcpy(best, l2, sizeof(RouteLeg) * (size_t) n2);
                } else if (n2 != bestN ||
                           memcmp(best, l2, sizeof(RouteLeg) * (size_t) n2) != 0) {
                    agreed = false;
                    break;
                }
            }
            if (agreed && bestN > 0) {
                memcpy(legs, best, sizeof(RouteLeg) * (size_t) bestN);
                n = bestN;
            }
        }
    }
    if (n > 0) {
        RenderRoute(legs, n, targetName, out, cap);
        return;
    }
    char bearing[80];
    BearingText(targetArea, bearing, sizeof(bearing));
    snprintf(out, cap, "No walking route known; it lies %s away.", bearing);
}

/* ---- indoors guidance ------------------------------------------------------
 * Inside a dungeon the guide routes room to room on the generated room
 * graph (alttp_dungeon_graph.cpp): doors confirmed from both sides by the
 * game's own pairing rule, staircases confirmed from both ends, pit drops
 * the game itself proves: a real pit in the room (an open-hole object, or
 * the hole hidden under a liftable block) in a room whose pits drop the
 * player - where pits only hurt, the destination byte is never read and
 * proves nothing. The cycle walks the three points of
 * interest - big chest, boss, entrance - and a point the graph cannot
 * reach from this part of the dungeon is skipped, never invented: when
 * nothing at all is reachable the answer says so. */

struct RoomLeg { uint16_t from; AlttpRoomEdge edge; };

int RoomEdgeCost(uint8_t kind)
{
    switch (kind) {
        case 1: return 110;   /* shutter: may need the room cleared first */
        case 2: return 150;   /* small-key door */
        case 3: return 150;   /* big-key door */
        case 4: return 130;   /* bombable wall */
        default: return 100;  /* free door, stairs, pit */
    }
}

/* Dijkstra over rooms from `from` to the nearest room carrying poiBit.
 * Fills legs in travel order; returns the leg count, 0 when the start
 * room itself carries the bit, or -1 when no such room is reachable. */
int FindRoomRoute(uint16_t from, uint8_t poiBit, RoomLeg* legs, int maxLegs,
                  uint16_t* targetOut)
{
    static int dist[320];
    static int prevRoom[320];
    static AlttpRoomEdge prevEdge[320];
    for (int i = 0; i < 320; i++) { dist[i] = -1; prevRoom[i] = -1; }
    dist[from] = 0;
    int goal = -1;
    for (;;) {
        int s = -1;
        for (int i = 0; i < 320; i++)
            if (dist[i] >= 0 && (s < 0 || dist[i] < dist[s])) s = i;
        if (s < 0) break;
        if (AlttpRoomPoi((uint16_t) s) & poiBit) { goal = s; break; }
        const AlttpRoomEdge* edges = nullptr;
        int n = AlttpRoomEdges((uint16_t) s, &edges);
        for (int i = 0; i < n; i++) {
            if (dist[edges[i].to] == -2) continue;   /* settled */
            int nd = dist[s] + RoomEdgeCost(edges[i].kind);
            if (dist[edges[i].to] < 0 || nd < dist[edges[i].to]) {
                dist[edges[i].to] = nd;
                prevRoom[edges[i].to] = s;
                prevEdge[edges[i].to] = edges[i];
            }
        }
        dist[s] = -2;   /* settled */
    }
    if (goal < 0) return -1;
    *targetOut = (uint16_t) goal;
    int count = 0;
    for (int s = goal; prevRoom[s] >= 0; s = prevRoom[s]) count++;
    if (count > maxLegs) return -1;
    int i = count;
    for (int s = goal; prevRoom[s] >= 0; s = prevRoom[s])
        legs[--i] = RoomLeg{ (uint16_t) prevRoom[s], prevEdge[s] };
    return count;
}

/* "Route: north through the west door, east through two doors, take the
 * stairs, then north through the locked door into the boss room." plus
 * the honest warnings: a locked door with no key in hand, a big-key door
 * without the big key, a gated room on the way. */
void RenderRoomRoute(const RoomLeg* legs, int n, const char* targetRoom,
                     char* out, size_t cap)
{
    struct Merged { uint8_t kind; uint8_t dir; int half; int count; };
    Merged m[64];
    int mn = 0;
    bool lockedUsed = false, bigkeyUsed = false;
    int gate = 0;
    for (int i = 0; i < n; i++) {
        const AlttpRoomEdge& e = legs[i].edge;
        if (e.kind == 2) lockedUsed = true;
        if (e.kind == 3) bigkeyUsed = true;
        if (!gate) gate = AlttpRoomGate(e.to);
        int half = -1;
        if (e.kind <= 4) {
            const AlttpRoomEdge* sib = nullptr;
            int cnt = AlttpRoomEdges(legs[i].from, &sib);
            int sameWall = 0;
            for (int j = 0; j < cnt; j++)
                if (sib[j].kind <= 4 && sib[j].dir == e.dir) sameWall++;
            if (sameWall >= 2) half = e.pos < 6 ? 0 : 1;
        }
        if (mn > 0 && e.kind <= 4 && m[mn - 1].kind == e.kind &&
            m[mn - 1].dir == e.dir && m[mn - 1].half == half)
            m[mn - 1].count++;
        else
            m[mn++] = Merged{ e.kind, e.dir, half, 1 };
    }
    static const char* kDir[4] = { "north", "south", "west", "east" };
    static const char* kKindWord[5] = { "door", "shutter door", "locked door",
                                        "big key door", "bombable wall" };
    char buf[384];
    size_t used = 0;
    used += (size_t) snprintf(buf + used, sizeof(buf) - used, "Route: ");
    for (int i = 0; i < mn; i++) {
        bool last = (i == mn - 1);
        if (i > 0) used += (size_t) snprintf(buf + used, sizeof(buf) - used, ", ");
        if (last && mn > 1) used += (size_t) snprintf(buf + used, sizeof(buf) - used, "then ");
        if (m[i].kind == 5) {
            used += (size_t) snprintf(buf + used, sizeof(buf) - used, "take the stairs");
        } else if (m[i].kind == 6) {
            used += (size_t) snprintf(buf + used, sizeof(buf) - used, "drop down the pit");
        } else {
            const char* halfWord = "";
            if (m[i].half >= 0)
                halfWord = (m[i].dir <= 1) ? (m[i].half == 0 ? "west " : "east ")
                                           : (m[i].half == 0 ? "north " : "south ");
            if (m[i].count == 1) {
                used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                          "%s through the %s%s", kDir[m[i].dir],
                                          halfWord, kKindWord[m[i].kind]);
            } else {
                char w[16];
                NumberWords((unsigned) m[i].count, w, sizeof(w));
                used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                          "%s through %s %s%ss", kDir[m[i].dir],
                                          w, halfWord, kKindWord[m[i].kind]);
            }
        }
        if (last)
            used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                      " into the %s.", targetRoom);
    }
    if (lockedUsed && u8(KEYS) == 0)
        used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                  " You have no small key for the locked door.");
    if (bigkeyUsed) {
        unsigned id = (unsigned) u8(DUNGEON_INDEX) / 2u;
        bool big = id < 16u && ((u16(BIG_KEY) >> id) & 1u) != 0;
        if (!big)
            used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                      " You do not have the big key.");
    }
    if (gate == 1)
        used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                  " One room on the way is gated: clear it to open the door.");
    else if (gate == 2)
        used += (size_t) snprintf(buf + used, sizeof(buf) - used,
                                  " One room on the way is gated: a switch opens the door.");
    snprintf(out, cap, "%s", buf);
}

void SpeakGuideIndoors()
{
    uint8_t dungeon = u8(DUNGEON_INDEX);
    unsigned room = u16(DUNGEON_ROOM);
    if (dungeon == 0xFF || room >= 320) {
        Say("Guidance works outside.", "guide", Priority::High);
        return;
    }
    uint16_t comp = AlttpRoomComponent((uint16_t) room);
    if (comp != g_indoorComponent) {
        g_indoorComponent = comp;
        g_indoorIndex = 0;
    }
    static const uint8_t kPoiBits[3] = { 2, 4, 1 };
    static const char* kPoiNames[3] = { "Big Chest", "Boss", "Entrance" };
    static const char* kPoiRooms[3] = { "big chest room", "boss room", "entrance room" };
    for (int t = 0; t < 3; t++) {
        int ci = (g_indoorIndex + t) % 3;
        RoomLeg legs[64];
        uint16_t target = 0;
        int n = FindRoomRoute((uint16_t) room, kPoiBits[ci], legs, 64, &target);
        if (n < 0) continue;
        g_indoorIndex = (unsigned) (ci + 1) % 3;
        char line[448];
        if (n == 0) {
            snprintf(line, sizeof(line), "Guide: %s. You are here.", kPoiNames[ci]);
            Say(line, "guide", Priority::High);
            return;
        }
        char route[384];
        RenderRoomRoute(legs, n, kPoiRooms[ci], route, sizeof(route));
        snprintf(line, sizeof(line), "Guide: %s. %s", kPoiNames[ci], route);
        Say(line, "guide", Priority::High);
        return;
    }
    Say("Guide: no route from here.", "guide", Priority::High);
}

/* Declared here because the guide aims at the objective before the objective
 * section below defines it. */
struct Objective { const char* text; uint8_t area; };
Objective CurrentObjective();

void SpeakGuide()
{
    if (CurrentMenu() != Menu::None) {
        Say("Close the menu first.", "guide", Priority::High);
        return;
    }
    if (u8(PLAYER_INDOORS) != 0) {
        SpeakGuideIndoors();
        return;
    }

    unsigned area = u16(OVERWORLD_AREA);
    uint8_t world = area >= 0x40 ? 1 : 0;
    const GuideTarget* targets = world ? kDarkTargets : kLightTargets;
    unsigned targetCount = world ? kDarkTargetCount : kLightTargetCount;

    /* ⛔ THE FIRST PRESS ANSWERS THE QUESTION THE PLAYER ACTUALLY HAS: where is
     * the next objective? The cycle re-aims only when the story stage or the
     * world changes, not on every press, so the whole landmark list stays
     * walkable. */
    Objective goal = CurrentObjective();
    if (goal.area != g_objectiveArea || world != g_guideWorld) {
        g_objectiveArea = goal.area;
        g_guideWorld = world;
        g_guideIndex = 0;
        for (unsigned i = 0; i < targetCount; i++) {
            if (targets[i].area == goal.area) { g_guideIndex = i; break; }
        }
    }

    const GuideTarget& target = targets[g_guideIndex % targetCount];
    g_guideIndex = (g_guideIndex + 1) % targetCount;

    char line[320];
    if (area == target.area) {
        snprintf(line, sizeof(line), "Guide: %s. You are here.", target.name);
        Say(line, "guide", Priority::High);
        return;
    }

    char dir[280];
    DirectionText(target.area, target.name, dir, sizeof(dir));
    snprintf(line, sizeof(line), "Guide: %s. %s", target.name, dir);
    Say(line, "guide", Priority::High);
}

/* ---- the objective -------------------------------------------------------
 * ⛔ THE STAGE COMES FROM THE GAME'S OWN PROGRESS BYTES, never from a guess
 * about where the player "should" be. Measured on the public save: progress
 * flags 0x15 (sword, Zelda delivered, uncle gone), pendants 0, sword 1.
 * bits: 0x01 uncle gave the sword, 0x04 Zelda reached the Sanctuary,
 * 0x10 the uncle left the house.
 *
 * The dark-world stage keys on sram_progress_indicator: the game sets it to
 * 3 when Agahnim falls in the castle tower (PrepareDungeonExitFromBossFight
 * in the decompilation), and it stays 3 for the whole second half. A set
 * crystal bit proves the same phase on its own. There is deliberately no
 * authoritative "next dungeon": the crystal dungeons gate each other by
 * equipment, not by a story counter, so the reader names what is held, what
 * remains, and which remaining dungeon is nearest — never an invented
 * "next". */

/* The seven crystal dungeons in gating order (each one's equipment opens
 * the next), with the crystal bit from the decompilation's
 * kDungeonCrystalPendantBit and the overworld area of the entrance. */
struct CrystalDungeon { uint8_t bit; uint8_t area; const char* name; };
const CrystalDungeon kCrystalDungeons[] = {
    { 2,  0x5E, "Palace of Darkness" },
    { 16, 0x7B, "Swamp Palace" },
    { 64, 0x40, "Skull Woods" },
    { 32, 0x58, "Thieves' Town" },
    { 4,  0x75, "Ice Palace" },
    { 1,  0x70, "Misery Mire" },
    { 8,  0x47, "Turtle Rock" },
};

bool DarkPhase()
{
    return u8(PROGRESS_INDICATOR) == 3 || u8(CRYSTALS) != 0;
}

int CrystalsHeld()
{
    uint8_t c = (uint8_t) (u8(CRYSTALS) & 0x7F);
    int n = 0;
    for (int i = 0; i < 7; i++) if (c & (1 << i)) n++;
    return n;
}

const CrystalDungeon* CrystalByArea(uint8_t area)
{
    for (const CrystalDungeon& d : kCrystalDungeons)
        if (d.area == area) return &d;
    return nullptr;
}

/* The dark-world target: the nearest remaining crystal dungeon by walking
 * cost when Link stands outdoors in the dark world, else the first
 * remaining in gating order; Ganon's Tower (0x43) when all seven are held. */
uint8_t DarkTarget()
{
    uint8_t crystals = u8(CRYSTALS);
    const CrystalDungeon* first = nullptr;
    for (const CrystalDungeon& d : kCrystalDungeons)
        if (!(crystals & d.bit)) { first = &d; break; }
    if (!first) return 0x43;
    if (u8(PLAYER_INDOORS) == 0 && u16(OVERWORLD_AREA) >= 0x40) {
        uint8_t raw = u8(OVERWORLD_SCREEN);
        if (raw < 128) {
            uint8_t screen = (uint8_t) (raw & 0x7F);
            int region = LiveRegion(screen);
            if (region >= 0) {
                const CrystalDungeon* bestD = nullptr;
                int bestCost = -1;
                for (const CrystalDungeon& d : kCrystalDungeons) {
                    if (crystals & d.bit) continue;
                    RouteLeg legs[kMaxRouteLegs];
                    int n = FindRoute(screen, region, d.area, legs, kMaxRouteLegs);
                    if (n < 0) continue;
                    int cost = 0;
                    for (int i = 0; i < n; i++) cost += 100 + (legs[i].ledge ? 50 : 0);
                    if (bestCost < 0 || cost < bestCost) { bestCost = cost; bestD = &d; }
                }
                if (bestD) return bestD->area;
            }
        }
    }
    return first->area;
}

Objective CurrentObjective()
{
    if (DarkPhase()) {
        if ((u8(CRYSTALS) & 0x7F) == 0x7F)
            return { "Climb Ganon's Tower and face Ganon.", 0x43 };
        return { "Rescue the seven maidens.", DarkTarget() };
    }

    uint8_t flags = u8(PROGRESS_FLAGS);
    uint8_t pendants = u8(PENDANTS);
    uint8_t sword = u8(SWORD_TYPE);

    if (!(flags & 0x01))
        return { "Find your uncle in Hyrule Castle.", 0x1B };
    if (!(flags & 0x04))
        return { "Rescue Zelda in Hyrule Castle.", 0x1B };
    if ((pendants & 7) != 7)
        return { "Collect the three pendants. Sahasrahla in Kakariko knows where to start.", 0x18 };
    if (sword < 2)
        return { "Claim the Master Sword in the Lost Woods.", 0x00 };
    return { "Confront Agahnim in Hyrule Castle.", 0x1B };
}

void SpeakObjective()
{
    if (CurrentMenu() != Menu::None) {
        Say("Close the menu first.", "objective", Priority::High);
        return;
    }

    unsigned area = u16(OVERWORLD_AREA);
    bool indoors = u8(PLAYER_INDOORS) != 0;
    char line[320];

    if (DarkPhase() && (u8(CRYSTALS) & 0x7F) != 0x7F) {
        /* The maidens stage: what is held, what remains, and — only when a
         * direction can be proven — the nearest remaining dungeon. */
        uint8_t crystals = (uint8_t) (u8(CRYSTALS) & 0x7F);
        char heldWords[16];
        NumberWords((unsigned) CrystalsHeld(), heldWords, sizeof(heldWords));
        heldWords[0] = (char) toupper((unsigned char) heldWords[0]);
        char remaining[192];
        size_t used = 0;
        remaining[0] = 0;
        for (const CrystalDungeon& d : kCrystalDungeons) {
            if (crystals & d.bit) continue;
            used += (size_t) snprintf(remaining + used, sizeof(remaining) - used,
                                      "%s%s", used ? ", " : "", d.name);
        }
        uint8_t target = DarkTarget();
        const CrystalDungeon* td = CrystalByArea(target);
        if (indoors) {
            snprintf(line, sizeof(line),
                     "Objective: Rescue the seven maidens. %s of seven crystals held. Remaining: %s.",
                     heldWords, remaining);
        } else if (area >= 0x40) {
            char dir[280];
            DirectionText(target, td ? td->name : "the dungeon", dir, sizeof(dir));
            snprintf(line, sizeof(line),
                     "Objective: Rescue the seven maidens. %s of seven crystals held. Remaining: %s. Nearest remaining: %s. %s",
                     heldWords, remaining, td ? td->name : "the dungeon", dir);
        } else {
            snprintf(line, sizeof(line),
                     "Objective: Rescue the seven maidens. %s of seven crystals held. Remaining: %s. Nearest remaining: %s, in the dark world.",
                     heldWords, remaining, td ? td->name : "the dungeon");
        }
        Say(line, "objective", Priority::High);
        return;
    }

    Objective goal = CurrentObjective();
    if (indoors) {
        snprintf(line, sizeof(line), "Objective: %s", goal.text);
        Say(line, "objective", Priority::High);
        return;
    }
    if (area == goal.area) {
        snprintf(line, sizeof(line), "Objective: %s You are there.", goal.text);
        Say(line, "objective", Priority::High);
        return;
    }

    const char* name = AlttpAreaName(goal.area);
    char dir[280];
    DirectionText(goal.area, name ? name : "The target", dir, sizeof(dir));
    snprintf(line, sizeof(line), "Objective: %s %s", goal.text, dir);
    Say(line, "objective", Priority::High);
}

void AlttpCommand(Command cmd)
{
    if (cmd == Command::DumpState) { DumpState(); return; }

    /* The file select answers even though no game is running: its state is in
     * save RAM, not in the work RAM that ready() validates. */
    if (CurrentMenu() == Menu::FileSelect) {
        if (cmd == Command::WhereAmI) SpeakFileSelect(true);
        else if (cmd == Command::Status) SpeakFileSelect(false);
        return;
    }

    /* ⛔ SILENT UNTIL THE GAME HAS STATE. A player command answered from a
     * booting console is the "bunch of numbers" failure the design forbids; the
     * UI does not offer the controls while ready() is false. */
    if (!AlttpReady()) return;

    switch (cmd) {
        case Command::WhereAmI: SpeakWhereAmI(); break;
        case Command::Status:   SpeakStatus(); break;
        case Command::Guide:    SpeakGuide(); break;
        case Command::Objective: SpeakObjective(); break;
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
    nullptr,   // cue_snapshot: this reader has no audio beacon
    "Menus, dialogue, file select, location, status, guidance and objectives",
    "Spoken: the file select reads each file's name, hearts and equipment; "
    "in-game dialogue is spoken as it appears; the item menu reads the "
    "selected item, and the save menu reads the selected slot. Where Am I "
    "names your area. Status reads hearts, magic, rupees and, inside a "
    "dungeon, keys, big key, map and compass. Guide gives a spoken "
    "walking route to named places in both the light and dark worlds "
    "when the game's tile data proves one, and otherwise a compass "
    "bearing, said as a fallback; inside a dungeon it routes room to "
    "room to the big chest, the boss and the entrance. Objective names "
    "your current goal and, in the dark world, the crystals held, "
    "those remaining, and the nearest remaining dungeon.\n"
    "Supported version: the USA 1.0 ROM with CRC32 777AAC2F.",
};

} // namespace oga
