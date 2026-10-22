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
    printf(" * name, game code, coverage line, library notes, console.\n");
    printf(" */\n");
    printf("data class AdapterGameInfo(\n");
    printf("    val id: String,\n");
    printf("    val name: String,\n");
    printf("    val code: String,\n");
    printf("    val coverage: String,\n");
    printf("    val notes: String,\n");
    printf("    val console: String,\n");
    printf(")\n");
    printf("\n");
    printf("/** The registry's library games, in registry order. */\n");
    printf("object AdapterLibraryData {\n");
    printf("    val games: List<AdapterGameInfo> = listOf(\n");
    int emitted = 0;
    for (int i = 0; i < n; i++) {
        const oga::Adapter* a = list[i];
        if (!LibraryAdapter(a)) continue;
        printf("        AdapterGameInfo(\n");
        printf("            id = \"%s\",\n", KotlinEscape(a->id).c_str());
        printf("            name = \"%s\",\n", KotlinEscape(a->display_name).c_str());
        printf("            code = \"%s\",\n", KotlinEscape(a->game_code).c_str());
        printf("            coverage = \"%s\",\n", KotlinEscape(a->coverage).c_str());
        printf("            notes = \"%s\",\n", KotlinEscape(a->notes).c_str());
        printf("            console = \"%s\",\n", KotlinEscape(LibraryConsole(a)).c_str());
        printf("        ),\n");
        emitted++;
    }
    printf("    )\n");
    printf("}\n");
    fprintf(stderr, "android-library-gen: %d library games emitted\n", emitted);
    return emitted > 0 ? 0 : 1;
}
