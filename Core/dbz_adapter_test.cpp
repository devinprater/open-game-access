/*
 * dbz_adapter_test.cpp — host test for the AotS adapter, against SYNTHETIC memory.
 *
 * WHY SYNTHETIC: the adapter's job is to turn bytes into speech, and the bytes that
 * matter are already known exactly (measured, then confirmed against the game's own
 * Status screens). Feeding it a fake RAM image built from that verified layout tests
 * the adapter's LOGIC — validation, bounds, wording — without booting a 128 MB ROM,
 * and it runs in milliseconds instead of five minutes.
 *
 * ⛔ THE TESTS THAT MATTER MOST are the boundary ones. The bug that cost this project
 * the most time was a record base one stride too high, which made every pointer
 * decode to a PLAUSIBLE NEIGHBOUR — nothing looked broken. Those tests therefore give
 * the adapter ONLY the bad pointer (all other slots zeroed) and require it to say
 * "no party members found". Leaving a valid neighbouring slot in place would let the
 * test pass while the adapter still accepted the bad pointer.
 *
 * ⛔ THE MOCK MUST MAP ABSOLUTE ADDRESSES. The adapter reads ABSOLUTE RAM addresses
 * (0x020CD660); the mock has to translate them to a buffer offset, exactly as a real
 * Host does. A mock that indexes its buffer directly with the absolute address
 * overflows instantly — and that crash looks like an adapter bug when it is a test
 * bug. Subtract the base, then mask with the buffer size.
 */
#include "adapter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t RAM[0x400000];
static const uint32_t RAMBASE = 0x02000000;
static inline size_t OFF(uint32_t a) { return (size_t)((a - RAMBASE) & 0x3FFFFF); }

static char SPOKEN[64][192]; static int NSPOKEN = 0;
static char LOGGED[64][256]; static int NLOGGED = 0;

static uint8_t  r8 (void*, uint32_t a) { return RAM[OFF(a)]; }
static uint16_t r16(void*, uint32_t a) { uint16_t v; memcpy(&v, &RAM[OFF(a)], 2); return v; }
static uint32_t r32(void*, uint32_t a) { uint32_t v; memcpy(&v, &RAM[OFF(a)], 4); return v; }
static void spk(void*, const char* s, bool) { if (NSPOKEN < 64) snprintf(SPOKEN[NSPOKEN++], 192, "%s", s); }
static void lg (void*, const char* s) { if (NLOGGED < 64) snprintf(LOGGED[NLOGGED++], 256, "%s", s); }
static void btn(void*, int, bool) {}

// ⛔ DECLARE IT INSIDE THE NAMESPACE. `extern const oga::Adapter name;` at global
// scope declares a DIFFERENT variable, and the link error ("undefined reference to
// kDragonBallZSaiyans") reads as a missing adapter rather than a wrong declaration.
namespace oga {
extern const Adapter kDragonBallZSaiyans;
extern const Adapter kFireEmblemShadowDragon;
extern const Adapter kGameBoyAdvance;
}

static void put16(uint32_t a, uint16_t v) { memcpy(&RAM[OFF(a)], &v, 2); }
static void put32(uint32_t a, uint32_t v) { memcpy(&RAM[OFF(a)], &v, 4); }
static void putname(uint32_t a, const char* s) { strncpy((char*)&RAM[OFF(a)], s, 15); }

static const uint32_t RB = 0x020CD660, ST = 0x24C, NAME = 0x114;
static const uint32_t PARTY_PTR = 0x020CC774, PARTY_CNT = 0x020CC794;

/// Fill the party array with exactly the given pointers (plus a NUL terminator),
/// zeroing every other slot first so a test can isolate one bad entry.
static void set_party(const uint32_t* ptrs, int n)
{
    for (int i = 0; i < 4; i++) put32(PARTY_PTR + (uint32_t) i * 4, 0);
    for (int i = 0; i < n && i < 4; i++) put32(PARTY_PTR + (uint32_t) i * 4, ptrs[i]);
    put32(PARTY_CNT, ((uint32_t) n << 16) | (uint32_t) n);
}

