// debug_channel_test.cpp — host test for the in-game debug channel (Core/debug_channel.h).
//
// A fake console: 4 KiB of RAM at 0x1000, a pad that records every press and release with
// the frame it happened on, and an exec-hook table. No ROM, no emulator. Run through
// scripts/debug-channel-test.sh, which also rebuilds with each SABOTAGE_* switch and requires
// this test to FAIL, so every rule below is shown to be tested.
#define OGA_DEBUG_CHANNEL_IMPLEMENTATION
#include "debug_channel.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>

using namespace oga;

static int g_fail = 0, g_pass = 0;
static void check(bool cond, const char* what)
{
    if (cond) { g_pass++; return; }
    g_fail++;
    printf("FAIL: %s\n", what);
}

struct Fake {
    uint8_t ram[4096] = {0};
    bool can_write = true;
    bool has_hooks = true;
    uint64_t frame = 0;
    std::vector<std::string> out;
    std::vector<bool> spoken;
    std::vector<std::string> pad;          // "f<frame> <button> down|up"
    uint32_t held = 0;
    std::set<uint32_t> armed;
    int arm_calls = 0, disarm_calls = 0;
};

static Fake F;

static bool FRead(void*, uint32_t a, int w, uint32_t* out)
{
    if (a < 0x1000 || a + (uint32_t) w > 0x2000) return false;
    uint32_t v = 0;
    for (int i = 0; i < w; i++) v |= (uint32_t) F.ram[a - 0x1000 + i] << (8 * i);
    *out = v;
    return true;
}
static bool FWrite(void*, uint32_t a, int w, uint32_t v)
{
    if (a < 0x1000 || a + (uint32_t) w > 0x2000) return false;
    for (int i = 0; i < w; i++) F.ram[a - 0x1000 + i] = (uint8_t) (v >> (8 * i));
    return true;
}
static void FButton(void*, int b, bool down)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "f%llu %s %s", (unsigned long long) F.frame, kDebugButtonNames[b], down ? "down" : "up");
    F.pad.push_back(buf);
    if (down) F.held |= 1u << b; else F.held &= ~(1u << b);
}
static bool FHook(void*, uint32_t a, bool enable)
{
    if (enable) { F.arm_calls++; F.armed.insert(a); }
    else { F.disarm_calls++; F.armed.erase(a); }
    return true;
}
static void FOut(void*, const char* line, bool spoken)
{
    F.out.push_back(line);
    F.spoken.push_back(spoken);
}

static DebugChannel* Make()
{
    F = Fake();
    DebugHost h = {};
    h.read = FRead;
    h.write = F.can_write ? FWrite : nullptr;
    h.set_button = FButton;
    h.exec_hook = F.has_hooks ? FHook : nullptr;
    h.out = FOut;
    return debug_create(&h);
}

// Emulate one frame: the console runs, then the channel runs, exactly as poke_frame does.
static void Step(DebugChannel* ch, int n = 1)
{
    for (int i = 0; i < n; i++) { F.frame++; debug_frame(ch, F.frame); }
}

static bool Said(const char* needle)
{
    for (const std::string& s : F.out) if (s.find(needle) != std::string::npos) return true;
    return false;
}
static int CountSaid(const char* needle)
{
    int n = 0;
    for (const std::string& s : F.out) if (s.find(needle) != std::string::npos) n++;
    return n;
}

static void TestPeekPoke()
{
    DebugChannel* ch = Make();
    F.ram[0x10] = 0x34; F.ram[0x11] = 0x12;
    debug_submit(ch, "peek 0x1010 2\npeek $1010\npeek 1010 1 4\npeek 0x9000");
    Step(ch);
    check(Said("peek 0x00001010 = 0x1234 (4660)"), "peek width 2 reads little-endian");
    check(Said("peek 0x00001010 = 0x34 (52)"), "$ and bare hex are addresses too");
    check(Said("0x00001010: 34 12 00 00"), "peek with a count dumps a row");
    check(Said("peek 0x00009000: not readable"), "an unreadable address says so, never 0");

    debug_submit(ch, "poke 0x1020 0x1FF 2\npoke 0x1022 300\npoke 0x1024 255");
    Step(ch);
    check(F.ram[0x20] == 0xFF && F.ram[0x21] == 0x01, "poke writes width 2 little-endian");
    check(Said("reads back 0x01FF"), "poke reads back what landed");
    check(Said("0x12C does not fit in 1 bytes") && F.ram[0x22] == 0, "an oversize value is refused, not truncated");
    check(F.ram[0x24] == 255, "a decimal value is decimal");
    debug_destroy(ch);

    // No write path: the NES is read-only by contract.
    F = Fake();
    DebugHost h = {}; h.read = FRead; h.out = FOut;
    ch = debug_create(&h);
    debug_submit(ch, "poke 0x1000 1");
    F.frame = 1; debug_frame(ch, 1);
    check(Said("poke refused: this console has no write path") && F.ram[0] == 0, "no write op is a spoken refusal");
    debug_destroy(ch);
}

