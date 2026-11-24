/*
 * debug_channel.h — the in-game debug channel: one text channel a player can drive WHILE
 * playing, to read and write memory, watch an address change, count and trace calls, and
 * press buttons by script. Design note: docs/design/debug-channel.md.
 *
 * WHY THIS EXISTS. Budokai 3 Access ships the tooling its author used to find everything
 * (docs/research/budokai3access-lessons.md §6): lines written into cmd.txt while the game
 * runs, read once and deleted. Our equivalents were split across the PPSSPP WebSocket
 * debugger, BizHawk Lua and one-off scripts/<game>-live-addresses.mjs, and none of them runs on
 * the phone the blind player is holding. RetroArch's MCP server (docs/retroarch-mcp-harness.md)
 * covers the driving half on its own cores, but it reads nothing on melonDS, PPSSPP, snes9x
 * or Genesis Plus GX, has no call counter, and is not this app. This channel lives INSIDE our
 * own cores, behind the same read path the adapters use, so an address it answers is the
 * address an adapter would read.
 *
 * SHAPE. The channel is pure: it knows nothing about emulators, files or speech. The host
 * (Core/pokecore.cpp) hands it a DebugHost of function pointers, feeds it text, calls
 * debug_frame() once per emulated frame, and reports exec-hook hits back. That keeps every
 * rule here testable on the host with a fake console (Core/debug_channel_test.cpp).
 *
 * ⛔ ONE TRANSLATION UNIT. The implementation sits in this header behind
 * OGA_DEBUG_CHANNEL_IMPLEMENTATION and is compiled exactly once, inside pokecore.cpp. That is
 * deliberate: pokecore.cpp is compiled by a dozen hand-written source lists (iOS OGA_GLUE,
 * the Android DS glue, build-host.sh and every capture harness), and a new .cpp missed by one
 * of them is an undefined symbol at the end of a long build — the history in
 * scripts/adapter-sources-check.py. Riding pokecore.cpp means every build that has the core
 * has the channel. Never compile it into a second TU of the same binary.
 *
 * THREADING. debug_submit() may be called from any thread (the app's UI, a file poll); it only
 * appends to a locked buffer. Everything else runs on the frame thread.
 */
#ifndef OGA_DEBUG_CHANNEL_H
#define OGA_DEBUG_CHANNEL_H

#include <stdint.h>
#include <stdbool.h>

namespace oga {

/// What the channel may ask of the running console. A NULL member is a refusal the channel
/// reports in words ("this console has no exec hooks"), never a silent no-op.
struct DebugHost {
    /// Read `width` (1, 2 or 4) bytes, little-endian. False = not readable here.
    bool (*read)(void* ctx, uint32_t addr, int width, uint32_t* out);
    /// Write `width` bytes. NULL = this console has no write path (the NES is read-only by
    /// contract, see mesen_core.h).
    bool (*write)(void* ctx, uint32_t addr, int width, uint32_t value);
    /// Press or release one pad button, in the shared POKE_BTN_* numbering.
    void (*set_button)(void* ctx, int pad_button, bool down);
    /// Arm or disarm a real execution hook at `addr`. NULL = no exec hooks on this console.
    /// When armed, the host calls debug_exec_hit() each time the CPU reaches `addr`.
    bool (*exec_hook)(void* ctx, uint32_t addr, bool enable);
    /// One reply line. `spoken` is true when the player asked for spoken replies (`speak on`)
    /// and this line is one worth hearing; the host decides how to voice it.
    void (*out)(void* ctx, const char* line, bool spoken);
    void* ctx;
};

/// Pad button names the channel accepts, in POKE_BTN_* order (A B select start right left up
/// down r l x y). Exposed for the test and the help text.
extern const char* const kDebugButtonNames[12];

/// Limits. Every one exists so a typo in cmd.txt cannot hang a session or flood a log.
constexpr int kDebugMaxWatches   = 16;
constexpr int kDebugMaxHooks     = 32;
constexpr int kDebugMaxPeek      = 256;    // items in one peek
constexpr int kDebugMaxPadFrames = 600;    // ten seconds at 60 fps
constexpr int kDebugDefaultPad   = 6;      // frames, the same order as a tap
constexpr int kDebugNoisyFrames  = 30;     // a watch that changes this many frames running is muted
constexpr int kDebugMaxTrace     = 200;
constexpr int kDebugTraceArgs    = 4;

struct DebugChannel;

DebugChannel* debug_create(const DebugHost* host);
void debug_destroy(DebugChannel* ch);

/// Queue text: one or more lines (\n or \r\n). Thread-safe. Lines run in order on the frame
/// thread; `pad` and `wait` hold the lines after them until their frames have passed.
void debug_submit(DebugChannel* ch, const char* text);

/// Run once per emulated frame, AFTER the frame: expire a held pad, run queued lines, report
/// watched addresses that changed. `frame` is the core's frame counter.
void debug_frame(DebugChannel* ch, uint64_t frame);

/// An armed exec hook fired. `args` are the first argument registers (ARM r0..r3, MIPS
/// a0..a3), `nargs` how many the host could read (0 is fine).
void debug_exec_hit(DebugChannel* ch, uint32_t pc, const uint32_t* args, int nargs);

/// Forget everything and let go of the pad: queued lines, watches, hooks, held buttons. The
/// host calls it when the game is unloaded, so a held button never leaks into the next game.
void debug_reset(DebugChannel* ch);

/// For the host and tests: how many lines wait, and whether a pad hold or wait is running.
int debug_pending(const DebugChannel* ch);
bool debug_busy(const DebugChannel* ch);

} // namespace oga

