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

    // The values the game's MEMBER screen showed, from the run the RAM was
    // dumped in (menu-pos3.bin, 2026-10-25). Stats are u32 triples
    // (cur/max/copy at +0/+4/+8) — the fixture writes the true layout.
    put32(RB+3*ST+0x0A0,300); put32(RB+3*ST+0x0A4,300); put32(RB+3*ST+0x0A8,300);
    put32(RB+3*ST+0x0B0,105); put32(RB+3*ST+0x0B4,105); put32(RB+3*ST+0x0B8,105); put32(RB+3*ST+0x240,70);
    put32(RB+4*ST+0x0A0,320); put32(RB+4*ST+0x0A4,320); put32(RB+4*ST+0x0A8,320);
    put32(RB+4*ST+0x0B0,110); put32(RB+4*ST+0x0B4,110); put32(RB+4*ST+0x0B8,110); put32(RB+4*ST+0x240,79);
    put32(RB+5*ST+0x0A0,305); put32(RB+5*ST+0x0A4,305); put32(RB+5*ST+0x0A8,305);
    put32(RB+5*ST+0x0B0,100); put32(RB+5*ST+0x0B4,100); put32(RB+5*ST+0x0B8,100); put32(RB+5*ST+0x240,61);

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
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "an empty party says so instead of inventing a member");

    // ---- the measured party --------------------------------------------------
    const uint32_t party[3] = { krillin, tien, yamcha };
    set_party(party, 3);
    CHECK(oga::kDragonBallZSaiyans.ready(), "ready once the party array is valid");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin")
                       && strstr(SPOKEN[0], "300")
                       && strstr(SPOKEN[0], "105"),
          "the FIRST NextAlly reads slot 0 (Krillin, HP 300 / Ki 105)");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Tien") && strstr(SPOKEN[0], "320"),
          "the second NextAlly advances to Tien with HP 320");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::PrevAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin"),
          "PrevAlly steps back to Krillin");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Tien"), "NextAlly steps forward again");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Yamcha") && strstr(SPOKEN[0], "305"),
          "the third NextAlly advances to Yamcha with HP 305");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN >= 1 && strstr(SPOKEN[0], "Krillin"),
          "NextAlly wraps back to the first member");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextEnemy);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Not applicable"),
          "NextEnemy refuses honestly rather than inventing an answer");

    // ---- ⛔ THE REGRESSION GUARDS --------------------------------------------
    // A pointer 0x20 off a record base is EXACTLY the shape of the one-stride bug.
    // All other slots are empty, so the adapter gets no valid fallback: if it accepts
    // this pointer it will name a wrong character and the test will see it.
    const uint32_t offb[1] = { RB + 3*ST + 0x20 };
    set_party(offb, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a pointer off a record boundary is REJECTED, not decoded to a neighbour");

    // A pointer past the end of the array.
    const uint32_t past[1] = { RB + 40*ST };
    set_party(past, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a pointer past the end of the array is rejected");

    // A pointer with a bad alignment.
    const uint32_t mis[1] = { RB + 3*ST + 2 };
    set_party(mis, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a misaligned pointer is rejected");

    // A record whose name is not text — refuse rather than read garbage aloud.
    // ⛔ ISOLATE IT: with valid neighbours in the array the walk would simply find
    // the next good member and the test would pass while the bad record was still
    // accepted. The bad record must be the ONLY slot.
    put16(RB + 3*ST + NAME, 0x0102);      // non-printable where the name should be
    const uint32_t corrupt[1] = { krillin };
    set_party(corrupt, 1);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::NextAlly);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "No party members"),
          "a record whose name is not text is rejected");
    putname(RB + 3*ST + NAME, "Krillin");

    // ---- reporting -----------------------------------------------------------
    set_party(party, 3);
    NLOGGED = 0; oga::kDragonBallZSaiyans.command(oga::Command::DumpState);
    CHECK(NLOGGED >= 2 && strstr(LOGGED[0], "base=0x020CD660")
                       && strstr(LOGGED[0], "stride=0x24C"),
          "DumpState logs the verified base and stride");

    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::WhereAmI);
    {
        // The status query now reads the WHOLE party in one press: a header plus
        // every member's line, in slot order. Concatenate and check membership,
        // because the exact line count depends on each member's NEXT line.
        char all[64 * 192]; all[0] = 0;
        for (int i = 0; i < NSPOKEN; i++) { strcat(all, SPOKEN[i]); strcat(all, "\n"); }
        CHECK(NSPOKEN >= 4 && strstr(all, "Party of 3")
                           && strstr(all, "Krillin")
                           && strstr(all, "Tien")
                           && strstr(all, "Yamcha"),
              "WhereAmI reads the whole party (header + all members) in one press");
        CHECK(strstr(all, "Krillin") < strstr(all, "Tien")
           && strstr(all, "Tien") < strstr(all, "Yamcha"),
              "the whole-party readout is in slot order");
    }

    // An empty party's status query says so instead of reading a stale cursor.
    set_party(nullptr, 0);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::WhereAmI);
    CHECK(NSPOKEN == 1 && strstr(SPOKEN[0], "Party not available"),
          "WhereAmI on an empty party says the party is not available");
    set_party(party, 3);

    // Regression (2026-10-25): HP/KI are u32s, not u16s. The old u16
    // read at +0x02 returned the high half of current (0) on real data, so a
    // damaged member must speak "HP 250 of 300", never "HP 250 of 0".
    // Layout corrected 2026-10-26 (round 9, live damage data): the triple
    // is (current +0x00, display +0x04, MAX +0x08) — this fixture writes
    // all three words, as the earlier version unknowingly relied on the
    // middle word holding the max.
    put32(RB+3*ST+0x0A0, 250); put32(RB+3*ST+0x0A4, 250); put32(RB+3*ST+0x0A8, 300);
    NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset(); oga::kDragonBallZSaiyans.command(oga::Command::WhereAmI);
    {
        char all[64 * 192]; all[0] = 0;
        for (int i = 0; i < NSPOKEN; i++) { strcat(all, SPOKEN[i]); strcat(all, "\n"); }
        CHECK(strstr(all, "Krillin. HP 250 of 300.") != nullptr,
              "a damaged member speaks HP current of max from the u32 layout");
    }
    put32(RB+3*ST+0x0A0, 300); put32(RB+3*ST+0x0A8, 300);

    // ---- dialogue (2026-10-25: witness proven live, see RE doc) ---------------
    // The message context and script below reproduce the live dumps' layout
    // byte-for-byte in miniature: SPK -> the speaker's name string in the
    // script, the buffer holds 0x0C + the processed box, P1 ends it.
    {
        const uint32_t MSG_FLAG = 0x020D5E08, MSG_P1 = 0x020D5E10;
        const uint32_t MSG_SPK  = 0x020D5E40, MSG_BUF = 0x020D5E44;
        const uint32_t SCR = 0x02283D00;
        auto putbytes = [&](uint32_t a, const char* s) {
            memcpy(&RAM[OFF(a)], s, strlen(s));
        };
        auto dlg_show = [&](const char* processed, uint32_t spk) {
            RAM[OFF(MSG_BUF)] = 0x0C;
            memcpy(&RAM[OFF(MSG_BUF) + 1], processed, strlen(processed));
            put16(MSG_FLAG, 1);
            put32(MSG_SPK, spk);
            put32(MSG_P1, MSG_BUF + 1 + (uint32_t) strlen(processed));
        };
        auto dlg_hide = [&] { put16(MSG_FLAG, 0); };
        auto frames = [&](int n) {
            for (int i = 0; i < n; i++) oga::kDragonBallZSaiyans.on_frame();
        };

        putbytes(SCR, "Krillin");
        putbytes(SCR + 8,   "<p>...!!!<n>Oh! Hey, Launch!<n>Good morning!<k>");
        putbytes(SCR + 0x60, "<p>Ahh... ahhh... ahhhh-choooo!!<k>");
        putbytes(SCR + 0xC0, "<p>Brrr! Sure is a cold one today, huh?<k>");
        putbytes(SCR + 0x100, "<p>......<k>");

        NSPOKEN = 0; oga::AdapterSpeechReset();
        dlg_show("...!!!\nOh! Hey, Launch!\nGood morning!", SCR);
        frames(1);
        CHECK(NSPOKEN == 0, "dialogue waits for a stable second frame (debounce)");
        frames(1);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Krillin: ...!!! Oh! Hey, Launch! Good morning!") == 0,
              "a confirmed box speaks with the speaker's name, newlines flattened");
        frames(3);
        CHECK(NSPOKEN == 1, "the same box does not repeat while it stays up");

        dlg_hide(); frames(3);
        dlg_show("Ahh... ahhh... ahhhh-choooo!!", SCR);
        frames(2);
        CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "Krillin: Ahh... ahhh... ahhhh-choooo!!") == 0,
              "the next box speaks after the between-box gap");

        // Narration: SPK lands on a binary record (first byte 0) -> no name.
        // Pages accumulate in the buffer; only the new page speaks.
        const uint32_t NSCR = 0x02283800, NSPK = 0x022837FA;
        putbytes(NSCR, "C13_0002");
        putbytes(NSCR + 16, "<p>Long ago, deep in the mountains, thousands of miles away from any city...<k>"
                             "<p>It is here where our amazing story begins.<k>");
        dlg_hide(); frames(3);
        dlg_show("Long ago, deep in the mountains, thousands\nof miles away from any city...", NSPK);
        frames(2);
        CHECK(NSPOKEN == 3 && strcmp(SPOKEN[2], "Long ago, deep in the mountains, thousands of miles away from any city...") == 0,
              "a narration page speaks with no name, wrap newline normalised away");
        // page 2 appended behind page 1 (0x0C-led), flag never dropped
        {
            const char* p1 = "Long ago, deep in the mountains, thousands\nof miles away from any city...";
            const char* p2 = "It is here where our amazing story begins.";
            RAM[OFF(MSG_BUF)] = 0x0C;
            memcpy(&RAM[OFF(MSG_BUF) + 1], p1, strlen(p1));
            uint32_t tail = MSG_BUF + 1 + (uint32_t) strlen(p1);
            RAM[OFF(tail)] = '\r'; RAM[OFF(tail + 1)] = 0x0C;
            memcpy(&RAM[OFF(tail + 2)], p2, strlen(p2));
            put32(MSG_P1, tail + 2 + (uint32_t) strlen(p2));
        }
        frames(2);
        CHECK(NSPOKEN == 4 && strcmp(SPOKEN[3], "It is here where our amazing story begins.") == 0,
              "a narration page advance speaks only the new page");

        // A segment the script does not contain stays silent.
        dlg_hide(); frames(3);
        dlg_show("This text is not in the script at all.", SCR);
        frames(3);
        CHECK(NSPOKEN == 4, "a segment with no script agreement stays silent");

        // A box whose speaker pointer is garbage speaks its text unnamed.
        dlg_hide(); frames(3);
        put16(0x02283F00, 0x0201);
        dlg_show("Brrr! Sure is a cold one today, huh?", 0x02283F00);
        frames(2);
        CHECK(NSPOKEN == 5 && strcmp(SPOKEN[4], "Brrr! Sure is a cold one today, huh?") == 0,
              "an unusable speaker pointer drops the name, not the confirmed text");

        // No flag, no speech — even with a perfect box in the buffer.
        dlg_show("......", SCR);
        put16(MSG_FLAG, 0);
        frames(3);
        CHECK(NSPOKEN == 5, "with the message flag down, a buffered box stays silent");

        // Two identical boxes in a row are two story beats: both speak.
        dlg_hide(); frames(3);
        dlg_show("......", SCR);
        frames(2);
        CHECK(NSPOKEN == 6 && strcmp(SPOKEN[5], "Krillin: ......") == 0,
              "an identical box speaks when it is a new beat (first)");
        dlg_hide(); frames(3);
        dlg_show("......", SCR);
        frames(2);
        CHECK(NSPOKEN == 7 && strcmp(SPOKEN[6], "Krillin: ......") == 0,
              "an identical box speaks when it is a new beat (second)");
        dlg_hide(); frames(2);
    }

    // ---- main-menu dial (2026-10-25: countdown byte proven live) --------------
    {
        const uint32_t MENU_WORD = 0x020D2128;
        auto mframes = [&](int n) {
            for (int i = 0; i < n; i++) oga::kDragonBallZSaiyans.on_frame();
        };
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put32(MENU_WORD, 0x00000203);   // a dialogue-shaped state, not the dial
        mframes(3);
        CHECK(NSPOKEN == 0, "off the dial screen, the menu watcher stays silent");
        put32(MENU_WORD, 0x05050202);   // dial open, Items (v=5)
        mframes(1);
        CHECK(NSPOKEN == 0, "menu arrival waits for a stable second frame");
        mframes(1);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Menu. Items.") == 0,
              "opening the menu speaks its arrival and the entry under the cursor");
        mframes(2);
        CHECK(NSPOKEN == 1, "resting on an entry does not repeat it");
        put32(MENU_WORD, 0x04040202);   // Skills (v=4)
        mframes(2);
        CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "Skills") == 0,
              "rotating the dial speaks the new entry");
        put32(MENU_WORD, 0x02020202);   // Status (v=2)
        mframes(2);
        CHECK(NSPOKEN == 3 && strcmp(SPOKEN[2], "Status") == 0,
              "the countdown byte maps to the right entry (Status)");
        put32(MENU_WORD, 0x06060202);   // Capsules (v=6, the UP-wrap entry)
        mframes(2);
        CHECK(NSPOKEN == 4 && strcmp(SPOKEN[3], "Capsules") == 0,
              "the wrap-around entry (Capsules) speaks");
        put32(MENU_WORD, 0x04050202);   // torn word: mirror bytes disagree
        mframes(3);
        CHECK(NSPOKEN == 4, "a torn dial word (mirror bytes differ) stays silent");
        put32(MENU_WORD, 0x00000203);   // close
        mframes(2);
        put32(MENU_WORD, 0x05050202);   // reopen on Items
        mframes(2);
        CHECK(NSPOKEN == 5 && strcmp(SPOKEN[4], "Menu. Items.") == 0,
              "reopening the menu speaks the arrival again");
        put32(MENU_WORD, 0x00000203);
        mframes(2);
    }

    // ---- HP/Ki maximums: the THIRD word of the triple (round 9, live) --------
    //
    // The record layout is (current, display, MAX): the middle word is
    // the health bar's drain animation and only equals the max while
    // undamaged. A damaged fixture (205 current, 236 still draining,
    // 300 max) must speak "205 of 300" — the pre-fix adapter read the
    // middle word and would have said "205 of 236".
    {
        put32(RB+3*ST+0x0A0,205); put32(RB+3*ST+0x0A4,236); put32(RB+3*ST+0x0A8,300);
        put32(RB+3*ST+0x0B0, 80); put32(RB+3*ST+0x0B4, 95); put32(RB+3*ST+0x0B8,105);
        NSPOKEN = 0; oga::AdapterSpeechReset(); oga::AdapterSpeechReset();
        oga::kDragonBallZSaiyans.command(oga::Command::WhereAmI);
        CHECK(NSPOKEN >= 2 && strstr(SPOKEN[1], "Krillin")
                          && strstr(SPOKEN[1], "HP 205 of 300")
                          && strstr(SPOKEN[1], "Ki 80 of 105"),
              "a damaged member speaks the true maximum (third word), not the drain value");
        put32(RB+3*ST+0x0A0,224); put32(RB+3*ST+0x0A4,224); put32(RB+3*ST+0x0A8,300);
        put32(RB+3*ST+0x0B0,105); put32(RB+3*ST+0x0B4,105); put32(RB+3*ST+0x0B8,105);
    }

    // ---- battle watcher (2026-10-26: flag/phase + enemy names proven live) --
    {
        const uint32_t BTL_FLAG = 0x020CC668, BTL_PHASE = 0x020CC66C;
        const uint32_t ENM1 = 0x020CE9D4, ENM2 = 0x020CEF88;
        auto bframes = [&](int n) {
            for (int i = 0; i < n; i++) oga::kDragonBallZSaiyans.on_frame();
        };
        auto putstr = [&](uint32_t a, const char* s) {
            memset(&RAM[OFF(a)], 0, 24); memcpy(&RAM[OFF(a)], s, strlen(s));
        };
        NSPOKEN = 0; oga::AdapterSpeechReset();
        put32(BTL_FLAG, 0); put32(BTL_PHASE, 0);
        bframes(3);
        CHECK(NSPOKEN == 0, "no battle flag, no battle speech");
        putstr(ENM1, "Giant Snake A"); putstr(ENM2, "Giant Snake B");
        put32(BTL_FLAG, 1); put32(BTL_PHASE, 0x11);
        bframes(1);
        CHECK(NSPOKEN == 0, "battle start waits for a stable second frame");
        bframes(1);
        CHECK(NSPOKEN == 1 && strcmp(SPOKEN[0], "Battle: Giant Snake A and Giant Snake B.") == 0,
              "battle start speaks both enemies' names");
        bframes(3);
        CHECK(NSPOKEN == 1, "a continuing battle does not repeat its start");
        put32(BTL_PHASE, 0x02);   // results / post-battle help chain
        bframes(2);
        CHECK(NSPOKEN == 2 && strcmp(SPOKEN[1], "Battle over.") == 0,
              "entering the post-battle chain speaks the end once");
        bframes(3);
        CHECK(NSPOKEN == 2, "the post-battle chain does not repeat the end");
        // Fail-closed: flag and phase claim a battle but slot 0's HP
        // triple is nonsense (max 0) — nothing may be spoken.
        putstr(ENM1, ""); putstr(ENM2, "");
        put32(BTL_PHASE, 0x11);
        put32(RB+3*ST+0x0A8, 0);
        bframes(3);
        CHECK(NSPOKEN == 2, "an insane HP triple keeps the battle watcher silent");
        put32(RB+3*ST+0x0A8, 300);
        bframes(2);
        CHECK(NSPOKEN == 3 && strcmp(SPOKEN[2], "Battle started.") == 0,
              "a battle with no readable enemy names falls back to the plain line");
        put32(BTL_FLAG, 0); put32(BTL_PHASE, 0);
        bframes(2);
        CHECK(NSPOKEN == 4 && strcmp(SPOKEN[3], "Battle over.") == 0,
              "the flag clearing speaks the end");
    }

    // ---- Active Guard cue (2026-10-26: descriptor block proven live) --
    // The block at 0x020CCBB0: target byte +0x03 (0x80..0x82 = the
    // target's formation ROW — never a name), window flag +0x0D,
    // ranged countdown u16 +0x0E, heavy countdown byte +0x10.
    {
        const uint32_t BTL_FLAG = 0x020CC668, BTL_PHASE = 0x020CC66C;
        const uint32_t AGT = 0x020CCBB3, AGW = 0x020CCBBD,
                       AGC = 0x020CCBBE, AGH = 0x020CCBC0;
        auto frames = [&](int n) {
            for (int i = 0; i < n; i++) oga::kDragonBallZSaiyans.on_frame();
        };
        auto snap = [&]() {
            oga::CueSnapshot s; oga::kDragonBallZSaiyans.cue_snapshot(&s); return s;
        };
        auto block = [&](uint8_t tgt, uint8_t win, uint16_t cnt, uint8_t hvy) {
            RAM[OFF(AGT)] = tgt; RAM[OFF(AGW)] = win;
            put16(AGC, cnt); RAM[OFF(AGH)] = hvy;
        };
        auto spoken_has = [&](const char* sub) {
            for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], sub)) return true;
            return false;
        };
        auto spoken_count = [&](const char* sub) {
            int n = 0;
            for (int i = 0; i < NSPOKEN; i++) if (strstr(SPOKEN[i], sub)) n++;
            return n;
        };
        put32(BTL_FLAG, 1); put32(BTL_PHASE, 0x11);
        block(0xFF, 0, 0, 0x13);
        frames(3);   // battle watcher fires its start line here
        NSPOKEN = 0; oga::AdapterSpeechReset();
        CHECK(!snap().guard_active, "an idle descriptor does not arm the guard cue");

        // The ranged family (the tutorial ghost's shape): arm at 29.
        block(0x81, 1, 29, 0x13); frames(1);
        oga::CueSnapshot s1 = snap();
        CHECK(s1.guard_active && s1.guard_beat && s1.guard_frac == 0.0f,
              "the window opens with its beat at frac 0");
        CHECK(!snap().guard_beat, "the beat is consumed by the first snapshot");
        CHECK(spoken_count("Guard!") == 1, "the setup line speaks once at the opening");
        block(0x81, 1, 15, 0x13); frames(1);
        oga::CueSnapshot s2 = snap();
        CHECK(s2.guard_frac > 0.40f && s2.guard_frac < 0.55f,
              "frac tracks the countdown against the arm value (15 of 29)");
        block(0x81, 1, 14, 0x13); frames(1);
        CHECK(!snap().guard_beat, "a continuing countdown does not re-beat");
        // Countdown zero, flag 2: launched — the sweep peaks.
        block(0x81, 2, 0, 0x13); frames(1);
        CHECK(snap().guard_frac == 1.0f, "flag 2 (launched) puts the sweep at its peak");
        // Impact: slot 0's record (Krillin) falls 224 -> 175.
        put32(RB+3*ST+0x0A0, 175); frames(1);
        CHECK(spoken_has("Krillin took 49 damage."),
              "the outcome line names the member whose HP fell, with the amount");
        block(0xFF, 0, 0, 0x13); frames(2);
        CHECK(!snap().guard_active, "the block clearing disarms the cue");
        CHECK(spoken_count("took") == 1, "the outcome line speaks once per attack");

        // Fail-closed shapes: none may arm or speak.
        int before = NSPOKEN;
        block(0x85, 1, 20, 0x13); frames(2);
        CHECK(!snap().guard_active && NSPOKEN == before,
              "a target byte outside 0x80..0x82 stays silent");
        block(0x80, 1, 900, 0x13); frames(2);
        CHECK(!snap().guard_active, "a countdown beyond the sanity bound stays silent");
        block(0x00, 0, 0, 0x05); frames(2);
        CHECK(!snap().guard_active, "a party action's block does not arm the cue");

        // Light melee: a flag-only window sweeping on its 28-frame lead.
        block(0x80, 1, 0, 0x13); frames(1);
        oga::CueSnapshot sarm = snap();
        CHECK(sarm.guard_active && sarm.guard_beat,
              "a flag-only opening arms the cue (light melee)");
        frames(13);
        oga::CueSnapshot sl = snap();
        CHECK(sl.guard_frac > 0.40f && sl.guard_frac < 0.60f,
              "the flag-only window sweeps on its witnessed lead");
        block(0xFF, 0, 0, 0x13); frames(2);
        CHECK(!snap().guard_active, "the flag clearing disarms the light window");

        // Heavy melee: +0x10 falling takes the sweep over.
        block(0x82, 1, 0, 0x13); frames(1);
        CHECK(snap().guard_active, "a heavy opening arms the cue");
        block(0x82, 1, 0, 0x0A); frames(1);
        oga::CueSnapshot sh = snap();
        CHECK(sh.guard_frac > 0.40f && sh.guard_frac < 0.55f,
              "a falling heavy countdown drives the sweep (10 of 19 left)");
        block(0xFF, 0, 0, 0x13); frames(2);
        put32(BTL_FLAG, 0); put32(BTL_PHASE, 0); frames(2);
    }

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