static void TestPadSequence()
{
    DebugChannel* ch = Make();
    // A menu walk: two downs, a wait, then a confirm. Every press must reach the game as its
    // own press, with the next line held until the hold and a released frame have passed.
    debug_submit(ch, "pad down 3\npad down 3\nwait 5\npad a+b 2\nstatus");
    Step(ch);   // frame 1: first pad presses
    check(F.pad.size() == 1 && F.pad[0] == "f1 down down", "the first pad presses on the frame it runs");
    check(debug_busy(ch) && debug_pending(ch) == 4, "the lines after a pad wait");
    Step(ch, 3);  // frames 2..4: released at 4
    check(F.pad.size() == 2 && F.pad[1] == "f4 down up", "a 3-frame hold is let go 3 frames later");
    check(F.held == 0, "nothing held after the hold");
    Step(ch);  // frame 5: second press, one released frame after the first
    check(F.pad.size() == 3 && F.pad[2] == "f5 down down", "a released frame separates two presses of one button");
    Step(ch, 3);  // frame 8 release; wait starts at 9
    Step(ch);     // frame 9: wait 5 -> busy until 14
    check(F.pad.size() == 4, "wait holds the next pad");
    Step(ch, 4);  // frames 10..13
    check(F.pad.size() == 4, "wait holds for its full count");
    Step(ch);     // frame 14: a+b
    check(F.pad.size() == 6 && F.pad[4] == "f14 a down" && F.pad[5] == "f14 b down", "a+b presses both buttons");
    Step(ch, 3);  // release at 16, status at 17
    check(Said("pad free"), "status runs after the last hold, with the pad free");

    debug_submit(ch, "pad jump 3");
    Step(ch);
    check(Said("pad: unknown button in 'jump'") && F.held == 0, "a misspelt button presses nothing and says so");
    debug_submit(ch, "pad a 300ms");
    Step(ch);
    check(Said("pad a for 18 frames"), "300 ms is 18 frames at 60 fps");
    debug_submit(ch, "pad cross 601");
    Step(ch, 40);
    check(Said("pad: at most 600 frames"), "a hold longer than ten seconds is refused");
    debug_destroy(ch);
}

static void TestResetReleases()
{
    DebugChannel* ch = Make();
    debug_submit(ch, "pad start 100\npeek 0x1000");
    Step(ch);
    check(F.held != 0, "start is held");
    debug_reset(ch);
    check(F.held == 0, "reset lets go of a held button, so it cannot leak into the next game");
    check(debug_pending(ch) == 0 && !debug_busy(ch), "reset drops queued lines");
    debug_destroy(ch);
}

static void TestWatch()
{
    DebugChannel* ch = Make();
    debug_submit(ch, "watch 0x1100\nwatch 0x1104 4\nwatch 0x8000");
    Step(ch);
    check(Said("watch 0x00001100 = 0x00"), "watch reports the starting value");
    check(Said("watch refused: 0x00008000 is not readable"), "an unreadable watch is refused");
    F.ram[0x100] = 2;
    Step(ch);
    check(Said("watch 0x00001100 0x00 -> 0x02 frame 2"), "a change is reported with its frame");
    Step(ch);
    check(CountSaid("watch 0x00001100 0x") == 1, "no change, no line");
    F.ram[0x104] = 0x78; F.ram[0x107] = 0x12;
    Step(ch);
    check(Said("0x00000000 -> 0x12000078"), "a width-4 watch sees the whole word");

    // A frame counter: changes every frame and must be muted after kDebugNoisyFrames.
    debug_submit(ch, "watch 0x1200");
    Step(ch);
    for (int i = 0; i < 100; i++) { F.ram[0x200]++; Step(ch); }
    int noisy = CountSaid("watch 0x00001200 0x");
    check(noisy == kDebugNoisyFrames - 1, "a watch that changes every frame stops reporting");
    check(Said("changes every frame; muted"), "and says once why it went quiet");
    debug_submit(ch, "watch 0x1200");
    Step(ch);
    F.ram[0x200]++;
    Step(ch);
    check(CountSaid("watch 0x00001200 0x") == noisy + 1, "watching again re-arms a muted watch");

    // A poke on a watched address is ours, not the game's.
    int before = CountSaid("watch 0x00001100 0x");
    debug_submit(ch, "poke 0x1100 9");
    Step(ch, 2);
    check(CountSaid("watch 0x00001100 0x") == before, "a poke is not reported as the game's change");
    debug_submit(ch, "unwatch all");
    Step(ch);
    F.ram[0x100] = 77;
    Step(ch);
    check(CountSaid("watch 0x00001100 0x") == before, "unwatch all stops reporting");
    debug_destroy(ch);
}