#endif // OGA_DEBUG_CHANNEL_H

/* ======================================================================== implementation */

#ifdef OGA_DEBUG_CHANNEL_IMPLEMENTATION
#ifndef OGA_DEBUG_CHANNEL_IMPLEMENTED
#define OGA_DEBUG_CHANNEL_IMPLEMENTED

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace oga {

const char* const kDebugButtonNames[12] = {
    "a", "b", "select", "start", "right", "left", "up", "down", "r", "l", "x", "y",
};

namespace debug_detail {

struct Watch {
    uint32_t addr;
    int width;
    uint32_t last;
    int run;        // consecutive frames it changed
    bool muted;
};

struct Hook {
    uint32_t addr;
    bool counting;
    uint64_t count;
    int trace_left;
};

} // namespace debug_detail

struct DebugChannel {
    DebugHost host;
    std::mutex inbox_mu;
    std::string inbox;              // submitted, not yet split into lines
    std::deque<std::string> lines;  // waiting to run, frame thread only
    uint64_t frame = 0;
    uint64_t busy_until = 0;        // lines wait until frame >= busy_until
    uint32_t held = 0;              // POKE_BTN_* bits the channel holds down
    uint64_t release_at = 0;        // frame at which `held` is let go
    bool speak = false;
    std::vector<debug_detail::Watch> watches;
    std::vector<debug_detail::Hook> hooks;
    std::vector<uint32_t> finished;  // traces that ran out inside a hit; disarmed next frame
};

namespace {

using debug_detail::Watch;
using debug_detail::Hook;

void Out(DebugChannel* ch, bool spoken, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (ch->host.out) ch->host.out(ch->host.ctx, buf, spoken && ch->speak);
}

std::string Lower(std::string s)
{
    for (char& c : s) c = (char) std::tolower((unsigned char) c);
    return s;
}

std::vector<std::string> Words(const std::string& line)
{
    std::vector<std::string> w;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && std::isspace((unsigned char) line[i])) i++;
        size_t j = i;
        while (j < line.size() && !std::isspace((unsigned char) line[j])) j++;
        if (j > i) w.push_back(line.substr(i, j - i));
        i = j;
    }
    return w;
}

/// An ADDRESS is always hex: "0x08A852D0", "$7E0100" and "8A852D0" are the same kind of
/// thing, because every address in this repo's notes is written in hex.
bool ParseAddr(const std::string& s, uint32_t* out)
{
    std::string t = s;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    else if (!t.empty() && t[0] == '$') t = t.substr(1);
    if (t.empty() || t.size() > 8) return false;
    for (char c : t) if (!std::isxdigit((unsigned char) c)) return false;
    *out = (uint32_t) strtoul(t.c_str(), nullptr, 16);
    return true;
}

