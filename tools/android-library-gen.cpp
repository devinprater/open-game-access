/*
 * android-library-gen.cpp — host generator for the Android game library.
 *
 * Emits app/src/main/java/com/devin/opengameaccess/AdapterLibraryData.kt from
 * the REAL adapter registry (oga::all_adapters() in Core/adapters.cpp, the
 * same objects the iOS app reads through poke_adapter_library_*), so the
 * Android library list cannot drift from the readers that actually ship:
 * add an adapter to the registry, re-run scripts/gen-android-library.sh, and
 * the game appears on Android too. The generated file is checked in because
 * the Android build has no C++ host step; scripts/gen-android-library.sh
 * --check (and the launcher contract test) fail when it is stale.
 *
 * ⛔ THE TWO RULES BELOW ARE COPIED, NOT SHARED. The library filter and the
 * console derivation live in Core/pokecore.cpp (LibraryAdapter and
 * poke_adapter_library_console) behind the PokeCore ABI, which the Android
 * layer never links. They are small and stable, and they are copied here
 * verbatim so the two platforms present the SAME list; if pokecore.cpp's
 * versions change, change these to match. The per-game strings (id, name,
 * code, coverage, notes) are NOT copied — they are read from the registry
 * objects at generation time, which is the part that must never drift.
 *
 * ⛔ A THIRD COPIED RULE, SAME FLAG (stage 4): each game's player-facing
 * command set and the commands' titles are copied from
 * Sources/OpenGameAccess/AdapterCommand.swift (supported(adapterID:) and
 * AdapterCommand.title). They cannot be read from the registry — the C++
 * Adapter struct carries no such list; the supported set is a
 * presentation decision the Swift layer owns ("what each adapter REALLY
 * does today"). Android's PSP screen renders its reader buttons from
 * this data, so the copy is what keeps the two platforms' buttons
 * identical; the launcher contract test (§10) compares the generated
 * lists against the Swift source itself, so a stale copy fails loudly.
 *
 * Host-only: never add this file to a build list (see core-sources.sh).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "adapter.h"

// ---- link stubs for fe_adapter.cpp's reader (never called) -----------------
// fe_adapter.cpp's command surface lives in fe_access.cpp, a standalone host
// harness with its own main() and a melonDS include graph, so it cannot join
// this link. The generator reads only fe11's registry STRINGS, which come
// from fe_adapter.cpp's real initializer; these stubs exist solely so the
// initializer's function pointers resolve. Following host_harness_stub.cpp's
// rule, they ABORT if ever called rather than silently doing nothing.
struct PokeCore;
namespace melonDS { class NDS; }
extern "C" {
static void FeStub(const char* name)
{
    fprintf(stderr, "android-library-gen: fe stub %s called — the data "
                    "generator must never run adapter code\n", name);
    abort();
}
void fe_set_say_sink(void (*)(const char*, bool)) { FeStub("fe_set_say_sink"); }
void fe_set_log_sink(void (*)(const char*)) { FeStub("fe_set_log_sink"); }
void fe_cmd_where_am_i(void) { FeStub("fe_cmd_where_am_i"); }
void fe_cmd_menu_state(void) { FeStub("fe_cmd_menu_state"); }
void fe_cmd_menu_nav(void) { FeStub("fe_cmd_menu_nav"); }
void fe_cmd_next_ally(int) { FeStub("fe_cmd_next_ally"); }
void fe_cmd_next_enemy(int) { FeStub("fe_cmd_next_enemy"); }
void fe_cmd_dump(void) { FeStub("fe_cmd_dump"); }
bool fe_ready(void) { FeStub("fe_ready"); return false; }
void* fe_main_ram(void*) { FeStub("fe_main_ram"); return nullptr; }
void fe_bind_ram(void*) { FeStub("fe_bind_ram"); }
} // extern "C"
melonDS::NDS* poke_debug_nds(PokeCore*) { FeStub("poke_debug_nds"); return nullptr; }

// The registry lives in namespace oga (see Core/adapters.cpp).
namespace oga {
const Adapter* const* all_adapters(int* count);
}

// ---- copied from Core/pokecore.cpp: LibraryAdapter -------------------------
// Console-only placeholder rows are skipped: their notes say there is no
// game-specific reader yet, and the library is a list of games.
static bool LibraryAdapter(const oga::Adapter* a)
{
    if (!a || !a->id) return false;
    return strcmp(a->id, "nes") != 0 && strcmp(a->id, "n64") != 0;
}

// ---- copied from Core/pokecore.cpp: poke_adapter_library_console -----------
// The registry encodes the console twice over: the id names the console
// (gba) or the game (fe11), and the code's shape does the rest.
static const char* LibraryConsole(const oga::Adapter* a)
{
    if (!a || !a->id) return "";
    if (strcmp(a->id, "gba") == 0) return "gb";
    if (strcmp(a->id, "nes") == 0) return "nes";
    if (strcmp(a->id, "n64") == 0) return "n64";
    if (a->game_code && strncmp(a->game_code, "crc:", 4) == 0) return "snes";
    if (a->game_code && strncmp(a->game_code, "ULUS", 4) == 0) return "psp";
    if (a->game_code && strlen(a->game_code) == 4) return "nds";
    return "";
}

// ---- copied from AdapterCommand.swift: supported(adapterID:) ---------------
// The player-facing command set per adapter id, as oga::Command values.
// An id with NO row here has no buttons — the Swift switch's default —
// which covers gba (its interaction is the Lua script's own hotkeys) and
// dbzar/dq9 (no case in the switch at all) alike.
struct SupportedRow {
    const char* id;
    const int* commands;
    int count;
};
#define CMD(x) static_cast<int>(oga::Command::x)
static const int kSupportedFe11[] = {CMD(WhereAmI), CMD(NextAlly),
    CMD(PrevAlly), CMD(NextEnemy), CMD(PrevEnemy), CMD(NextUnactedAlly),
    CMD(DumpState)};
static const int kSupportedDbzSaiyans[] = {CMD(WhereAmI), CMD(NextAlly),
    CMD(PrevAlly), CMD(DumpState)};
static const int kSupportedAlttp[] = {CMD(WhereAmI), CMD(Status),
    CMD(Guide), CMD(Objective), CMD(DumpState)};
static const int kSupportedDissidia[] = {CMD(WhereAmI), CMD(MenuNext),
    CMD(MenuPrev), CMD(MenuLeft), CMD(MenuRight), CMD(CustToggle),
    CMD(CharToggle), CMD(OskToggle), CMD(DumpState)};
static const int kSupportedKhbbs[] = {CMD(WhereAmI), CMD(MenuState),
    CMD(NextAlly), CMD(NextEnemy), CMD(Guide), CMD(DumpState)};
static const int kSupportedMku[] = {CMD(WhereAmI), CMD(MenuState),
    CMD(DumpState)};
static const SupportedRow kSupported[] = {
    {"fe11", kSupportedFe11, 7},
    {"dbz-saiyans", kSupportedDbzSaiyans, 4},
    {"gba", nullptr, 0},
    {"alttp", kSupportedAlttp, 5},
    {"dissidia", kSupportedDissidia, 9},
    {"khbbs", kSupportedKhbbs, 6},
    {"mku", kSupportedMku, 3},
};
#undef CMD

static const int* SupportedFor(const char* id, int* count)
{
    for (const SupportedRow& row : kSupported) {
        if (id && strcmp(id, row.id) == 0) {
            *count = row.count;
            return row.commands;
        }
    }
    *count = 0;
    return nullptr;
}

// ---- copied from AdapterCommand.swift: AdapterCommand.title ----------------
// Only the commands some supported set names get a row: the Android
// library carries titles for exactly the buttons it can render.
static const char* CommandTitle(int command)
{
    switch (static_cast<oga::Command>(command)) {
        case oga::Command::WhereAmI: return "Where am I";
        case oga::Command::NextAlly: return "Next ally";
        case oga::Command::PrevAlly: return "Previous ally";
        case oga::Command::NextEnemy: return "Next enemy";
        case oga::Command::PrevEnemy: return "Previous enemy";
        case oga::Command::NextUnactedAlly: return "Next waiting ally";
        case oga::Command::DumpState: return "Copy state to log";
        case oga::Command::MenuState: return "Menu status";
        case oga::Command::MenuNext: return "Next item";
        case oga::Command::MenuPrev: return "Previous item";
        case oga::Command::MenuLeft: return "Previous value";
        case oga::Command::MenuRight: return "Next value";
        case oga::Command::CustToggle: return "Customize row";
        case oga::Command::CharToggle: return "Character";
        case oga::Command::OskToggle: return "Name entry";
        case oga::Command::Status: return "Status";
        case oga::Command::Guide: return "Guide";
        case oga::Command::Objective: return "Objective";
        default: return nullptr;
    }
}

// Escape a registry string as a Kotlin string literal body. Kotlin raw
// strings still interpolate `$`, so a plain quoted literal with full
// escaping is the only safe form for text we do not control.
static std::string KotlinEscape(const char* s)
{
    std::string out;
    if (!s) return out;
    for (const unsigned char* p = (const unsigned char*) s; *p; p++) {
        switch (*p) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '$':  out += "\\$"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (*p < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", *p);
                    out += buf;
                } else {
                    out += (char) *p;
                }
        }
    }
    return out;
}

int main(void)
{
    int n = 0;
    const oga::Adapter* const* list = oga::all_adapters(&n);

    printf("package com.devin.opengameaccess\n");
    printf("\n");
    printf("// GENERATED by tools/android-library-gen.cpp from the Core adapter\n");
    printf("// registry (Core/adapters.cpp) — do not edit by hand. Regenerate with\n");
    printf("// scripts/gen-android-library.sh; the launcher contract test fails\n");
    printf("// when this file's game count stops matching the registry.\n");
    printf("\n");
    printf("/**\n");
    printf(" * One game that has a native reader, as the library presents it.\n");
    printf(" *\n");
    printf(" * The fields mirror iOS's AdapterGame (AdapterLibrary.swift) exactly,\n");
    printf(" * because both are read from the same registry entry: id, display\n");
    printf(" * name, game code, coverage line, library notes, console. The\n");
    printf(" * commands list is the one deliberate addition: the oga::Command\n");
    printf(" * values (adapter.h's raw ABI values) of the buttons iOS shows\n");
    printf(" * for this game, copied from AdapterCommand.supported — see the\n");
    printf(" * generator's header for why that rule is copied, not read.\n");
    printf(" */\n");
    printf("data class AdapterGameInfo(\n");
    printf("    val id: String,\n");
    printf("    val name: String,\n");
    printf("    val code: String,\n");
    printf("    val coverage: String,\n");
    printf("    val notes: String,\n");
    printf("    val console: String,\n");
    printf("    val commands: List<Int>,\n");
    printf(")\n");
    printf("\n");
    printf("/** The registry's library games, in registry order. */\n");
    printf("object AdapterLibraryData {\n");
    printf("    val games: List<AdapterGameInfo> = listOf(\n");
    int emitted = 0;
    for (int i = 0; i < n; i++) {
        const oga::Adapter* a = list[i];
        if (!LibraryAdapter(a)) continue;
        int commandCount = 0;
        const int* commands = SupportedFor(a->id, &commandCount);
        printf("        AdapterGameInfo(\n");
        printf("            id = \"%s\",\n", KotlinEscape(a->id).c_str());
        printf("            name = \"%s\",\n", KotlinEscape(a->display_name).c_str());
        printf("            code = \"%s\",\n", KotlinEscape(a->game_code).c_str());
        printf("            coverage = \"%s\",\n", KotlinEscape(a->coverage).c_str());
        printf("            notes = \"%s\",\n", KotlinEscape(a->notes).c_str());
        printf("            console = \"%s\",\n", KotlinEscape(LibraryConsole(a)).c_str());
        printf("            commands = listOf(");
        for (int c = 0; c < commandCount; c++) {
            printf("%s%d", c ? ", " : "", commands[c]);
        }
        printf("),\n");
        printf("        ),\n");
        emitted++;
    }
    printf("    )\n");
    printf("}\n");
    printf("\n");
    printf("/**\n");
    printf(" * The player-facing title of each command a supported set names,\n");
    printf(" * keyed by the command's raw value — copied from\n");
    printf(" * AdapterCommand.title by the same generator rule as the sets.\n");
    printf(" */\n");
    printf("object AdapterCommandTitles {\n");
    printf("    val byId: Map<Int, String> = mapOf(\n");
    for (int c = 0; c <= static_cast<int>(oga::Command::Objective); c++) {
        const char* title = CommandTitle(c);
        if (!title) continue;
        printf("        %d to \"%s\",\n", c, KotlinEscape(title).c_str());
    }
    printf("    )\n");
    printf("}\n");
    fprintf(stderr, "android-library-gen: %d library games emitted\n", emitted);
    return emitted > 0 ? 0 : 1;
}
