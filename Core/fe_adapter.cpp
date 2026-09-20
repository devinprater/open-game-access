/*
 * fe_adapter.cpp — exposes the Fire Emblem reader to Open Game Access.
 *
 * WHY THIS EXISTS: fe_access.cpp was written as a standalone host harness. Its
 * command logic is verified, but it ends in `main()` — so on iOS the file could
 * not be compiled at all (a second `main` would clash with the app), and the
 * registry in adapters.cpp declared `kFireEmblemShadowDragon` with no definition
 * anywhere. The result was that Fire Emblem read nothing, and every FE control in
 * the UI was inert.
 *
 * This file is the missing seam. It:
 *   1. defines the adapter the registry already referenced,
 *   2. routes the reader's spoken output to the host's speech callback,
 *   3. forwards the host's accessibility commands to the reader's own `cmd*`.
 *
 * ⛔ THE READER'S TEXT IS REUSED, NOT REWRITTEN. Its wording was reviewed and its
 * numbers were verified against live RAM (see docs/fire-emblem-shadow-dragon-memory.md).
 * Rewriting the sentences here would fork that verified text into a second copy
 * that can silently drift.
 *
 * ⛔ SCOPE, STATED HONESTLY: this adapter answers the tactical-map questions the
 * reader implements — where am I, next ally, next enemy, and a dump. It does NOT
 * read menus, dialogue or the intro cutscene; those have no reader yet. So the
 * `ready()` gate below is the map state, and the adapter stays silent until a map
 * actually exists rather than narrating whatever is in RAM.
 */
#include "adapter.h"
#include "pokecore.h"

// ⛔ NOT `#include "NDS.h"`. This translation unit needs nothing but a pointer —
// the melonDS headers are only on the include path for the units that must
// dereference melonDS types, and pulling NDS.h in here drags melonDS's own
// include graph into the adapter for a type it never touches. fe_access.cpp
// forward-declares for the same reason; matching that keeps the two files
// compiling under the same flags.
namespace melonDS { class NDS; }

#include <stdio.h>
#include <string.h>

// The reader, compiled from fe_access.cpp. Declared here rather than in a header
// so adapter.h stays game-agnostic (see its own note on not over-generalising).
extern "C" {
void fe_set_say_sink(void (*say)(const char*, bool));
void fe_set_log_sink(void (*log)(const char*));
}

// The reader's command surface. Each of these speaks through the sink above.
// They are file-static in fe_access.cpp, so fe_access.cpp provides these
// non-static wrappers instead of exposing its internals.
extern "C" {
void fe_cmd_where_am_i(void);
void fe_cmd_next_ally(int dir);
void fe_cmd_next_enemy(int dir);
void fe_cmd_dump(void);
bool fe_ready(void);
void* fe_main_ram(void* nds);
void fe_bind_ram(void* ram);
}

// The core's host-only hook for reaching the live console. Declared rather than
// included: it is deliberately not in pokecore.h because it hands out an internal
// type, and an adapter is the one caller entitled to it.
melonDS::NDS* poke_debug_nds(PokeCore* core);

namespace oga {
namespace {

melonDS::NDS* g_nds = nullptr;
const Host* g_host = nullptr;
char g_code[8] = {0};

void HostSay(const char* utf8, bool interrupt)
{
    if (g_host && g_host->speak) g_host->speak(g_host->ctx, utf8, interrupt);
}

void HostLog(const char* utf8)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, utf8);
}

bool Attach(const Host* host)
{
    if (!host) return false;
    g_host = host;

    PokeCore* core = (PokeCore*) host->ctx;
    melonDS::NDS* nds = poke_debug_nds(core);
    if (!nds) return false;
    g_nds = nds;
    fe_bind_ram(fe_main_ram(nds));

    // Route the reader's output. Set on EVERY attach rather than once at startup:
    // the host pointer is per-core and a fresh core is built per ROM.
    fe_set_say_sink(HostSay);
    fe_set_log_sink(HostLog);

    HostLog("[fe] adapter attached\n");
    return true;
}

void Detach(void)
{
    fe_set_say_sink(nullptr);
    fe_set_log_sink(nullptr);
    fe_bind_ram(nullptr);
    g_nds = nullptr;
    g_host = nullptr;
}

bool Ready(void)
{
    return fe_ready();
}

void OnFrame(void)
{
    // Nothing per-frame: the FE reader is command-driven, like the Pokémon one.
    // Deliberately empty rather than absent — a future "unit acted" announcement
    // would live here, and an empty hook documents that it was considered.
}

void HandleCommand(oga::Command cmd)
{
    switch (cmd) {
        case oga::Command::WhereAmI:        fe_cmd_where_am_i();   break;
        case oga::Command::NextAlly:        fe_cmd_next_ally(+1);  break;
        case oga::Command::PrevAlly:        fe_cmd_next_ally(-1);  break;
        case oga::Command::NextEnemy:       fe_cmd_next_enemy(+1); break;
        case oga::Command::PrevEnemy:       fe_cmd_next_enemy(-1); break;
        // No reader exists for these yet. Saying so is the honest answer; staying
        // silent would look like a broken control.
        case oga::Command::NextUnactedAlly:
            HostSay("Reading unacted units is not available yet.", false); break;
        case oga::Command::DumpState:       fe_cmd_dump();         break;
    }
}

} // namespace

// The registry's symbol. `extern` at namespace scope is what gives this external
// linkage — a bare `const` here would be internal and the registry could not see
// it, which is exactly the link failure this project hit before.
extern const Adapter kFireEmblemShadowDragon = {
    "fe11",
    "Fire Emblem: Shadow Dragon",
    "YFEE",              // USA; the reader's addresses are verified against this rev
    Attach,
    OnFrame,
    HandleCommand,
    Ready,
    Detach,
};

} // namespace oga