/// A VALUE or COUNT is decimal unless it says hex ("0x1F", "$1F").
bool ParseNum(const std::string& s, uint32_t* out)
{
    if (s.empty()) return false;
    if ((s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) || s[0] == '$')
        return ParseAddr(s, out);
    for (char c : s) if (!std::isdigit((unsigned char) c)) return false;
    if (s.size() > 10) return false;
    unsigned long long v = strtoull(s.c_str(), nullptr, 10);
    if (v > 0xFFFFFFFFull) return false;
    *out = (uint32_t) v;
    return true;
}

bool ParseWidth(const std::string& s, int* out)
{
    std::string t = Lower(s);
    if (t == "1" || t == "b" || t == "u8")  { *out = 1; return true; }
    if (t == "2" || t == "h" || t == "u16") { *out = 2; return true; }
    if (t == "4" || t == "w" || t == "u32") { *out = 4; return true; }
    return false;
}

/// "a", "a+b", "down,start". Returns false on any name it does not know: a misspelt button
/// must not quietly press nothing while the script carries on as if it had.
bool ParseButtons(const std::string& s, uint32_t* mask)
{
    uint32_t m = 0;
    std::string t = Lower(s);
    size_t i = 0;
    while (i <= t.size())
    {
        size_t j = t.find_first_of("+,", i);
        if (j == std::string::npos) j = t.size();
        std::string name = t.substr(i, j - i);
        if (name == "dup") name = "up";
        else if (name == "ddown") name = "down";
        else if (name == "dleft") name = "left";
        else if (name == "dright") name = "right";
        else if (name == "cross") name = "a";      // the PSP backend maps A onto Cross
        else if (name == "circle") name = "b";
        else if (name == "triangle") name = "x";
        else if (name == "square") name = "y";
        int found = -1;
        for (int k = 0; k < 12; k++) if (name == kDebugButtonNames[k]) found = k;
        if (found < 0) return false;
        m |= 1u << found;
        i = j + 1;
    }
    if (!m) return false;
    *mask = m;
    return true;
}

/// "12" or "12f" = frames; "300ms" = milliseconds at 60 fps, rounded up so a short hold is
/// never zero frames. Frames are the native unit for the same reason poke_tap_button counts
/// them: a press must span emulated frames, whatever the display link does.
bool ParseFrames(const std::string& s, int* out)
{
    std::string t = Lower(s);
    uint32_t v = 0;
    if (t.size() > 2 && t.compare(t.size() - 2, 2, "ms") == 0)
    {
        if (!ParseNum(t.substr(0, t.size() - 2), &v)) return false;
        uint64_t f = ((uint64_t) v * 60 + 999) / 1000;
        *out = (int) std::min<uint64_t>(f, (uint64_t) kDebugMaxPadFrames + 1);
        return true;
    }
    if (!t.empty() && t.back() == 'f') t.pop_back();
    if (!ParseNum(t, &v)) return false;
    *out = (int) std::min<uint32_t>(v, (uint32_t) kDebugMaxPadFrames + 1);
    return true;
}

void ReleasePad(DebugChannel* ch)
{
    if (!ch->held) return;
    for (int b = 0; b < 12; b++)
        if (ch->held & (1u << b) && ch->host.set_button) ch->host.set_button(ch->host.ctx, b, false);
    ch->held = 0;
}

std::string ButtonList(uint32_t mask)
{
    std::string s;
    for (int b = 0; b < 12; b++)
        if (mask & (1u << b)) { if (!s.empty()) s += "+"; s += kDebugButtonNames[b]; }
    return s;
}

