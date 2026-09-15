/*
 * gba_adapter_test.cpp — a host test for the GBA adapter, with a STUB host.
 *
 * ⛔ WHY A STUB HOST, GIVEN THE LESSON FROM THE mGBA WORK. A stub that is MORE PERMISSIVE
 * than reality validates a design that cannot work — that is exactly how an entire shim
 * architecture passed 70+ checks and then failed on the real emulator. So this stub is
 * deliberately built to be NO MORE capable than the real Host: it implements only the six
 * function pointers the struct declares, and it can be told to return "not allocated yet"
 * so the adapter's negative paths are exercised rather than assumed.
 *
 * What this test CAN prove: the adapter selects correctly by game code, refuses codes the
 * reader does not support, reports readiness honestly, refuses to invent a position before
 * the save block exists, and speaks a delta when the player has moved.
 *
 * What it CANNOT prove: that the addresses are correct on a real ROM. That is proven
 * separately, by running the reader itself in mGBA and reading what it says
 * (see tools/re/platforms/gba/FOOTSTEP-RESULT.txt).
 *
 * Build:  g++ -O2 -ICore -std=c++17 -o /tmp/gba_adapter_test \
 *             Core/gba_adapter_test.cpp Core/gba_adapter.cpp
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

namespace oga {
void gba_set_game_code(const char* code);
}


// ── A link-time stub for the Fire Emblem adapter ────────────────────────────────────────
// adapters.cpp references it, but this test exercises only the GBA adapter and pulling in
// fe_access.cpp would drag the melonDS headers and a whole core with it. Defined here so the
// registry links. ⛔ This is a LINK STUB, not a claim about FE11 — its real implementation is
// in fe_access.cpp and is tested by its own harness (scripts/fe-access.sh).
namespace oga {
bool fe_stub_attach(const Host*) { return false; }
void fe_stub_frame(void) {}
void fe_stub_command(Command) {}
bool fe_stub_ready(void) { return false; }
void fe_stub_detach(void) {}
// ⛔ `extern` IS REQUIRED HERE, AND THIS IS A REAL C++ RULE WORTH KNOWING.
// A `const` object at namespace scope has INTERNAL linkage by default in C++, so without
// `extern` the symbol is invisible to adapters.cpp and the link fails with a confusing
// "undefined reference" to a symbol that is plainly defined right here. The GBA adapter
// avoids this because gba_adapter.cpp declares `extern const Adapter kGameBoyAdvance;`
// above its definition.
extern const Adapter kFireEmblemShadowDragon;
const Adapter kFireEmblemShadowDragon = {
    "fe11", "Fire Emblem: Shadow Dragon (stub)", "YFEE",
    fe_stub_attach, fe_stub_frame, fe_stub_command, fe_stub_ready, fe_stub_detach,
};
} // namespace oga

// ── A deliberately minimal stub host ────────────────────────────────────────────────────
namespace {

struct Stub {
    uint8_t  mem[0x40000];          // 256 KB, enough for the save block region
    uint32_t mem_base = 0x02000000; // where this stub's memory starts
    bool     pointer_valid = false; // when false, read32(SAVEBLOCK_PTR) returns 0
    char     spoken[32][256];
    int      spoke = 0;
    char     logs[32][256];
    int      logged = 0;
};

Stub g_stub;

uint8_t  stub_r8 (void* c, uint32_t a)  { Stub* s=(Stub*)c; uint32_t o=a-s->mem_base; return o<sizeof(s->mem) ? s->mem[o] : 0; }
uint16_t stub_r16(void* c, uint32_t a)  { Stub* s=(Stub*)c; uint32_t o=a-s->mem_base;
                                          if (o+1>=sizeof(s->mem)) return 0;
                                          return (uint16_t)(s->mem[o] | (s->mem[o+1]<<8)); }
uint32_t stub_r32(void* c, uint32_t a)  {
    Stub* s = (Stub*)c;
    // ⛔ THE SAVEBLOCK POINTER IS SPECIAL. When pointer_valid is false the real game has not
    // allocated SaveBlock1 yet, and the adapter must NOT report a position. Returning 0 here
    // is what a real console does at that moment, so the stub does the same rather than a
    // convenient non-zero value.
    if (a == 0x03005008 && !s->pointer_valid) return 0;
    uint32_t o = a - s->mem_base;
    if (o+3 >= sizeof(s->mem)) return 0;
    return (uint32_t)(s->mem[o] | (s->mem[o+1]<<8) | (s->mem[o+2]<<16) | ((uint32_t)s->mem[o+3]<<24));
}
void stub_speak(void* c, const char* u, bool) { Stub* s=(Stub*)c; if (s->spoke<32) { strncpy(s->spoken[s->spoke], u, 255); s->spoke++; } }
void stub_log  (void* c, const char* u)       { Stub* s=(Stub*)c; if (s->logged<32){ strncpy(s->logs[s->logged], u, 255); s->logged++; } }
void stub_btn  (void*, int, bool)             { }

int failures = 0;
void check(const char* label, bool got, bool want) {
    bool ok = (got == want);
    if (!ok) failures++;
    printf("  %-58s %s\n", label, ok ? "ok" : "FAIL");
}
void check_str(const char* label, const char* got, const char* want) {
    bool ok = got && strcmp(got, want) == 0;
    if (!ok) failures++;
    printf("  %-58s %s   got=%s\n", label, ok ? "ok" : "FAIL", got ? got : "(null)");
}

/// Point the stub at a save block at `sb_addr` with the given player coords.
void setup_saveblock(uint32_t sb_addr, uint16_t x, uint16_t y) {
    uint32_t off = sb_addr - g_stub.mem_base;
    memset(g_stub.mem, 0, sizeof g_stub.mem);
    // write the pointer value into mem at the pointer's location
    uint32_t p_off = 0x03005008 - g_stub.mem_base;
    (void) p_off;  // the pointer is handled by the special case in stub_r32
    g_stub.mem[off + 0] = (uint8_t)(x & 0xFF);
    g_stub.mem[off + 1] = (uint8_t)(x >> 8);
    g_stub.mem[off + 2] = (uint8_t)(y & 0xFF);
    g_stub.mem[off + 3] = (uint8_t)(y >> 8);
    g_stub.pointer_valid = true;
    // The adapter reads read32(0x03005008) to get the base. The stub returns 0 for that
    // address unless we make it return the save block address — do that by stashing it.
    // Implemented via a dedicated field rather than a real memory cell so the test stays
    // clear about what is being simulated.
}

} // namespace

// The stub needs to hand back the save-block address from the special pointer read.
// Declared here so the linkage is obvious.
namespace { uint32_t g_saveblock_addr = 0; }

// Patch: make the pointer read return the configured address.
static uint32_t stub_r32_patched(void* c, uint32_t a) {
    Stub* s = (Stub*)c;
    if (a == 0x03005008) return s->pointer_valid ? g_saveblock_addr : 0;
    uint32_t o = a - s->mem_base;
    if (o+3 >= sizeof(s->mem)) return 0;
    return (uint32_t)(s->mem[o] | (s->mem[o+1]<<8) | (s->mem[o+2]<<16) | ((uint32_t)s->mem[o+3]<<24));
}

int main(void) {
    printf("GBA adapter host test (stub host: only the six declared callbacks)\n\n");

    oga::Host host = {};
    host.read8  = stub_r8;
    host.read16 = stub_r16;
    host.read32 = stub_r32_patched;
    host.speak  = stub_speak;
    host.log    = stub_log;
    host.set_button = stub_btn;
    host.ctx    = &g_stub;

    int n = 0;
    const oga::Adapter* const* list = oga::all_adapters(&n);
    printf("registry: %d adapter(s)\n", n);
    bool found = false;
    for (int i = 0; i < n; ++i) if (strcmp(list[i]->id, "gba") == 0) found = true;
    check("the gba adapter is registered", found, true);

    const oga::Adapter* a = nullptr;
    for (int i = 0; i < n; ++i) if (strcmp(list[i]->id, "gba") == 0) a = list[i];
    if (!a) { printf("\n!! cannot continue without the gba adapter\n"); return 1; }

    printf("\n-- selection by game code --\n");
    oga::gba_set_game_code("BPRE");
    check("accepts BPRE (FireRed)", a->attach(&host), true);
    oga::gba_set_game_code("BPEE");
    check("accepts BPEE (Emerald)", a->attach(&host), true);
    oga::gba_set_game_code("AXVE");
    check("REFUSES AXVE (Ruby - not supported by v3.1.0)", a->attach(&host), false);
    oga::gba_set_game_code("");
    check("accepts an empty code (GB/GBC title)", a->attach(&host), true);

    printf("\n-- readiness is honest before the save block exists --\n");
    oga::gba_set_game_code("BPRE");
    a->attach(&host);
    g_stub.pointer_valid = false;
    a->on_frame();
    check("not ready when SaveBlock1 is unallocated", a->ready(), false);
    g_stub.spoke = 0;
    a->command(oga::Command::WhereAmI);
    check_str("says 'loading' rather than inventing (0,0)",
              g_stub.spoke ? g_stub.spoken[0] : "", "Still loading.");

    printf("\n-- position once the save block exists --\n");
    const uint32_t SB = 0x02000000 + 0x1000;
    g_saveblock_addr = SB;
    setup_saveblock(SB, 6, 6);
    a->on_frame();
    check("ready when SaveBlock1 is allocated", a->ready(), true);
    g_stub.spoke = 0;
    a->command(oga::Command::WhereAmI);
    check_str("reports the player position", g_stub.spoke ? g_stub.spoken[0] : "", "x 6, y 6.");

    printf("\n-- a second read after moving reports a DELTA --\n");
    setup_saveblock(SB, 10, 8);
    a->on_frame();
    g_stub.spoke = 0;
    a->command(oga::Command::WhereAmI);
    check_str("reports movement, not just coordinates",
              g_stub.spoke ? g_stub.spoken[0] : "", "x 10, y 8. Moved 4 right, 2 down.");

    printf("\n-- commands that do not apply say so --\n");
    g_stub.spoke = 0;
    a->command(oga::Command::NextEnemy);
    check_str("NextEnemy is refused honestly",
              g_stub.spoke ? g_stub.spoken[0] : "", "Not applicable in this game.");

    printf("\n-- the dump reports what it can vouch for --\n");
    g_stub.logged = 0;
    a->command(oga::Command::DumpState);
    check("DumpState produced a log line", g_stub.logged >= 1, true);
    if (g_stub.logged) printf("      %s\n", g_stub.logs[0]);

    a->detach();
    check("detach clears readiness", a->ready(), false);

    printf("\n");
    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