int main()
{
    const char* names[8] = {"Goku","Gohan","Piccolo","Krillin",
                            "Tien","Yamcha","Bubbles","Gregory"};
    for (int i = 0; i < 8; i++) putname(RB + i * ST + NAME, names[i]);

    // The values the game's Status screen showed, from the run the RAM was dumped in.
    put16(RB+3*ST+0x0A0,300); put16(RB+3*ST+0x0A2,300);
    put16(RB+3*ST+0x0B0,105); put16(RB+3*ST+0x0B2,105); put16(RB+3*ST+0x240,70);
    put16(RB+4*ST+0x0A0,320); put16(RB+4*ST+0x0A2,320);
    put16(RB+4*ST+0x0B0,110); put16(RB+4*ST+0x0B2,110); put16(RB+4*ST+0x240,79);
    put16(RB+5*ST+0x0A0,305); put16(RB+5*ST+0x0A2,305);
    put16(RB+5*ST+0x0B0,100); put16(RB+5*ST+0x0B2,100); put16(RB+5*ST+0x240,61);

    oga::Host H = {};
    H.read8 = r8; H.read16 = r16; H.read32 = r32;
    H.speak = spk; H.log = lg; H.set_button = btn; H.ctx = nullptr;

    int pass = 0, fail = 0;
    #define CHECK(c,m) do { if (c) { pass++; } else { fail++; printf("  FAIL: %s\n", m); } } while (0)

    oga::kDragonBallZSaiyans.attach(&H);

    CHECK(!oga::kDragonBallZSaiyans.ready(),
          "not ready before the party array exists");

    const uint32_t krillin = RB + 3*ST, tien = RB + 4*ST, yamcha = RB + 5*ST;

    // ---- an empty party is reported, not guessed at --------------------------
    set_party(nullptr, 0);
    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "an empty party says so instead of inventing a member");

    // ---- the measured party --------------------------------------------------
    const uint32_t party[3] = { krillin, tien, yamcha };
    set_party(party, 3);
    CHECK(oga::kDragonBallZSaiyans.ready(), "ready once the party array is valid");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin")
                       && strstr(SPOKEN[0], "300")
                       && strstr(SPOKEN[0], "105"),
          "the FIRST NextAlly reads slot 0 (Krillin, HP 300 / Ki 105)");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Tien") && strstr(SPOKEN[0], "320"),
          "the second NextAlly advances to Tien with HP 320");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::PrevAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin"),
          "PrevAlly steps back to Krillin");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Tien"), "NextAlly steps forward again");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Yamcha") && strstr(SPOKEN[0], "305"),
          "the third NextAlly advances to Yamcha with HP 305");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin"),
          "NextAlly wraps back to the first member");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not applicable"),
          "NextEnemy refuses honestly rather than inventing an answer");

    // ---- ⛔ THE REGRESSION GUARDS --------------------------------------------
    // A pointer 0x20 off a record base is EXACTLY the shape of the one-stride bug.
    // All other slots are empty, so the adapter gets no valid fallback: if it accepts
    // this pointer it will name a wrong character and the test will see it.
    const uint32_t offb[1] = { RB + 3*ST + 0x20 };
    set_party(offb, 1);
    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a pointer off a record boundary is REJECTED, not decoded to a neighbour");

    // A pointer past the end of the array.
    const uint32_t past[1] = { RB + 40*ST };
    set_party(past, 1);
    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a pointer past the end of the array is rejected");

    // A pointer with a bad alignment.
    const uint32_t mis[1] = { RB + 3*ST + 2 };
    set_party(mis, 1);
    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a misaligned pointer is rejected");

    // A record whose name is not text — refuse rather than read garbage aloud.
    // ⛔ ISOLATE IT: with valid neighbours in the array the walk would simply find
    // the next good member and the test would pass while the bad record was still
    // accepted. The bad record must be the ONLY slot.
    put16(RB + 3*ST + NAME, 0x0102);      // non-printable where the name should be
    const uint32_t corrupt[1] = { krillin };
    set_party(corrupt, 1);
    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a record whose name is not text is rejected");
    putname(RB + 3*ST + NAME, "Krillin");

    // ---- reporting -----------------------------------------------------------
    set_party(party, 3);
    NLOGGED = 0; oga::kDragonBallZSaiyans.command(oga::Command::DumpState);
    CHECK(NLOGGED >= 2 && strstr(LOGGED[0], "base=0x020CD660")
                       && strstr(LOGGED[0], "stride=0x24C"),
          "DumpState logs the verified base and stride");

    NSPOKEN = 0; oga::kDragonBallZSaiyans.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Krillin"),
          "WhereAmI names the current member without claiming map knowledge");

    oga::kDragonBallZSaiyans.detach();
    CHECK(!oga::kDragonBallZSaiyans.ready(), "detach clears readiness");

    printf("  dbz adapter: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

// Stubs so adapters.o links without dragging in the FE/GBA machinery.
namespace oga {
static bool s_att(const Host*) { return false; }
static void s_frm() {}
static void s_cmd(Command) {}
static bool s_rdy() { return false; }
static void s_det() {}
const Adapter kFireEmblemShadowDragon = {"fe11","FE","YFEE",s_att,s_frm,s_cmd,s_rdy,s_det};
const Adapter kGameBoyAdvance        = {"gba","GBA","",    s_att,s_frm,s_cmd,s_rdy,s_det};
}