const char* kHelp[] = {
    "commands, one per line. Addresses are hex; counts and values are decimal unless 0x.",
    "peek ADDR [WIDTH] [COUNT]   read WIDTH 1, 2 or 4 bytes; COUNT items in a row",
    "poke ADDR VALUE [WIDTH]     write, then read back",
    "watch ADDR [WIDTH]          report every change, with the frame",
    "unwatch ADDR|all",
    "pad BUTTONS [FRAMES|MSms]   hold buttons (a+b, down, start, cross); default 6 frames",
    "wait FRAMES|MSms            hold the lines after it",
    "count ADDR [ADDR...]        count calls of code at ADDR; count dump; count reset",
    "uncount ADDR|all",
    "trace ADDR [N]              report the next N calls with their first arguments",
    "status                      frame, pad, watches, hooks",
    "speak on|off                also speak replies and watch changes",
    "clear                       drop queued lines, release the pad, remove watches and hooks",
};

Hook* FindHook(DebugChannel* ch, uint32_t addr)
{
    for (Hook& h : ch->hooks) if (h.addr == addr) return &h;
    return nullptr;
}

/// Arm a hook (or reuse an armed one). Answers false with the reason already said.
Hook* ArmHook(DebugChannel* ch, uint32_t addr, const char* verb)
{
    if (Hook* h = FindHook(ch, addr)) return h;
    if (!ch->host.exec_hook)
    {
        Out(ch, true, "%s refused: this console has no exec hooks", verb);
        return nullptr;
    }
    if ((int) ch->hooks.size() >= kDebugMaxHooks)
    {
        Out(ch, true, "%s refused: %d hooks already armed", verb, kDebugMaxHooks);
        return nullptr;
    }
    if (!ch->host.exec_hook(ch->host.ctx, addr, true))
    {
        Out(ch, true, "%s refused: the console would not arm a hook at 0x%08X", verb, addr);
        return nullptr;
    }
    ch->hooks.push_back(Hook{ addr, false, 0, 0 });
    return &ch->hooks.back();
}

void DisarmIfIdle(DebugChannel* ch, uint32_t addr)
{
    for (size_t i = 0; i < ch->hooks.size(); i++)
    {
        Hook& h = ch->hooks[i];
        if (h.addr != addr) continue;
#ifndef SABOTAGE_KEEP_IDLE_HOOKS
        if (h.counting || h.trace_left > 0) return;
        if (ch->host.exec_hook) ch->host.exec_hook(ch->host.ctx, addr, false);
        ch->hooks.erase(ch->hooks.begin() + (long) i);
#endif
        return;
    }
}

void DisarmAll(DebugChannel* ch)
{
    for (const Hook& h : ch->hooks)
        if (ch->host.exec_hook) ch->host.exec_hook(ch->host.ctx, h.addr, false);
    ch->hooks.clear();
}

void CmdPeek(DebugChannel* ch, const std::vector<std::string>& w)
{
    uint32_t addr = 0, n = 1;
    int width = 1;
    if (w.size() < 2 || !ParseAddr(w[1], &addr)) { Out(ch, true, "peek needs an address"); return; }
    if (w.size() >= 3 && !ParseWidth(w[2], &width)) { Out(ch, true, "peek: width is 1, 2 or 4"); return; }
    if (w.size() >= 4 && (!ParseNum(w[3], &n) || n == 0)) { Out(ch, true, "peek: count is a number"); return; }
    if (n > (uint32_t) kDebugMaxPeek) n = kDebugMaxPeek;

    if (n == 1)
    {
        uint32_t v = 0;
        if (!ch->host.read || !ch->host.read(ch->host.ctx, addr, width, &v))
            Out(ch, true, "peek 0x%08X: not readable on this console", addr);
        else
            Out(ch, true, "peek 0x%08X = 0x%0*X (%u)", addr, width * 2, v, v);
        return;
    }
    // Rows of 16 bytes' worth, the shape of every hex dump in docs/evidence.
    const int per_row = 16 / width;
    std::string row;
    uint32_t row_addr = addr;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t a = addr + i * (uint32_t) width, v = 0;
        char cell[16];
        if (ch->host.read && ch->host.read(ch->host.ctx, a, width, &v))
            snprintf(cell, sizeof(cell), " %0*X", width * 2, v);
        else
            snprintf(cell, sizeof(cell), " %.*s", width * 2, "????????");
        row += cell;
        if ((int) (i % per_row) == per_row - 1 || i + 1 == n)
        {
            // Only the first row is worth speaking; the rest belongs to the log.
            Out(ch, row_addr == addr, "0x%08X:%s", row_addr, row.c_str());
            row.clear();
            row_addr = a + (uint32_t) width;
        }
    }
}