static void TestCountTrace()
{
    DebugChannel* ch = Make();
    debug_submit(ch, "count 0x8000100 0x8000200\ntrace 0x8000300 2");
    Step(ch);
    check(F.armed.size() == 3, "count and trace arm real hooks");
    for (int i = 0; i < 5; i++) debug_exec_hit(ch, 0x8000100, nullptr, 0);
    debug_exec_hit(ch, 0x8000200, nullptr, 0);
    debug_exec_hit(ch, 0x8000999, nullptr, 0);   // not ours: ignored
    uint32_t args[4] = { 1, 2, 3, 4 };
    debug_exec_hit(ch, 0x8000300, args, 4);
    debug_exec_hit(ch, 0x8000300, args, 2);
    debug_exec_hit(ch, 0x8000300, args, 4);      // a third hit after the trace ran out
    check(CountSaid("trace 0x08000300 frame") == 2, "trace reports exactly N calls");
    check(Said("a0=0x00000001 a1=0x00000002 a2=0x00000003 a3=0x00000004"), "trace carries the argument registers");
    check(Said("trace 0x08000300 done"), "a finished trace says so");
    check(F.armed.count(0x8000300) == 1, "the finished hook is not removed inside its own hit");
    Step(ch);
    check(F.armed.count(0x8000300) == 0, "the finished trace is disarmed between frames");
    debug_submit(ch, "count dump");
    Step(ch);
    // Most called first.
    size_t i100 = 0, i200 = 0;
    for (size_t i = 0; i < F.out.size(); i++)
    {
        if (F.out[i] == "  0x08000100 5") i100 = i;
        if (F.out[i] == "  0x08000200 1") i200 = i;
    }
    check(i100 && i200 && i100 < i200, "count dump lists counts, most called first");
    debug_submit(ch, "count reset\ncount dump\nuncount 0x8000100");
    Step(ch);
    check(Said("  0x08000100 0"), "count reset zeroes the counters");
    check(F.armed.count(0x8000100) == 0 && F.armed.count(0x8000200) == 1, "uncount disarms one hook only");
    debug_submit(ch, "clear");
    Step(ch);
    check(F.armed.empty(), "clear disarms every hook");
    debug_destroy(ch);

    // A console with no exec hooks refuses in words.
    F = Fake();
    DebugHost h = {}; h.read = FRead; h.out = FOut;
    ch = debug_create(&h);
    debug_submit(ch, "count 0x100\ntrace 0x100");
    F.frame = 1; debug_frame(ch, 1);
    check(Said("count refused: this console has no exec hooks"), "no exec hooks: count refuses");
    check(Said("trace refused: this console has no exec hooks"), "no exec hooks: trace refuses");
    debug_destroy(ch);
}

static void TestSpeechAndParsing()
{
    DebugChannel* ch = Make();
    debug_submit(ch, "peek 0x1000\r\n# a comment line\n\n  PEEK 0x1001   # trailing comment\nfrobnicate\nhelp");
    Step(ch);
    check(CountSaid("peek 0x0000100") == 2, "CRLF, comments, blank lines and case are tolerated");
    check(Said("unknown command 'frobnicate'"), "an unknown command says so");
    check(Said("pad BUTTONS"), "help lists the pad command");
    bool any_spoken = false;
    for (bool s : F.spoken) any_spoken |= s;
    check(!any_spoken, "speech is off by default");

    debug_submit(ch, "speak on\npeek 0x1000\nhelp\nwatch 0x1300");
    Step(ch);
    F.ram[0x300] = 5;
    Step(ch);
    bool peek_spoken = false, help_spoken = false, watch_spoken = false;
    for (size_t i = 0; i < F.out.size(); i++)
    {
        if (F.out[i].find("peek 0x00001000") == 0 && F.spoken[i]) peek_spoken = true;
        if (F.out[i].find("pad BUTTONS") != std::string::npos && F.spoken[i]) help_spoken = true;
        if (F.out[i].find("watch 0x00001300 0x00 -> 0x05") == 0 && F.spoken[i]) watch_spoken = true;
    }
    check(peek_spoken, "speak on voices replies");
    check(watch_spoken, "speak on voices watch changes");
    check(!help_spoken, "help text is never spoken (it belongs in the log)");
    debug_destroy(ch);
}