void CmdPoke(DebugChannel* ch, const std::vector<std::string>& w)
{
    uint32_t addr = 0, value = 0;
    int width = 1;
    if (w.size() < 3 || !ParseAddr(w[1], &addr) || !ParseNum(w[2], &value))
    { Out(ch, true, "poke needs an address and a value"); return; }
    if (w.size() >= 4 && !ParseWidth(w[3], &width)) { Out(ch, true, "poke: width is 1, 2 or 4"); return; }
    if (width < 4 && value >> (8 * width)) { Out(ch, true, "poke: 0x%X does not fit in %d bytes", value, width); return; }
    if (!ch->host.write) { Out(ch, true, "poke refused: this console has no write path"); return; }
    if (!ch->host.write(ch->host.ctx, addr, width, value))
    { Out(ch, true, "poke 0x%08X refused: not writable on this console", addr); return; }
    uint32_t back = 0;
    if (ch->host.read && ch->host.read(ch->host.ctx, addr, width, &back))
        Out(ch, true, "poke 0x%08X = 0x%0*X, reads back 0x%0*X", addr, width * 2, value, width * 2, back);
    else
        Out(ch, true, "poke 0x%08X = 0x%0*X, could not read back", addr, width * 2, value);
    // A poke on a watched address is not a change the game made: resync so it is not
    // reported as one.
    for (Watch& wt : ch->watches)
        if (wt.addr == addr && ch->host.read) ch->host.read(ch->host.ctx, wt.addr, wt.width, &wt.last);
}

void CmdWatch(DebugChannel* ch, const std::vector<std::string>& w)
{
    uint32_t addr = 0;
    int width = 1;
    if (w.size() < 2 || !ParseAddr(w[1], &addr)) { Out(ch, true, "watch needs an address"); return; }
    if (w.size() >= 3 && !ParseWidth(w[2], &width)) { Out(ch, true, "watch: width is 1, 2 or 4"); return; }
    uint32_t v = 0;
    if (!ch->host.read || !ch->host.read(ch->host.ctx, addr, width, &v))
    { Out(ch, true, "watch refused: 0x%08X is not readable on this console", addr); return; }
    for (Watch& wt : ch->watches)
        if (wt.addr == addr)
        {
            // Watching again re-arms a muted watch and may change its width.
            wt.width = width; wt.last = v; wt.run = 0; wt.muted = false;
            Out(ch, true, "watch 0x%08X = 0x%0*X (re-armed)", addr, width * 2, v);
            return;
        }
    if ((int) ch->watches.size() >= kDebugMaxWatches)
    { Out(ch, true, "watch refused: %d watches already", kDebugMaxWatches); return; }
    ch->watches.push_back(Watch{ addr, width, v, 0, false });
    Out(ch, true, "watch 0x%08X = 0x%0*X", addr, width * 2, v);
}

void CmdUnwatch(DebugChannel* ch, const std::vector<std::string>& w)
{
    if (w.size() >= 2 && Lower(w[1]) == "all")
    {
        Out(ch, true, "unwatched %d", (int) ch->watches.size());
        ch->watches.clear();
        return;
    }
    uint32_t addr = 0;
    if (w.size() < 2 || !ParseAddr(w[1], &addr)) { Out(ch, true, "unwatch needs an address or all"); return; }
    for (size_t i = 0; i < ch->watches.size(); i++)
        if (ch->watches[i].addr == addr)
        {
            ch->watches.erase(ch->watches.begin() + (long) i);
            Out(ch, true, "unwatched 0x%08X", addr);
            return;
        }
    Out(ch, true, "unwatch: 0x%08X was not watched", addr);
}

void CmdPad(DebugChannel* ch, const std::vector<std::string>& w)
{
    uint32_t mask = 0;
    int frames = kDebugDefaultPad;
    if (w.size() < 2 || !ParseButtons(w[1], &mask))
    { Out(ch, true, "pad: unknown button in '%s'", w.size() >= 2 ? w[1].c_str() : ""); return; }
    if (w.size() >= 3 && (!ParseFrames(w[2], &frames) || frames <= 0))
    { Out(ch, true, "pad: duration is frames, or ms like 300ms"); return; }
    if (frames > kDebugMaxPadFrames)
    { Out(ch, true, "pad: at most %d frames", kDebugMaxPadFrames); return; }
    if (!ch->host.set_button) { Out(ch, true, "pad refused: no pad on this console"); return; }
    for (int b = 0; b < 12; b++)
        if (mask & (1u << b)) ch->host.set_button(ch->host.ctx, b, true);
    ch->held = mask;
    ch->release_at = ch->frame + (uint64_t) frames;
    // One released frame after the hold before the next line runs, so `pad a` twice is two
    // presses the game can see, not one long one.
#ifndef SABOTAGE_NO_RELEASE_GAP
    ch->busy_until = ch->release_at + 1;
#else
    ch->busy_until = ch->release_at;
#endif
    Out(ch, false, "pad %s for %d frames (frame %llu)", ButtonList(mask).c_str(), frames,
        (unsigned long long) ch->frame);
}

void CmdWait(DebugChannel* ch, const std::vector<std::string>& w)
{
    int frames = 0;
    if (w.size() < 2 || !ParseFrames(w[1], &frames) || frames <= 0 || frames > kDebugMaxPadFrames)
    { Out(ch, true, "wait: 1 to %d frames, or ms like 500ms", kDebugMaxPadFrames); return; }
    ch->busy_until = std::max(ch->busy_until, ch->frame + (uint64_t) frames);
}

void CmdCount(DebugChannel* ch, const std::vector<std::string>& w)
{
    if (w.size() >= 2 && Lower(w[1]) == "dump")
    {
        std::vector<const Hook*> c;
        for (const Hook& h : ch->hooks) if (h.counting) c.push_back(&h);
        if (c.empty()) { Out(ch, true, "count: nothing counted"); return; }
        std::stable_sort(c.begin(), c.end(), [](const Hook* a, const Hook* b) { return a->count > b->count; });
        Out(ch, true, "count at frame %llu, %d addresses, most called first",
            (unsigned long long) ch->frame, (int) c.size());
        for (const Hook* h : c)
            Out(ch, false, "  0x%08X %llu", h->addr, (unsigned long long) h->count);
        return;
    }
    if (w.size() >= 2 && Lower(w[1]) == "reset")
    {
        for (Hook& h : ch->hooks) h.count = 0;
        Out(ch, true, "count reset");
        return;
    }
    if (w.size() < 2) { Out(ch, true, "count needs an address, dump or reset"); return; }
    for (size_t i = 1; i < w.size(); i++)
    {
        uint32_t addr = 0;
        if (!ParseAddr(w[i], &addr)) { Out(ch, true, "count: '%s' is not an address", w[i].c_str()); return; }
        Hook* h = ArmHook(ch, addr, "count");
        if (!h) return;
        h->counting = true;
        Out(ch, false, "counting 0x%08X", addr);
    }
}

void CmdUncount(DebugChannel* ch, const std::vector<std::string>& w)
{
    if (w.size() >= 2 && Lower(w[1]) == "all")
    {
        std::vector<uint32_t> addrs;
        for (Hook& h : ch->hooks) { h.counting = false; addrs.push_back(h.addr); }
        for (uint32_t a : addrs) DisarmIfIdle(ch, a);
        Out(ch, true, "uncounted all");
        return;
    }
    uint32_t addr = 0;
    if (w.size() < 2 || !ParseAddr(w[1], &addr)) { Out(ch, true, "uncount needs an address or all"); return; }
    Hook* h = FindHook(ch, addr);
    if (!h || !h->counting) { Out(ch, true, "uncount: 0x%08X was not counted", addr); return; }
    h->counting = false;
    DisarmIfIdle(ch, addr);
    Out(ch, true, "uncounted 0x%08X", addr);
}