static void Put16(uint32_t a, uint16_t v) { F.ram[a - 0x1000] = (uint8_t) v; F.ram[a - 0x1000 + 1] = (uint8_t) (v >> 8); }

// A memory search the way a player maps a health bar with no debugger: remember the range,
// take a hit, keep what went down, wait, keep what stayed, until one address is left.
static void TestSearch()
{
    DebugChannel* ch = Make();
    Put16(0x1500, 166);            // the real health bar
    Put16(0x1600, 166);            // a decoy that also starts full but never moves
    Put16(0x1700, 5);              // a timer that counts down every step
    debug_submit(ch, "search start 0x1000 0x1000 2\nsearch eq 166");
    Step(ch);
    check(Said("search started: 2048 addresses of width 2 from 0x00001000"), "search start remembers every aligned halfword");
    check(Said("search eq: 2 left"), "eq keeps the addresses that hold the value now");
    check(Said("  0x00001500 = 0x00A6 (166)") && Said("  0x00001600 = 0x00A6 (166)"),
          "with few left, a step names them");

    Put16(0x1500, 140); Put16(0x1700, 4);
    debug_submit(ch, "search down");
    Step(ch);
    check(Said("search down: 1 left"), "down drops the decoy that did not move");

    // Unchanged since the LAST step (140), not since the start (166).
    Put16(0x1700, 3);
    debug_submit(ch, "search same");
    Step(ch);
    check(Said("search same: 1 left"), "same compares with the previous step, not the start");

    Put16(0x1500, 100);
    debug_submit(ch, "search down 40\nsearch list");
    Step(ch);
    check(Said("search down: 1 left") && Said("  0x00001500 = 0x0064 (100)"), "down by exactly N follows the bar");
    check(Said("search: 1 left"), "list says how many are left");

    debug_submit(ch, "search up\nsearch eq 1");
    Step(ch);
    check(Said("search up: nothing left. The search is over"), "an empty result ends the search and says so");
    check(Said("search: no search under way"), "a step with no search says how to start one");

    // A wide start: a counter going up by one among thousands of zeros.
    debug_submit(ch, "search start 0x1000 0x1000 1");
    Step(ch);
    F.ram[0x123]++; F.ram[0x456] = 7;
    debug_submit(ch, "search changed");
    Step(ch);
    check(Said("search changed: 2 left"), "changed keeps everything that moved");
    F.ram[0x123]++; F.ram[0x456] = 9;
    debug_submit(ch, "search up 1");
    Step(ch);
    check(Said("search up: 1 left") && Said("  0x00001123 = 0x02 (2)"), "up by 1 finds the counter");

    debug_submit(ch, "search start 0x1000 0x300000\nsearch start 0x9000 16\nsearch start 0x1000 16 2\nsearch eq 70000");
    Step(ch);
    check(Said("search: at most 0x200000 bytes at a time"), "a search wider than 2 MiB is refused");
    check(Said("search refused: nothing readable from 0x00009000"), "an unreadable range is refused");
    check(Said("search: 0x11170 does not fit in 2 bytes"), "a value too wide for the search is refused");

    debug_submit(ch, "status\nclear");
    Step(ch);
    debug_submit(ch, "search list");   // its own file: clear drops the lines queued after it
    Step(ch);
    check(Said("search: 8 candidates of width 2"), "status reports a search under way");
    check(CountSaid("search: no search under way") == 2, "clear ends the search too");

    debug_submit(ch, "search start 0x1000 64");
    Step(ch);
    debug_reset(ch);
    debug_submit(ch, "search list");
    Step(ch);
    check(CountSaid("search: no search under way") == 3, "reset ends the search, so it never runs into the next game");
    debug_destroy(ch);
}

int main()
{
    TestPeekPoke();
    TestPadSequence();
    TestResetReleases();
    TestWatch();
    TestCountTrace();
    TestSpeechAndParsing();
    TestSearch();
    printf("debug channel: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