void CmdTrace(DebugChannel* ch, const std::vector<std::string>& w)
{
    uint32_t addr = 0, n = 10;
    if (w.size() < 2 || !ParseAddr(w[1], &addr)) { Out(ch, true, "trace needs an address"); return; }
    if (w.size() >= 3 && (!ParseNum(w[2], &n) || n == 0)) { Out(ch, true, "trace: N is a number"); return; }
    if (n > (uint32_t) kDebugMaxTrace) n = kDebugMaxTrace;
    Hook* h = ArmHook(ch, addr, "trace");
    if (!h) return;
    h->trace_left = (int) n;
    Out(ch, false, "tracing the next %u calls of 0x%08X", n, addr);
}

void CmdStatus(DebugChannel* ch)
{
    int counting = 0, tracing = 0;
    for (const Hook& h : ch->hooks) { counting += h.counting; tracing += h.trace_left > 0; }
    Out(ch, true, "frame %llu, pad %s, %d lines waiting, %d watches, %d counted, %d traced, speech %s",
        (unsigned long long) ch->frame, ch->held ? ButtonList(ch->held).c_str() : "free",
        (int) ch->lines.size(), (int) ch->watches.size(), counting, tracing,
        ch->speak ? "on" : "off");
    for (const Watch& wt : ch->watches)
        Out(ch, false, "  watch 0x%08X width %d = 0x%0*X%s", wt.addr, wt.width, wt.width * 2,
            wt.last, wt.muted ? " (muted: changes every frame)" : "");
}

void ClearAll(DebugChannel* ch)
{
    ch->lines.clear();
    ReleasePad(ch);
    ch->busy_until = 0;
    ch->release_at = 0;
    ch->watches.clear();
    DisarmAll(ch);
    ch->finished.clear();
}

void RunLine(DebugChannel* ch, const std::string& raw)
{
    std::string line = raw;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    std::vector<std::string> w = Words(line);
    if (w.empty()) return;
    std::string cmd = Lower(w[0]);
    if      (cmd == "peek" || cmd == "read")   CmdPeek(ch, w);
    else if (cmd == "poke" || cmd == "write")  CmdPoke(ch, w);
    else if (cmd == "watch")                   CmdWatch(ch, w);
    else if (cmd == "unwatch")                 CmdUnwatch(ch, w);
    else if (cmd == "pad")                     CmdPad(ch, w);
    else if (cmd == "wait")                    CmdWait(ch, w);
    else if (cmd == "count")                   CmdCount(ch, w);
    else if (cmd == "uncount")                 CmdUncount(ch, w);
    else if (cmd == "trace")                   CmdTrace(ch, w);
    else if (cmd == "status")                  CmdStatus(ch);
    else if (cmd == "clear")                   { ClearAll(ch); Out(ch, true, "cleared"); }
    else if (cmd == "speak")
    {
        if (w.size() >= 2 && Lower(w[1]) == "on")       { ch->speak = true;  Out(ch, true, "speech on"); }
        else if (w.size() >= 2 && Lower(w[1]) == "off") { Out(ch, true, "speech off"); ch->speak = false; }
        else Out(ch, true, "speak on or speak off");
    }
    else if (cmd == "help")
    {
        for (const char* h : kHelp) Out(ch, false, "%s", h);
    }
    else Out(ch, true, "unknown command '%s'; try help", w[0].c_str());
}

void DrainInbox(DebugChannel* ch)
{
    std::string text;
    {
        std::lock_guard<std::mutex> lock(ch->inbox_mu);
        text.swap(ch->inbox);
    }
    size_t i = 0;
    while (i < text.size())
    {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ch->lines.push_back(line);
        i = j + 1;
    }
}

void CheckWatches(DebugChannel* ch)
{
    for (Watch& wt : ch->watches)
    {
        uint32_t v = 0;
        if (!ch->host.read || !ch->host.read(ch->host.ctx, wt.addr, wt.width, &v)) continue;
        if (v == wt.last) { wt.run = 0; continue; }
        uint32_t was = wt.last;
        wt.last = v;
        wt.run++;
        if (wt.muted) continue;
#ifndef SABOTAGE_NO_NOISE_MUTE
        if (wt.run >= kDebugNoisyFrames)
        {
            // A frame counter or an animation timer changes every frame: reporting it would
            // bury every other line, and speaking it would drown the game. Say so once.
            wt.muted = true;
            Out(ch, true, "watch 0x%08X changes every frame; muted. watch it again to re-arm", wt.addr);
            continue;
        }
#endif
        Out(ch, true, "watch 0x%08X 0x%0*X -> 0x%0*X frame %llu", wt.addr, wt.width * 2, was,
            wt.width * 2, v, (unsigned long long) ch->frame);
    }
}

} // namespace

DebugChannel* debug_create(const DebugHost* host)
{
    DebugChannel* ch = new DebugChannel();
    if (host) ch->host = *host;
    return ch;
}

void debug_destroy(DebugChannel* ch)
{
    if (!ch) return;
    ClearAll(ch);
    delete ch;
}

void debug_submit(DebugChannel* ch, const char* text)
{
    if (!ch || !text || !*text) return;
    std::lock_guard<std::mutex> lock(ch->inbox_mu);
    if (ch->inbox.size() > 65536) return;   // a runaway writer cannot grow this without bound
    ch->inbox += text;
    if (ch->inbox.back() != '\n') ch->inbox += '\n';
}

void debug_frame(DebugChannel* ch, uint64_t frame)
{
    if (!ch) return;
    ch->frame = frame;
    if (ch->held && frame >= ch->release_at) ReleasePad(ch);
    DrainInbox(ch);
    // Watches first, so a change the game made this frame is reported before the lines that
    // run now could poke over it.
    CheckWatches(ch);
    for (uint32_t a : ch->finished) DisarmIfIdle(ch, a);
    ch->finished.clear();
    while (!ch->lines.empty())
    {
#ifndef SABOTAGE_IGNORE_BUSY
        if (frame < ch->busy_until) break;
#endif
        std::string line = ch->lines.front();
        ch->lines.pop_front();
        RunLine(ch, line);
    }
}

void debug_exec_hit(DebugChannel* ch, uint32_t pc, const uint32_t* args, int nargs)
{
    if (!ch) return;
    Hook* h = FindHook(ch, pc);
    if (!h) return;
    if (h->counting) h->count++;
    if (h->trace_left > 0)
    {
        char buf[96] = {0};
        int len = 0;
        for (int i = 0; i < nargs && i < kDebugTraceArgs && args; i++)
            len += snprintf(buf + len, sizeof(buf) - (size_t) len, " a%d=0x%08X", i, args[i]);
        Out(ch, false, "trace 0x%08X frame %llu%s", pc, (unsigned long long) ch->frame, buf);
        h->trace_left--;
        if (h->trace_left == 0)
        {
            Out(ch, true, "trace 0x%08X done", pc);
            // NOT disarmed here: this runs inside the console's own breakpoint callback, and
            // removing a breakpoint from inside its hit is the kind of re-entry an emulator's
            // debugger is not written for. debug_frame disarms it, between frames.
            ch->finished.push_back(pc);
        }
    }
}

void debug_reset(DebugChannel* ch)
{
    if (!ch) return;
    {
        std::lock_guard<std::mutex> lock(ch->inbox_mu);
        ch->inbox.clear();
    }
    ClearAll(ch);
    ch->frame = 0;
}

int debug_pending(const DebugChannel* ch)
{
    return ch ? (int) ch->lines.size() : 0;
}

bool debug_busy(const DebugChannel* ch)
{
    return ch && (ch->held != 0 || ch->frame < ch->busy_until);
}

} // namespace oga

#endif // OGA_DEBUG_CHANNEL_IMPLEMENTED
#endif // OGA_DEBUG_CHANNEL_IMPLEMENTATION
