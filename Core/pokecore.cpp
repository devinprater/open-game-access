/*
    pokecore.cpp — the iOS-side app glue for melonDS + the Lua accessibility layer.

    This is the equivalent of melonDS's Qt frontend, reduced to what a
    screen-reader player needs:

      * one NDS instance, software renderer, direct boot from a ROM file
      * a frame loop driven by the app's display link (one frame per call)
      * the Lua script engine, running main.lua in a coroutine and resuming it
        once per emulated frame from _Update()
      * input: DS buttons and touch from Swift, plus the script's hotkeys
      * speech out through a callback the Swift layer owns
      * melonDS savestates for quick save/load

    Nothing here knows about SwiftUI, and nothing above it knows about melonDS.
*/
#include "pokecore.h"

#include "NDS.h"
#include "NDSCart.h"
#include "SPU.h"
#include "adapter.h"
#include "announce.h"
#include "Savestate.h"
#include "Platform.h"
#include "gba_core.h"
#include "psp_core.h"
#include "mesen_core.h"

// The GBA adapter's symbols (defined in gba_adapter.cpp). A GBA ROM selects
// its native reader the same way an NDS ROM is matched from the registry —
// except the registry cannot match it (its game_code is empty by design: it
// matches by the code the host hands over at load, plus GB/GBC by platform),
// so the GBA load path below names it directly.
namespace oga {
extern const Adapter kGameBoyAdvance;
extern const Adapter kNintendoEntertainmentSystem;
void gba_set_game_code(const char* code);
}

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace melonDS;

// Implemented in poke_platform.cpp.
#include "poke_internal.h"

// The one backend interface (Core/oga_core.h). Every console below is reached
// through it, so adding a console does not mean editing every entry point.
#include "oga_core.h"

// Forward: defined beside HostSpeak below; used by the Lua bindings and
// lifecycle functions above it.
static uint64_t CoreNowMs(void);
static void QueueSpeak(void* ctx, const char* utf8, bool interrupt, uint32_t id);

struct PokeCore {
    std::unique_ptr<NDS> nds;
    char error[512] = {0};
    std::string savePath;

    // ---- the live backend ----
    //
    // WHICH emulator is running this ROM, as one vtable (Core/oga_core.h).
    //
    // This replaced two nullable pointers plus two booleans (isGba/isPsp) that
    // ~100 dispatch sites had to agree about — the arrangement that let a .gba
    // reach melonDS if one site forgot a check. Now there is one value, set
    // once at load and never changed, and every entry point goes through it.
    //
    // `backend.state` is the live core object (GbaCore*/PspCore*/NDS*) and
    // `backend.ops` is the static table for its console. Both are null when no
    // ROM is loaded. The NDS ops live in this file, below, because they need
    // PokeCore's private type.
    OgaCore backend = { nullptr, nullptr };

    // Live backend objects, owned here for lifetime. The ops tables do not own
    // them: `backend.state` just points at one of these.
    GbaCore* gba = nullptr;
    PspCore* psp = nullptr;
    // The NES (MesenCE). Same ownership rule as the others: created here, destroyed here, and
    // pointed at by backend.state. It is the one backend whose console is reached through an
    // adapter that reads live RAM rather than a Lua reader.
    NesCore* nes = nullptr;
    // PSP runtime assets (compat.ini, soft-GPU atlas, VFPU LUTs) ship inside
    // the app bundle; the app points the core at them before loading a PSP
    // ROM. Empty means "use the PPSSPP_ASSETS env var or ./ppsspp-assets",
    // which is what the host proof and a bare checkout rely on.
    std::string pspAssetDir;

    // ---- script engine ----
    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;
    bool scriptLoaded = false;
    std::string script;
    int frameCounter = 0;

    // ---- game adapter ----
    //
    // Which accessibility adapter (if any) owns this ROM. Selected once at load
    // time from the ROM's game code and driven by poke_command() below. A null
    // adapter is the normal case: it means no native reader exists for this game,
    // and the Lua script is the only accessibility layer.
    const oga::Adapter* adapter = nullptr;
    bool adapterAttached = false;
    // ROM game ID: 4 chars for NDS (header 0x0C), up to 9+ for PSP (PARAM.SFO,
    // e.g. ULUS10437). 16 covers both with room.
    char gameCode[16] = {0};
    // The Host handed to the adapter. Stored in the core so its address stays
    // valid for as long as the adapter holds it.
    oga::Host host = {};

    // ---- announcement queue ----
    // One queue per core (docs/design/announcement-queue.md). The sink forwards
    // to the platform with its id (EmitSpeech); done-reports arrive via poke_announce_done() from
    // any thread. Until a platform reports done, pacing is by estimate
    // (host_reports_done=false), which is safe on both platforms.
    oga::AnnounceQueue* announceQ = nullptr;
    // Recent (id, text) pairs for the legacy poke_announce_id_for_text() lookup.
    // Hosts that set the id callback get the id directly and never need this.
    // Full queue text length, so a long line still matches exactly.
    static constexpr int kAnnounceHist = 8;
    uint32_t annIds[kAnnounceHist] = {0};
    char annTexts[kAnnounceHist][oga::kAnnounceTextMax] = {{0}};
    int annNext = 0;

    // ---- input ----
    // DS keys are active-low in hardware: a set bit means "not pressed", and
    // NDS::SetKeyMask takes that same inverted mask.
    uint32_t buttonsDown = 0;
    bool touchDown = false;
    uint16_t touchX = 0;
    uint16_t touchY = 0;
    // Hotkey letters, as main.lua's input.get() expects them (one-char strings).
    std::vector<char> hotkeysDown;

    // ---- joypad.set{}: the per-frame button OVERRIDE ----
    //
    // ⛔ THIS IS NOT COSMETIC. main.lua's controller-mod layer (hold the left
    // trigger -> the pad drives the accessibility list instead of the game) works
    // by writing joypad.set{ EveryButton=false } each frame, so the game receives
    // no input at all while the layer is held. BizHawk and DeSmuME both support
    // it; melonDS-lua never did, which is why it was a no-op in the Android port
    // and the whole layer was dead on mobile.
    //
    // `overrides` holds only the buttons the script named, and it is CLEARED at
    // the end of every emulated frame — the script re-asserts what it wants each
    // frame, exactly as it does on BizHawk (which wipes overrides every frame
    // before the script's loop runs).
    uint32_t buttonOverrides = 0;    // bit set = this button's state is overridden
    uint32_t overrideValues = 0;     // bit set = overridden button is PRESSED

    // ---- output ----
    PokeSpeechCallback speechCb = nullptr;
    void* speechUserdata = nullptr;
    // Optional: same contract as speechCb plus the queue utterance id (0 for
    // lines that bypass the queue). When set, it is used instead of speechCb.
    PokeSpeechIdCallback speechIdCb = nullptr;
    void* speechIdUserdata = nullptr;
    PokeLogCallback logCb = nullptr;
    void* logUserdata = nullptr;
    bool audioEnabled = true;

    // ---- display ----
    // The core's framebuffers are 32-bit ARGB, and the app wants RGBA8888, so
    // frames are converted into this staging buffer.
    std::vector<uint8_t> frameRGBA;
    int frameScreen = -1;

    // ---- firmware / BIOS ----
    // Paths to real dumps. Optional: melonDS falls back to FreeBIOS and a
    // generated firmware, but the generated firmware is NOT bootable, which
    // forces NeedsDirectBoot() and skips the menu handshake the game expects.
    // Giving it a real firmware + BIOS lets the console boot the way hardware
    // does, which is what a game that waits on the menu handshake needs.
    std::string bios9Path;
    std::string bios7Path;
    std::string firmwarePath;
    bool hasFirmware = false;

    bool running = false;
    int stopReason = 0;
};

// The DS ops table, DEFINED at the very end of this file: its functions call
// ApplyInput, FlushSave, BuildHost and the savestate code, all of which are
// defined below this point.
//
// ⛔ Reached through a function, not a bare forward declaration. In C a
// namespace-scope `static const OgaCoreOps x;` is a tentative definition; in C++
// it is an ERROR ("uninitialized const"), and it then collides with the real
// definition as a redefinition. A function returning the table sidesteps both
// and keeps the single definition at the end of the file.
static const OgaCoreOps* NdsOpsTable(void);

// Is the live backend melonDS? Derived from the ops pointer rather than a
// boolean, so it cannot disagree with the table the calls actually go through.
static bool IsPokeNds(const PokeCore* core)
{
    return core && core->backend.ops == NdsOpsTable();
}

// The Platform layer signals a console-driven stop (power off, bad exception
// region); this is the single core the app owns, so it is reachable globally.
static PokeCore* gCurrentCore = nullptr;

// --------------------------------------------------------------------- helpers

static void SetError(PokeCore* core, const char* fmt, ...)
{
    if (!core) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(core->error, sizeof(core->error), fmt, args);
    va_end(args);
}

static void ForwardLog(const char* text)
{
    if (gCurrentCore && gCurrentCore->logCb) gCurrentCore->logCb(text, gCurrentCore->logUserdata);
}

// The Lua engine needs the core it belongs to; it is reachable from any lua_State.
static PokeCore* CoreFromLua(lua_State* L)
{
    lua_pushlightuserdata(L, (void*) &CoreFromLua);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* core = static_cast<PokeCore*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return core;
}

static void RegisterCore(lua_State* L, PokeCore* core)
{
    lua_pushlightuserdata(L, (void*) &CoreFromLua);
    lua_pushlightuserdata(L, core);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

// ------------------------------------------------------------------ Lua: memory
//
// This mirrors melonDS-lua's own `memory` library (LuaMemory.cpp): the same
// domain names, the same argument order (address first, domain last), the same
// function names, so bizhawk_compat.lua and main.lua need no changes at all.

enum class Bus { NotBus, Arm9, Arm7 };

struct MemoryDomain {
    const char* name;
    uint8_t* base;
    uint64_t size;
    Bus bus;
    // The address the domain starts at in the guest's address space. Reads and
    // writes arrive as ADDRESSES (the compat shim sends RAM_BASE + offset, i.e.
    // 0x02000000 + off) but are indexed into `base`, so the start address must be
    // subtracted before the size check and the access. Without this, a Main RAM
    // read of 0x02000000 was compared against a 4 MiB `size` and failed the
    // bounds check, silently returning 0 for EVERY address and every domain —
    // which makes the accessibility script see a blank RAM chip and narrate
    // nothing, however long it runs.
    uint64_t start;
};

static std::vector<MemoryDomain> DomainsFor(PokeCore* core)
{
    std::vector<MemoryDomain> domains;
    NDS* nds = core->nds.get();
    if (!nds) return domains;

    // DS mode maps 4 MiB of Main RAM at 0x02000000; DSi mode maps the full 16 MiB
    // (MainRAMMaxSize). The /4 here was also wrong for DS: MainRAMMaxSize is
    // already 16 MiB, so DS wants the first 4 MiB starting at 0x02000000.
    const bool isDSi = (nds->ConsoleType == 1);
    const uint64_t mainRamSize = isDSi ? nds->MainRAMMaxSize : 0x400000;

    domains.push_back({"Main RAM", nds->MainRAM, mainRamSize, Bus::NotBus, 0x02000000});
    domains.push_back({"Shared WRAM", nds->SharedWRAM, nds->SharedWRAMSize, Bus::NotBus, 0x03000000});
    domains.push_back({"ARM7 WRAM", nds->ARM7WRAM, nds->ARM7WRAMSize, Bus::NotBus, 0x03800000});
    if (auto* cart = nds->GetNDSCart())
    {
        domains.push_back({"SRAM", nds->GetNDSSave(), nds->GetNDSSaveLength(), Bus::NotBus, 0});
        domains.push_back({"ROM", const_cast<uint8_t*>(cart->GetROM()), cart->GetROMLength(), Bus::NotBus, 0});
    }
    domains.push_back({"Instruction TCM", nds->ARM9.ITCM, ITCMPhysicalSize, Bus::NotBus, 0});
    domains.push_back({"Data TCM", nds->ARM9.DTCM, DTCMPhysicalSize, Bus::NotBus, 0});
    domains.push_back({"ARM9 BIOS", const_cast<uint8_t*>(nds->GetARM9BIOS().data()), ARM9BIOSSize, Bus::NotBus, 0xFFFF0000});
    domains.push_back({"ARM7 BIOS", const_cast<uint8_t*>(nds->GetARM7BIOS().data()), ARM7BIOSSize, Bus::NotBus, 0x00000000});
    domains.push_back({"ARM9 System Bus", nullptr, 0x10000000, Bus::Arm9, 0});
    domains.push_back({"ARM7 System Bus", nullptr, 0x10000000, Bus::Arm7, 0});

    // ROM/SRAM/TCM domains have no meaningful guest base (the cart ROM is not
    // linearly mapped), so they default to 0 and are addressed by offset.
    return domains;
}

// Peeking through the system bus must not poke at hardware registers that have
// side effects (key input, touch, IPC FIFOs) — melonDS-lua learned this the
// hard way and guards the same addresses.
static bool SafeToPeek(bool arm9, uint32_t addr)
{
    if (arm9)
    {
        if ((addr & 0xFFFFFF00) == 0x04004200) return false;
        switch (addr)
        {
            case 0x04000130: case 0x04000131:
            case 0x04000600: case 0x04000601:
            case 0x04000602: case 0x04000603:
                return false;
        }
    }
    else
    {
        if (addr >= 0x04800000 && addr <= 0x04810000)
        {
            if (addr & 1) addr--;
            addr &= 0x7FFE;
            if (addr == 0x044 || addr == 0x060) return false;
        }
    }
    return true;
}

static void DomainRead(const MemoryDomain& d, NDS* nds, uint8_t* out, int64_t address, int64_t count)
{
    // `address` is a GUEST ADDRESS (e.g. 0x02000000 for the start of Main RAM).
    // Domain-backed memory (`Bus::NotBus`) is a flat host buffer, so the guest
    // address must be rebased by the domain's start address before indexing.
    while (count-- > 0)
    {
        switch (d.bus)
        {
        case Bus::NotBus:
        {
            const int64_t off = (int64_t) address - (int64_t) d.start;
            if (off >= 0 && (uint64_t) off < d.size && d.base)
                *out = d.base[off];
            else
                *out = 0;
            break;
        }
        case Bus::Arm9:
            if (address < (int64_t) nds->ARM9.ITCMSize)
                *out = nds->ARM9.ITCM[address & (ITCMPhysicalSize - 1)];
            else if ((address & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
                *out = nds->ARM9.DTCM[address & (DTCMPhysicalSize - 1)];
            else
                *out = SafeToPeek(true, (uint32_t) address) ? nds->ARM9Read8((uint32_t) address) : 0;
            break;
        case Bus::Arm7:
            *out = SafeToPeek(false, (uint32_t) address) ? nds->ARM7Read8((uint32_t) address) : 0;
            break;
        }
        address++;
        out++;
    }
}

static void DomainWrite(const MemoryDomain& d, NDS* nds, const uint8_t* in, int64_t address, int64_t count)
{
    // Same rebasing rule as DomainRead: `address` is a guest address.
    while (count-- > 0)
    {
        switch (d.bus)
        {
        case Bus::NotBus:
        {
            const int64_t off = (int64_t) address - (int64_t) d.start;
            if (off >= 0 && (uint64_t) off < d.size && d.base) d.base[off] = *in;
            break;
        }
        case Bus::Arm9:
            if (address < (int64_t) nds->ARM9.ITCMSize)
                nds->ARM9.ITCM[address & (ITCMPhysicalSize - 1)] = *in;
            else if ((address & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
                nds->ARM9.DTCM[address & (DTCMPhysicalSize - 1)] = *in;
            else
                nds->ARM9Write8((uint32_t) address, *in);
            break;
        case Bus::Arm7:
            nds->ARM7Write8((uint32_t) address, *in);
            break;
        }
        address++;
        in++;
    }
}

// Resolves the domain argument, defaulting to Main RAM exactly as melonDS-lua
// does. `argIndex` is the stack position of the optional domain name.
static bool ResolveDomain(PokeCore* core, lua_State* L, int argIndex, MemoryDomain& out)
{
    static std::vector<MemoryDomain> cache;
    cache = DomainsFor(core);
    for (auto& d : cache)
    {
        if (std::strcmp(d.name, "Main RAM") == 0) out = d;
    }
    if (lua_isstring(L, argIndex))
    {
        const char* wanted = lua_tostring(L, argIndex);
        for (auto& d : cache)
        {
            // Accept both "Main RAM" and "MainRAM": the compat shim writes the
            // latter, melonDS-lua's own docs use the former.
            std::string a(d.name), b(wanted);
            auto strip = [](std::string& s) {
                s.erase(std::remove_if(s.begin(), s.end(), [](char c) {
                    return c == ' ' || c == '_';
                }), s.end());
            };
            strip(a); strip(b);
            if (a == b) { out = d; return true; }
        }
        return false;
    }
    return true;
}

template <typename T>
static int LuaRead(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (!core || !core->nds) { lua_pushinteger(L, 0); return 1; }
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    MemoryDomain domain;
    ResolveDomain(core, L, 2, domain);

    int64_t value = 0;
    uint8_t bits[sizeof(int64_t)] = {0};
    // The bounds check must rebase the guest address by the domain's start:
    // comparing 0x02000000 against a 4 MiB `size` failed for Main RAM and made
    // every read silently return 0.
    const int64_t off = (int64_t) address - (int64_t) domain.start;
    if (off >= 0 && (uint64_t) off + sizeof(T) <= domain.size)
        DomainRead(domain, core->nds.get(), bits, address, sizeof(T));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (size_t i = 0; i < sizeof(T) / 2; i++) std::swap(bits[i], bits[sizeof(T) - 1 - i]);
#endif
    std::memcpy(&value, bits, sizeof(T));
    lua_pushinteger(L, (lua_Integer) value);
    return 1;
}

template <typename T>
static int LuaWrite(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (!core || !core->nds) return 0;
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    int64_t value = (int64_t) luaL_checkinteger(L, 2);
    MemoryDomain domain;
    if (!ResolveDomain(core, L, 3, domain)) return 0;
    {   // rebase before the bounds check (see LuaRead)
        const int64_t off = (int64_t) address - (int64_t) domain.start;
        if (off < 0 || (uint64_t) off + sizeof(T) > domain.size) return 0;
    }

    uint8_t bits[sizeof(int64_t)] = {0};
    std::memcpy(bits, &value, sizeof(T));
    DomainWrite(domain, core->nds.get(), bits, address, sizeof(T));
    return 0;
}

static int LuaGetDomainList(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    auto domains = core ? DomainsFor(core) : std::vector<MemoryDomain>{};
    lua_createtable(L, (int) domains.size(), 0);
    for (size_t i = 0; i < domains.size(); i++)
    {
        lua_pushstring(L, domains[i].name);
        lua_seti(L, -2, (lua_Integer) i + 1);
    }
    return 1;
}

// ------------------------------------------------------------------- Lua: input
//
// melonDS-lua's input library, reduced to what the accessibility shim uses.
// Hotkeys arrive as single letters; the shim's input.get() passes them through.

static int LuaHeldKeys(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    lua_createtable(L, 0, core ? (int) core->hotkeysDown.size() : 0);
    if (core)
    {
        for (char key : core->hotkeysDown)
        {
            // melonDS-lua keys the table by Qt keycode; the shim's translator
            // maps those to letters. Sending the ASCII code directly means the
            // same table shape arrives at input.get().
            lua_pushboolean(L, 1);
            lua_seti(L, -2, (lua_Integer) (unsigned char) key);
        }
    }
    return 1;
}

static int LuaGetJoy(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    lua_createtable(L, 0, 12);
    // melonDS-lua reports the pad as a name->bool table; main.lua's controller
    // mod layer asks for the core's button list here and uses it to block
    // every button while its modifier is held. Reporting the real DS buttons
    // (and their true state) is what makes that layer behave on iOS too.
    static const char* names[] = {"A","B","Select","Start","Right","Left","Up","Down","R","L","X","Y"};
    for (int i = 0; i < 12; i++)
    {
        bool down = core && (core->buttonsDown & (1u << i));
        lua_pushboolean(L, down);
        lua_setfield(L, -2, names[i]);
    }
    return 1;
}

// joypad.set{table} — the per-frame button override (see the PokeCore field).
//
// ⛔ THE ARGUMENT ORDER OF THE OVERLOAD MATTERS: an EMPTY table is not "no
// change", it is "clear every override I set", and the script depends on that.
// main.lua's pad_step calls joypad.set({}) to drop its overrides and re-latch
// from the physical pad before it asks joypad.getimmediate(), which is how it
// learns what is REALLY held. Treating {} as a no-op makes the release latch
// never engage and the layer leaks presses into the game.
//
// Button names follow melonDS-lua's / the DS layout, which is also the order in
// POKE_BTN_*: A,B,Select,Start,Right,Left,Up,Down,R,L,X,Y. A script may also
// pass the name it got from joypad.getimmediate() — the same set.
static int LuaJoySet(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (!core) return 0;

    // Every call REPLACES this frame's override set, matching BizHawk, where the
    // override table is rebuilt per frame rather than accumulated.
    core->buttonOverrides = 0;
    core->overrideValues = 0;

    if (lua_gettop(L) < 1 || !lua_istable(L, 1)) return 0;

    static const char* names[] = {"A","B","Select","Start","Right","Left","Up","Down","R","L","X","Y"};
    for (int i = 0; i < 12; i++)
    {
        lua_getfield(L, 1, names[i]);
        if (!lua_isnil(L, -1))
        {
            core->buttonOverrides |= (1u << i);
            if (lua_toboolean(L, -1)) core->overrideValues |= (1u << i);
        }
        lua_pop(L, 1);
    }
    // main.lua also names the touch panel here ("Touch X"/"Touch Y" as analog
    // axes); those are strings in its table and are handled by the touch path,
    // so they are ignored rather than misread as buttons.
    return 0;
}

static int LuaNDSTapDown(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (core)
    {
        core->touchX = (uint16_t) luaL_checkinteger(L, 1);
        core->touchY = (uint16_t) luaL_checkinteger(L, 2);
        core->touchDown = true;
    }
    return 0;
}

static int LuaNDSTapUp(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (core) core->touchDown = false;
    return 0;
}

// -------------------------------------------------------------------- Lua: gui
//
// The script's overlay canvas API. The accessibility tool draws nothing —
// everything it produces is speech — so these are accepted and ignored, which
// keeps a script that calls gui.* from erroring out and killing the loop.

static int LuaNoop(lua_State* L) { (void) L; return 0; }
static int LuaMakeCanvas(lua_State* L) { lua_pushinteger(L, 1); return 1; }

// ------------------------------------------------------------------- Lua: misc

static int LuaPrint(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; i++)
    {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s) { out.append(s, len); lua_pop(L, 1); }
    }
    if (core && core->logCb) core->logCb(out.c_str(), core->logUserdata);
    return 0;
}

// Every line the player hears leaves the core here. NULL text means "stop".
// `id` is the announcement-queue utterance id, 0 for lines outside the queue.
static void EmitSpeech(PokeCore* core, const char* utf8, bool interrupt, uint32_t id)
{
    if (!core) return;
    if (core->speechIdCb)
        core->speechIdCb(utf8, interrupt, id, core->speechIdUserdata);
    else if (core->speechCb)
        core->speechCb(utf8, interrupt, core->speechUserdata);
}

// speech.say / speech.stop — the one channel the player actually hears. The
// compat shim prefers a global named `hermes_tts` (kept from the Android port
// so the same shim text works on both platforms); this provides it.
static int LuaSpeak(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    const char* text = luaL_checkstring(L, 1);
    bool interrupt = true;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2))
    {
        const char* mode = lua_tostring(L, 2);
        interrupt = !(mode && std::strcmp(mode, "queue") == 0);
    }
    if (text) EmitSpeech(core, text, interrupt, 0);
    return 0;
}

// speech.stop — the script's "stop talking now" (its R key). It MUST reach the
// platform: the callback's contract is that a NULL text means stop, and without
// forwarding that, the on-screen "Stop speech" button and the R key do nothing
// on iOS while working fine on Android. Speech has no other way to be stopped.
static int LuaStopSpeech(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (core && core->announceQ) oga::announce_stop(core->announceQ, CoreNowMs());
    EmitSpeech(core, nullptr, true, 0);
    return 0;
}

static const luaL_Reg kMemoryFuncs[] = {
    {"read_u8",      LuaRead<uint8_t>},
    {"readbyte",     LuaRead<uint8_t>},
    {"read_u16_le",  LuaRead<uint16_t>},
    {"read_u16_be",  LuaRead<uint16_t>},
    {"read_u32_le",  LuaRead<uint32_t>},
    {"read_u32_be",  LuaRead<uint32_t>},
    {"read_s8",      LuaRead<int8_t>},
    {"read_s16_le",  LuaRead<int16_t>},
    {"read_s32_le",  LuaRead<int32_t>},
    {"write_u8",     LuaWrite<uint8_t>},
    {"write_u16_le", LuaWrite<uint16_t>},
    {"write_u32_le", LuaWrite<uint32_t>},
    {"getmemorydomainlist", LuaGetDomainList},
    {nullptr, nullptr}
};

static const luaL_Reg kInputFuncs[] = {
    {"HeldKeys",   LuaHeldKeys},
    {"GetJoy",     LuaGetJoy},
    {"JoySet",     LuaJoySet},
    {"NDSTapDown", LuaNDSTapDown},
    {"NDSTapUp",   LuaNDSTapUp},
    {"Keys",       LuaHeldKeys},
    {nullptr, nullptr}
};

static const luaL_Reg kGuiFuncs[] = {
    {"MakeCanvas",   LuaMakeCanvas},
    {"SetCanvas",    LuaNoop},
    {"ClearOverlay", LuaNoop},
    {"Flip",         LuaNoop},
    {"drawText",     LuaNoop},
    {"drawLine",     LuaNoop},
    {"Rect",         LuaNoop},
    {"FillRect",     LuaNoop},
    {"Ellipse",      LuaNoop},
    {nullptr, nullptr}
};

// -------------------------------------------------------------------- plumbing

static void FlushSave(PokeCore* core)
{
    if (!core || !core->nds || core->savePath.empty()) return;
    const uint8_t* saveMem = core->nds->GetNDSSave();
    uint32_t saveLen = core->nds->GetNDSSaveLength();
    if (!saveMem || !saveLen) return;
    FILE* f = fopen(core->savePath.c_str(), "wb");
    if (!f) return;
    fwrite(saveMem, 1, saveLen, f);
    fclose(f);
}

void poke_flush_save(PokeCore* core) { FlushSave(core); }

/// Called from the Platform layer when the emulated console stops itself.
void poke_internal_signal_stop(int stopReason)
{
    if (!gCurrentCore) return;
    gCurrentCore->stopReason = stopReason;
    gCurrentCore->running = false;
}

// ------------------------------------------------------------------ lifecycle

PokeCore* poke_create(void)
{
    auto* core = new PokeCore();
    Platform::PokeSetLogForward(ForwardLog);
    oga::AnnounceSink sink{};
    sink.speak = QueueSpeak;
    sink.log = nullptr;
    sink.ctx = core;
    oga::AnnounceConfig cfg{};
    cfg.host_reports_done = false;  // estimate pacing until completion hooks land
    cfg.diag_verbose = false;
    core->announceQ = oga::announce_create(sink, cfg);
    // A core exists before any ROM does and is a DS core until a ROM says
    // otherwise.
    //
    // ⛔ `state` IS the core for the DS: its ops are free functions that cast the
    // state straight back to PokeCore*, because the melonDS NDS / Lua state /
    // adapter registry all live there. Leaving it NULL (which compiles fine and
    // looks safe) makes every entry point refuse, and the symptom is a console
    // that "stops at frame 0" with an empty framebuffer — not a NULL crash.
    core->backend.ops = NdsOpsTable();
    core->backend.state = core;
    gCurrentCore = core;
    return core;
}

void poke_destroy(PokeCore* core)
{
    if (!core) return;
    // Detach first: an adapter holds function pointers and a ctx pointing at THIS
    // core, so leaving it attached after free is a dangling callback the next
    // frame would call into freed memory.
    if (core->adapter && core->adapterAttached && core->adapter->detach)
        core->adapter->detach();
    oga::announce_destroy(core->announceQ);
    core->announceQ = nullptr;
    if (core->nds) core->nds->Stop();
    if (core->L) lua_close(core->L);
    if (core->gba) { gba_destroy(core->gba); core->gba = nullptr; }
    if (core->psp) { psp_destroy(core->psp); core->psp = nullptr; }
    if (core->nes) { nes_destroy(core->nes); core->nes = nullptr; }
    core->backend.ops = nullptr;
    core->backend.state = nullptr;
    if (gCurrentCore == core) gCurrentCore = nullptr;
    delete core;
}

void poke_set_speech_callback(PokeCore* core, PokeSpeechCallback cb, void* userdata)
{
    if (!core) return;
    core->speechCb = cb;
    core->speechUserdata = userdata;
}

void poke_set_speech_id_callback(PokeCore* core, PokeSpeechIdCallback cb, void* userdata)
{
    if (!core) return;
    core->speechIdCb = cb;
    core->speechIdUserdata = userdata;
}

void poke_set_log_callback(PokeCore* core, PokeLogCallback cb, void* userdata)
{
    if (!core) return;
    core->logCb = cb;
    core->logUserdata = userdata;
}

const char* poke_last_error(PokeCore* core) { return core ? core->error : "no core"; }

// ------------------------------------------------------------------- adapters
//
// The Host an adapter receives. Reads go through the same helpers the Lua
// bindings use, so an adapter cannot reach past Main RAM or fault the app on a
// bad pointer — the bounds check lives in one place rather than being re-derived
// per adapter.
// A scalar read through Main RAM, using the same domain table the Lua memory
// bindings use. Keeping one path means an adapter and a script cannot disagree
// about what an address contains, and the bus-side-effect guard applies to both.
static bool AdapterReadScalar(PokeCore* core, uint32_t addr, int width, uint32_t* out)
{
    *out = 0;
    if (!core) return false;
    if (core->backend.ops && !IsPokeNds(core))
    {
        // Console addresses go straight to that console's bus — the same read
        // path its reader uses, so an adapter and a script cannot disagree
        // about what an address contains. `false` (not 0) is the answer for an
        // address outside the map: an adapter must not narrate a zero it
        // invented.
        return core->backend.ops->read(core->backend.state, addr, width, out);
    }
    if (!core->nds) return false;
    uint8_t bytes[4] = {0};
    int n = 0;
    const auto& domains = DomainsFor(core);
    for (const MemoryDomain& d : domains)
    {
        if (addr < d.start) continue;
        const uint64_t off = (uint64_t) addr - d.start;
        if (off >= d.size) continue;
        // Side-effecting hardware (key input, touch, IPC FIFOs) must not be
        // poked through the bus: reading them MUTATES state. SafeToPeek is the
        // same guard the Lua bindings use, so an adapter and a script cannot
        // disagree about which addresses are safe.
        if (d.bus != Bus::NotBus && !SafeToPeek(d.bus == Bus::Arm9, addr)) return false;
        DomainRead(d, core->nds.get(), bytes, addr, width);
        n = width;
        break;
    }
    if (n == 0) return false;          // outside every domain: 0, and say so
    uint32_t v = 0;
    for (int i = 0; i < width; i++) v |= ((uint32_t) bytes[i]) << (8 * i);
    *out = v;
    return true;
}

static uint8_t  HostRead8 (void* ctx, uint32_t a) { uint32_t v=0; AdapterReadScalar((PokeCore*)ctx, a, 1, &v); return (uint8_t) v; }
static uint16_t HostRead16(void* ctx, uint32_t a) { uint32_t v=0; AdapterReadScalar((PokeCore*)ctx, a, 2, &v); return (uint16_t) v; }
static uint32_t HostRead32(void* ctx, uint32_t a) { uint32_t v=0; AdapterReadScalar((PokeCore*)ctx, a, 4, &v); return v; }

static uint64_t CoreNowMs(void)
{
    using namespace std::chrono;
    return (uint64_t) duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// AnnounceSink::speak for the per-core queue: hands the line and its id to the
// platform, and remembers (id, text) for the legacy poke_announce_id_for_text().
static void QueueSpeak(void* ctx, const char* utf8, bool interrupt, uint32_t id)
{
    PokeCore* core = (PokeCore*) ctx;
    if (!core || !utf8) return;
    int slot = core->annNext % PokeCore::kAnnounceHist;
    core->annNext++;
    core->annIds[slot] = id;
    snprintf(core->annTexts[slot], sizeof(core->annTexts[slot]), "%s", utf8);
    EmitSpeech(core, utf8, interrupt, id);
}

static void HostSpeak(void* ctx, const char* utf8, bool interrupt)
{
    PokeCore* core = (PokeCore*) ctx;
    if (utf8) EmitSpeech(core, utf8, interrupt, 0);
}

static void HostLog(void* ctx, const char* utf8)
{
    PokeCore* core = (PokeCore*) ctx;
    if (core->logCb && utf8) core->logCb(utf8, core->logUserdata);
}

static void HostSetButton(void* ctx, int ds_button, bool down)
{
    poke_set_button((PokeCore*) ctx, ds_button, down);
}

// One Host per core, held in the core so its lifetime matches.
static void BuildHost(PokeCore* core)
{
    core->host.read8      = HostRead8;
    core->host.read16     = HostRead16;
    core->host.read32     = HostRead32;
    core->host.speak      = HostSpeak;
    core->host.now_ms     = 0;
    core->host.announce_q = core->announceQ;
    core->host.log        = HostLog;
    core->host.set_button = HostSetButton;
    core->host.ctx        = core;
}

const char *poke_adapter_id(PokeCore *core)
{
    return (core && core->adapter) ? core->adapter->id : nullptr;
}

const char *poke_adapter_name(PokeCore *core)
{
    return (core && core->adapter) ? core->adapter->display_name : nullptr;
}

const char *poke_game_code(PokeCore *core)
{
    return (core && core->gameCode[0]) ? core->gameCode : "";
}

const char *poke_reader_set(PokeCore *core)
{
    /* Routed through the ops table like every other backend capability, so a console that has no
     * bundled reader (the DS builds its script in) simply answers "" without a per-console check
     * here. Read AFTER a ROM is loaded -- the answer is a property of the cartridge, and before
     * load there is no cartridge. */
    if (!core || !core->backend.ops || !core->backend.ops->reader_set) return "";
    const char* s = core->backend.ops->reader_set(core->backend.state);
    return s ? s : "";
}

bool poke_adapter_ready(PokeCore *core)
{
    if (!core || !core->adapter) return false;
    // Attach here as well as on first command. The UI offers the buttons only
    // when ready() is true, so attaching solely inside poke_command deadlocks:
    // no buttons, no command, no attach, ever — "reader loading" forever.
    // Attaching only wires callbacks (and refuses when the console does not
    // exist yet); ready() below still guards against uninitialised game state.
    if (!core->adapterAttached)
    {
        BuildHost(core);
        if (!core->adapter->attach || !core->adapter->attach(&core->host))
            return false;
        core->adapterAttached = true;
    }
    return core->adapter->ready ? core->adapter->ready() : true;
}

void poke_announce_done(PokeCore* core, uint32_t utterance_id, int success)
{
    if (!core) return;
    oga::announce_speech_done(core->announceQ, utterance_id, success != 0);
}

uint32_t poke_announce_id_for_text(PokeCore* core, const char* utf8_text)
{
    if (!core || !utf8_text) return 0;
    // Newest first: a repeated line maps to its latest utterance.
    for (int n = 0; n < PokeCore::kAnnounceHist; n++) {
        int slot = (core->annNext - 1 - n) % PokeCore::kAnnounceHist;
        if (slot < 0) slot += PokeCore::kAnnounceHist;
        if (core->annIds[slot] != 0 &&
            std::strcmp(core->annTexts[slot], utf8_text) == 0)
            return core->annIds[slot];
    }
    return 0;
}

/* Per-frame battle snapshot for the host cue synth. See adapter.h CueSnapshot.
 *
 * Returns 0 when there is nothing to cue (no ROM, no adapter, a backend without cue data,
 * the adapter not attached yet, or simply no battle). Otherwise:
 *   1 = in battle, no lock          2 = locked on the enemy
 *   3 = locked on the EX core
 * and *dist is the distance to the locked target in world units (0 when not locked).
 *
 * ⛔ DELIBERATELY DOES NOT ATTACH. This is called every frame, long before the player has
 * asked the reader anything; attaching here would build the adapter's host against a RAM
 * image whose structures do not exist yet — the very failure the lazy attach in
 * poke_command() exists to avoid. The cue therefore starts on the first command, which is
 * when play actually begins.
 *
 * Speech-free and side-effect-free: safe to poll at frame rate.
 */
int poke_cue_snapshot(PokeCore* core, float* dist)
{
    if (dist) *dist = 0.0f;
    if (!core || !core->adapter || !core->adapterAttached) return 0;
    if (!core->adapter->cue_snapshot) return 0;   /* backend has no cue data */
    oga::CueSnapshot s{};
    if (!core->adapter->cue_snapshot(&s) || !s.battle) return 0;
    if (dist) *dist = s.dist;
    // 4 = a one-frame EX-gain pulse (an absorbed EX Force / EX Core). It is reported even
    // with the lock off, because the pickup is independent of what is targeted.
    if (s.core_gain) return 4;
    if (!s.locked) return 1;
    return s.is_core ? 3 : 2;
}

bool poke_command(PokeCore *core, int cmd)
{
    if (!core || !core->adapter) return false;
    // ⛔ WIDENED IN THE SAME COMMIT THAT APPENDED THE COMMANDS. A stale upper bound
    // silently rejects a new command with no speech and no error.
    if (cmd < 0 ||
        cmd > (int) oga::Command::RepeatOlder) return false;
    if (cmd == (int) oga::Command::StopSpeech)
    {
        // Player stop key: clear queued lines and stop the platform voice.
        // Works without attaching: silence must not depend on game state.
        if (core->announceQ) oga::announce_stop(core->announceQ, CoreNowMs());
        EmitSpeech(core, nullptr, true, 0);
        return true;
    }
    if (cmd == (int) oga::Command::RepeatNewest ||
        cmd == (int) oga::Command::RepeatOlder)
    {
        // The spoken-history walk. Core-internal like StopSpeech: it needs the queue,
        // not an adapter, so it works on the Lua script and on a game with no adapter —
        // both speak through this queue's sink. Refusal is silent: the UI says
        // "Nothing to repeat." itself, the same way it does for the script path.
        if (!core->announceQ) return false;
        bool newest = cmd == (int) oga::Command::RepeatNewest;
        return newest ? oga::announce_repeat_newest(core->announceQ, CoreNowMs())
                      : oga::announce_repeat_older(core->announceQ, CoreNowMs());
    }
    if (!core->adapter->command) return false;

    // Attach lazily, on the first command rather than at ROM load.
    //
    // WHY LAZY: an adapter's reads are only meaningful once the game has
    // allocated its own structures, which happens some way into boot. Attaching
    // at load time would hand it a RAM image with none of its objects in it.
    // Attaching here means the first time the player asks a question is also the
    // first time the adapter has to be correct.
    if (!core->adapterAttached)
    {
        BuildHost(core);
        if (!core->adapter->attach || !core->adapter->attach(&core->host))
            return false;
        core->adapterAttached = true;
    }

    // Refuse rather than narrate when there is nothing true to say. Saying "not
    // on a map yet" is the adapter's own job; reporting from uninitialised memory
    // is nobody's.
    if (core->adapter->ready && !core->adapter->ready()) return false;

    core->host.now_ms = CoreNowMs();
    core->adapter->command((oga::Command) cmd);
    return true;
}

int poke_command_button(PokeCore *core, int cmd)
{
    (void) core; (void) cmd;
    // Adapters drive the game through the host's set_button when they need to.
    // None of Fire Emblem's map queries has a game button of its own, so there is
    // no mapping to report — and -1 is the honest answer. The UI must not present
    // a control as "the game's own button" until one is actually mapped.
    return -1;
}

const char* poke_version(void) { return "melonDS 1.1 + Lua accessibility"; }

// ---------------------------------------------------------------- script setup

static void InstallLibraries(PokeCore* core)
{
    lua_State* L = core->L;
    RegisterCore(L, core);

    // Replace `print` with the console bridge so the script's dev trail reaches
    // the app's reading log instead of a stdout nobody sees.
    lua_pushcfunction(L, LuaPrint);
    lua_setglobal(L, "print");

    auto makeLibrary = [&](const char* name, const luaL_Reg* funcs) {
        luaL_newlib(L, funcs);
        lua_setglobal(L, name);
    };
    makeLibrary("memory", kMemoryFuncs);
    makeLibrary("input", kInputFuncs);
    makeLibrary("gui", kGuiFuncs);

    // hermes_tts: what the compat shim's speech.say calls.
    lua_newtable(L);
    lua_pushcfunction(L, LuaSpeak);
    lua_setfield(L, -2, "speak");
    lua_pushcfunction(L, LuaStopSpeech);
    lua_setfield(L, -2, "stop");
    lua_setglobal(L, "hermes_tts");

    // memory.read_bytes_as_array / write_bytes_as_array are used by main.lua.
    lua_getglobal(L, "memory");
    lua_pushcfunction(L, [](lua_State* L) -> int {
        PokeCore* core = CoreFromLua(L);
        if (!core || !core->nds) return 0;
        uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
        int count = (int) luaL_checkinteger(L, 2);
        MemoryDomain domain;
        ResolveDomain(core, L, 3, domain);
        lua_createtable(L, count, 0);
        for (int i = 0; i < count; i++)
        {
            uint8_t byte = 0;
            const int64_t addr = (int64_t) address + i;
            // Rebase before the bounds check (see DomainRead) — the old check
            // compared a guest ADDRESS against a domain SIZE, so every byte came
            // back 0.
            const int64_t off = addr - (int64_t) domain.start;
            if (off >= 0 && (uint64_t) off < domain.size)
                DomainRead(domain, core->nds.get(), &byte, addr, 1);
            // 1-indexed: the script handles both indexings.
            lua_pushinteger(L, byte);
            lua_seti(L, -2, i + 1);
        }
        return 1;
    });
    lua_setfield(L, -2, "read_bytes_as_array");
    lua_pop(L, 1); // memory table
}

void poke_set_script(PokeCore* core, const char* script)
{
    if (!core || !script) return;
    core->script = script;
}

// The directory holding the Pokémon Access Lua reader set (oga_bootstrap.lua
// + the reader tree), for Game Boy ROMs. NDS ROMs use poke_set_script with a
// concatenated source string instead; the GBA reader loads its ~177 files
// itself with loadfile, so it needs a directory, not a string.
void poke_set_psp_asset_dir(PokeCore* core, const char* asset_dir)
{
    if (!core) return;
    core->pspAssetDir = (asset_dir && *asset_dir) ? asset_dir : "";
}

void poke_set_script_dir(PokeCore* core, const char* dir)
{
    if (!core || !dir || !core->backend.ops) return;
    // Routed through the ops table: a backend without an external script set
    // has no member here and the call is a documented no-op, instead of the
    // `isGba` check that used to be the only thing keeping this safe.
    if (core->backend.ops->set_script_dir)
        core->backend.ops->set_script_dir(core->backend.state, dir);
}

// --------------------------------------------------------------------- loading

// Read a whole file into a vector. Returns false if it cannot be read or is
// empty, so a missing firmware dump degrades to FreeBIOS instead of failing the
// load.
static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }
    out.resize((size_t) len);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    if (got != out.size()) { out.clear(); return false; }
    return true;
}

// Point the core at real BIOS/firmware dumps. All three are optional; anything
// missing keeps melonDS's built-in fallback for that piece.
void poke_set_firmware(PokeCore* core, const char* bios9, const char* bios7, const char* firmware)
{
    if (!core) return;
    core->bios9Path = bios9 ? bios9 : "";
    core->bios7Path = bios7 ? bios7 : "";
    core->firmwarePath = firmware ? firmware : "";
}

// ------------------------------------------------------------------ Game Boy
//
// A .gba/.gbc/.gb ROM never touches melonDS: it loads into the mGBA core with
// the unmodified Pokémon Access Lua reader set. The native GBA adapter is
// SELECTED here but, like every NDS adapter, ATTACHED lazily by
// poke_adapter_ready — attaching only wires callbacks, and ready() still
// guards against uninitialised game state.

// The speech/log sinks the mGBA core calls, forwarded to the app's callbacks.
static void GbaSayForward(const char* utf8, bool interrupt, void* ctx)
{
    PokeCore* core = (PokeCore*) ctx;
    if (utf8) EmitSpeech(core, utf8, interrupt, 0);
}
static void GbaLogForward(const char* utf8, void* ctx)
{
    PokeCore* core = (PokeCore*) ctx;
    if (core && core->logCb && utf8) core->logCb(utf8, core->logUserdata);
}

// Tear down whichever backend ran before, so a new ROM never inherits a live
// core, a Lua state, or an attached adapter from the previous game.
static void TeardownBackends(PokeCore* core)
{
    if (core->adapter && core->adapterAttached && core->adapter->detach)
        core->adapter->detach();
    if (core->nds) { core->nds->Stop(); core->nds.reset(); }
    if (core->L) { lua_close(core->L); core->L = nullptr; core->coroutine = nullptr; core->scriptLoaded = false; }
    if (core->gba) { gba_destroy(core->gba); core->gba = nullptr; }
    if (core->psp) { psp_destroy(core->psp); core->psp = nullptr; }
    if (core->nes) { nes_destroy(core->nes); core->nes = nullptr; }
    core->adapter = nullptr;
    core->adapterAttached = false;
    // Clearing the ops is what makes "is a game loaded?" answerable from one
    // field: anything still holding an ops pointer would be describing a
    // console that is no longer running. The DS's load path re-claims it.
    core->backend.ops = nullptr;
    core->backend.state = nullptr;
}

static bool LoadGbaRom(PokeCore* core, const char* rom_path, const char* save_path)
{
    TeardownBackends(core);

    core->gba = gba_create();
    if (!core->gba) { SetError(core, "Could not create the Game Boy core."); return false; }
    char code[16] = {0};
    int plat = -1;
    if (!gba_load_rom(core->gba, rom_path, save_path ? save_path : "", code, &plat))
    {
        SetError(core, "%s", gba_last_error(core->gba));
        gba_destroy(core->gba);
        core->gba = nullptr;
        return false;
    }
    core->backend = oga_gba_core(core->gba);
    strncpy(core->gameCode, code, sizeof(core->gameCode) - 1);
    // The adapter matches by the code the host hands over (its registry entry
    // is empty by design); GB/GBC titles additionally match by platform.
    // Selected here, attached later by poke_adapter_ready like every adapter.
    oga::gba_set_game_code(code);
    gba_set_speech_callback(core->gba, GbaSayForward, core);
    gba_set_log_callback(core->gba, GbaLogForward, core);
    core->adapter = &oga::kGameBoyAdvance;
    return true;
}

static bool LoadNesRom(PokeCore* core, const char* rom_path, const char* save_path)
{
    TeardownBackends(core);

    core->nes = nes_create();
    if (!core->nes) { SetError(core, "Could not create the NES core."); return false; }
    char code[16] = {0};
    // ⛔ THE SAVE PATH IS MESEN'S HOME FOLDER. nes_load_rom passes it to
    // FolderUtilities::SetHomeFolder before the emulator is built, because Mesen THROWS
    // "Home folder not specified" from inside LoadRom without it -- and that exception is caught
    // and reported as a bare `false`, so the real cause is invisible from the caller.
    if (!nes_load_rom(core->nes, rom_path, save_path ? save_path : "", code))
    {
        SetError(core, "%s", nes_last_error(core->nes));
        nes_destroy(core->nes);
        core->nes = nullptr;
        return false;
    }
    core->backend = oga_nes_core(core->nes);
    strncpy(core->gameCode, code, sizeof(core->gameCode) - 1);
    // The NES adapter is a SEAM that refuses by design (Core/nes_adapter.cpp): it is the script
    // adapter's job to narrate, and no reader script is bundled yet. Selecting it here means the UI
    // correctly shows no reader controls, which is the truth, rather than showing controls that do
    // nothing.
    core->adapter = &oga::kNintendoEntertainmentSystem;
    return true;
}

static bool LoadPspRom(PokeCore* core, const char* rom_path, const char* save_path)
{
    TeardownBackends(core);

    core->psp = psp_create();
    if (!core->psp) { SetError(core, "Could not create the PSP core."); return false; }
    if (!core->pspAssetDir.empty())
        psp_set_asset_dir(core->psp, core->pspAssetDir.c_str());
    char code[16] = {0};
    if (!psp_load_rom(core->psp, rom_path, save_path ? save_path : "", code))
    {
        SetError(core, "%s", psp_last_error(core->psp));
        psp_destroy(core->psp);
        core->psp = nullptr;
        return false;
    }
    core->backend = oga_psp_core(core->psp);
    strncpy(core->gameCode, code, sizeof(core->gameCode) - 1);
    // The adapter comes from the registry by game ID (ULUS10437 -> Dissidia),
    // exactly like an NDS game. A PSP game with no adapter is the normal
    // no-reader case. Selected here, attached later by poke_adapter_ready.
    core->adapter = oga::find_by_game_code(core->gameCode);
    return true;
}

bool poke_load_rom(PokeCore* core, const char* rom_path, const char* save_path)
{
    if (!core || !rom_path) { SetError(core, "No ROM path given."); return false; }
    core->error[0] = 0;

    // Which backend runs this file, decided ONCE, here (see oga_core.h's
    // oga_resolve_backend). It used to be two extension checks written inline
    // plus a third copy of the same knowledge in the UI's switch, which is how
    // a .gba came to be offered to melonDS.
    //
    // ⛔ An unrecognised extension is REFUSED, never defaulted to the DS: a
    // misnamed file failing loudly beats emulating the wrong console.
    {
        const OgaResolvedBackend* backend = oga_resolve_backend(rom_path);
        if (!backend)
        {
            SetError(core, "That file type is not something this app can run.");
            return false;
        }
        if (backend->gba_hint) return LoadGbaRom(core, rom_path, save_path);
        /* ⛔ THE NES IS DISPATCHED BY ITS RESOLVED ID, so it cannot drift from the
         * registry the way an extension check would. A .nes that reached melonDS would
         * be the exact bug the resolver was written to end. */
        if (backend->nes_hint) return LoadNesRom(core, rom_path, save_path);
        if (backend->psp_hint) return LoadPspRom(core, rom_path, save_path);
    }

    // A new NDS ROM on a core that previously ran another backend: tear it
    // down first, or every branch below would keep serving the old game.
    // (TeardownBackends also clears the adapter selection, which the NDS
    // adapter-select below re-does from the registry.)
    TeardownBackends(core);

    FILE* f = fopen(rom_path, "rb");
    if (!f) { SetError(core, "Could not open the game file."); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); SetError(core, "The game file is empty."); return false; }

    auto rom = std::make_unique<uint8_t[]>((size_t) len);
    size_t got = fread(rom.get(), 1, (size_t) len, f);
    fclose(f);
    if (got != (size_t) len) { SetError(core, "Could not read the whole game file."); return false; }

    auto cart = NDSCart::ParseROM(std::move(rom), (uint32_t) len);
    if (!cart) { SetError(core, "This game file is not a Nintendo DS ROM."); return false; }

    // A fresh NDS per game. The software renderer is the core's own default
    // (GPU::SetRenderer falls back to it when handed nothing), and it is what
    // this app wants: the finished 256x192 framebuffers are read straight out
    // of the core, so no GPU surface is ever needed.
    //
    // ⛔ THE JIT MUST BE EXPLICITLY DISABLED, AND ASKING FOR IT IS A TRAP.
    // This build is compiled WITHOUT JIT_ENABLED (the ARM64 JIT needs MAP_JIT
    // and pthread_jit_write_protect_np, which iOS only grants to apps signed
    // with the dynamic-codesigning entitlement — a free-account sideload has
    // none). NDSArgs' default is `std::optional<JITArgs> JIT = JITArgs()`, i.e.
    // JIT *requested*. With it requested, NDS::RunFrame() dispatches to
    // RunFrame<CPUExecuteMode::JIT>() — and in a build where JIT_ENABLED is
    // absent, that path never actually runs the CPUs. The console "boots" and
    // then both ARM cores march through unmapped memory forever: the symptom is
    // a single frame that never completes, with the guest PC sitting in the
    // 0x2E8xxxxx range. Setting JIT to std::nullopt selects the interpreter,
    // which is the correct (and only) engine here.
    NDSArgs args;
    args.OutputSampleRate = 32768.0;
    args.JIT = std::nullopt;   // interpreter: see the note above

    // Real BIOS + firmware when the app has them. This is what decides whether
    // the console boots through the firmware/menu handshake or is forced into
    // direct boot:
    //   * a real bios9/bios7 makes IsLoadedARM9BIOSKnownNative() true, and
    //   * a real firmware makes SPI.GetFirmware().IsBootable() true,
    // and NeedsDirectBoot() returns true unless BOTH hold. Without them the
    // console is forced down the direct-boot path, which fabricates the state
    // the menu would have left behind — and a game that waits on that handshake
    // simply never starts.
    {
        std::vector<uint8_t> b9, b7, fw;
        if (ReadWholeFile(core->bios9Path, b9))
        {
            if (b9.size() == ARM9BIOSSize)
                memcpy(args.ARM9BIOS->data(), b9.data(), ARM9BIOSSize);
            else
                SetError(core, "bios9.bin is %zu bytes, expected %d.",
                         b9.size(), (int) ARM9BIOSSize);
        }
        if (ReadWholeFile(core->bios7Path, b7))
        {
            if (b7.size() == ARM7BIOSSize)
                memcpy(args.ARM7BIOS->data(), b7.data(), ARM7BIOSSize);
            else
                SetError(core, "bios7.bin is %zu bytes, expected %d.",
                         b7.size(), (int) ARM7BIOSSize);
        }
        if (ReadWholeFile(core->firmwarePath, fw))
        {
            if (fw.size() >= 0x20000)
            {
                args.Firmware = Firmware(fw.data(), (u32) fw.size());
                core->hasFirmware = args.Firmware.IsBootable();
            }
            else
            {
                SetError(core, "firmware.bin is %zu bytes, expected at least 131072.",
                         fw.size());
            }
        }
    }

    core->nds = std::make_unique<NDS>(std::move(args));

    // ⛔ THE CONSOLE MUST BE RESET BEFORE THE CART IS INSERTED AND BOOTED, OR
    // EVERY FRAME RUNS FOREVER. NDS::Reset() is what fills ARM9MemTimings /
    // ARM7MemTimings, ARM9ClockShift and MainRAMMask. Constructing an NDS and
    // going straight to SetNDSCart + SetupDirectBoot leaves ALL of them zero,
    // and with zero timings every instruction adds 0 cycles: `Cycles` never
    // grows, ARM7Timestamp never reaches its target, and NDS::RunFrame's inner
    // `while (ARM7Timestamp < target)` never terminates. The symptom is exactly
    // what this app showed on iOS and on the host: the CPUs execute REAL game
    // code (PCs track correctly through the ROM) while frame 0 never completes —
    // measured at >300 s per frame, 0 frames done in 30 s.
    // The Qt frontend always resets first; this call is the missing step.
    core->nds->Reset();

    core->nds->SetNDSCart(std::move(cart));

    if (save_path)
    {
        // melonDS allocates the save memory when the cart is inserted; loading
        // an existing .sav over it is what makes progress persist. This must
        // happen BEFORE the reset below, because the reset re-initialises the
        // console and the reference frontends likewise have the save already
        // inside the cart before they reset.
        FILE* sf = fopen(save_path, "rb");
        if (sf)
        {
            uint8_t* saveMem = core->nds->GetNDSSave();
            uint32_t saveLen = core->nds->GetNDSSaveLength();
            if (saveMem && saveLen) fread(saveMem, 1, saveLen, sf);
            fclose(sf);
        }
    }
    core->savePath = save_path ? save_path : "";

    // ⛔ RESET AGAIN *AFTER* THE CART IS INSERTED — this is the step that makes
    // a cart-using game actually start. Both reference frontends do exactly this
    // and the second reset is NOT redundant:
    //
    //   Qt:      new NDS() → Reset() → SetNDSCart() → Reset() → SetupDirectBoot() → Start()
    //   Android: new NDS() → Reset() → SetNDSCart() → Reset() → SetupDirectBoot() → Start()
    //
    // Reset() re-initialises the memory controller, VRAM banks and LCD power for
    // the console *as it now stands*, with a cart present. Resetting only before
    // the insert leaves the console (and the GPU's display setup) configured for
    // a cartless machine, which is what produced a valid but permanently flat
    // framebuffer: the GPU never gets told to draw.
    core->nds->Reset();

    // ⛔ THE POWER-MANAGEMENT CHIP AND THE RTC MUST BE SEEDED AFTER RESET.
    // melonDS's Reset() leaves the SPI PowerMan battery state and the RTC
    // unset, and it expects the FRONTEND to provide them — the core has no idea
    // what the real battery/clock are. The Android frontend does exactly this
    // pair after every reset:
    //     setBatteryLevels();  // SPI.GetPowerMan()->SetBatteryLevelOkay(true)
    //     setDateTime();       // RTC.SetDateTime(now)
    // Skipping them leaves the console reporting an unset battery and a zeroed
    // clock. Pokémon B/W reads both early in boot, so this is a real candidate
    // for a game that runs without ever reaching its graphics setup.
    if (core->nds->SPI.GetPowerMan())
        core->nds->SPI.GetPowerMan()->SetBatteryLevelOkay(true);

    {
        std::time_t t = std::time(nullptr);
        std::tm* now = std::localtime(&t);
        if (now)
            core->nds->RTC.SetDateTime(now->tm_year + 1900, now->tm_mon + 1,
                                       now->tm_mday, now->tm_hour, now->tm_min,
                                       now->tm_sec);
    }

    // Boot the way the console actually does when we have real dumps. There is
    // NO NDS::Boot() to call — firmware boot is expressed by simply NOT doing a
    // direct boot: the CPUs run the real BIOS from the reset vector, the BIOS
    // runs the firmware, and the firmware boots the inserted cart. That is the
    // whole handshake a game's boot code waits on, and it is why direct boot
    // (which fabricates the end state instead) can leave a game spinning.
    // With FreeBIOS or a generated firmware, NeedsDirectBoot() is true and
    // direct boot is the only option — the firmware image is not executable.
    if (core->nds->NeedsDirectBoot())
        core->nds->SetupDirectBoot(rom_path);

    // ---------------------------------------------------------- adapter select
    //
    // The ROM's game code is at header offset 0x0C..0x0F. It is read from the
    // PARSED cart rather than the file, so it is the same bytes the loader used
    // and cannot disagree with what was actually booted.
    //
    // A match does NOT mean the game is ready — the game's own structures do not
    // exist yet. It only means an adapter is willing to try. attach() is deferred
    // until the console has booted far enough, and ready() gates what is spoken.
    core->backend.ops = NdsOpsTable();
    core->backend.state = core;   // the DS's state IS the core; see poke_create
    core->adapter = nullptr;
    core->adapterAttached = false;
    core->gameCode[0] = 0;
    {
        // The game code is the four ASCII bytes at ROM offset 0x0C. Read them from
        // the cartridge's own ROM buffer rather than from the file we opened: that
        // buffer is what the console actually booted, so the code cannot disagree
        // with the running game. (`Header` is protected on CartCommon, so the ROM
        // bytes are the accessible path; NDSCart.h's GameCodeAsU32 is just this
        // same arithmetic over the same four bytes.)
        if (auto* cart = core->nds->GetNDSCart())
        {
            const u8* rom = cart->GetROM();
            if (rom && cart->GetROMLength() >= 0x10)
                for (int i = 0; i < 4; i++) core->gameCode[i] = (char) rom[0x0C + i];
        }
        core->gameCode[4] = 0;

        core->adapter = oga::find_by_game_code(core->gameCode);
    }

    return true;
}

// ------------------------------------------------------------------- scripting

static bool StartScript(PokeCore* core)
{
    if (!core->nds) return false;
    if (core->script.empty()) { SetError(core, "No accessibility script was bundled."); return false; }

    lua_State* L = luaL_newstate();
    if (!L) { SetError(core, "Could not create the script engine."); return false; }
    luaL_openlibs(L);
    core->L = L;

    InstallLibraries(core);

    if (luaL_loadbuffer(L, core->script.c_str(), core->script.size(), "@main.lua") != LUA_OK)
    {
        SetError(core, "Script load error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    // Run the script inside a coroutine: emu.frameadvance() yields it and the
    // frame loop resumes it, which is what keeps main.lua byte-identical to the
    // BizHawk original instead of being rewritten around a different loop shape.
    lua_State* co = lua_newthread(L);
    core->coroutine = co;
    lua_insert(L, -2);              // [thread, chunk]
    lua_xmove(L, co, 1);            // chunk -> coroutine stack

    // Lua 5.4's lua_resume takes a 4th argument and WRITES the result count
    // through it, so it must be a real pointer — passing nullptr segfaults
    // inside ldo.c at the first resume.
    int nresults = 0;
    int rc = lua_resume(co, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        SetError(core, "Script error: %s", lua_tostring(co, -1));
        return false;
    }
    core->scriptLoaded = true;
    return true;
}

bool poke_start(PokeCore* core)
{
    if (!core) { return false; }
    if (core->backend.ops && !IsPokeNds(core))
    {
        // The Game Boy reader boots inside gba_start (it says Ready when it
        // recognises the cart) and PSP boot happens inside psp_start; neither
        // has a concatenated script to install the way the DS does.
        if (!core->backend.ops->start(core->backend.state))
        {
            SetError(core, "%s", core->backend.ops->last_error(core->backend.state));
            return false;
        }
        core->running = true;
        return true;
    }
    if (!core->nds) { SetError(core, "Load a game first."); return false; }
    core->nds->Start();
    if (!StartScript(core)) return false;
    core->running = true;
    return true;
}

void poke_stop(PokeCore* core)
{
    if (!core) return;
    core->running = false;
    if (core->backend.ops && !IsPokeNds(core)) { core->backend.ops->stop(core->backend.state); return; }
    if (core->nds) core->nds->Stop();
}

// Host-only diagnostic hook: lets a test binary read the guest CPU state
// (program counters, halted flags) without exposing NDS in the public C API.
// ios-debug: not used by the app.
melonDS::NDS* poke_debug_nds(PokeCore* core)
{
    return core ? core->nds.get() : nullptr;
}

// Host-only: how many emulated frames have completed, for speed diagnostics.
// ios-debug: not used by the app.
unsigned long long poke_frames_completed(PokeCore* core)
{
    return core ? (unsigned long long) core->frameCounter : 0ULL;
}

bool poke_running(PokeCore* core) { return core && core->running; }
int poke_stop_reason(PokeCore* core) { return core ? core->stopReason : 0; }

// True when a real, bootable firmware image was installed (which is what lets
// the console boot through the firmware instead of being forced to direct boot).
bool poke_has_firmware(PokeCore* core) { return core && core->hasFirmware; }

// ------------------------------------------------------------------ frame loop

static void ApplyInput(PokeCore* core)
{
    // ⛔ DS KEYS ARE ACTIVE LOW, AND SetKeyMask REPLACES THE BITS DIRECTLY.
    // It does NOT invert for you: `KeyInput &= 0xFFFCFC00; KeyInput |= mask`.
    // So the mask passed here must have bit = 1 for RELEASED and bit = 0 for
    // PRESSED. Reset() sets KeyInput = 0x007F03FF — the low 12 bits (A/B/Select/
    // Start/dpad/R/L/X/Y) are all 1, i.e. "nothing held".
    //
    // Passing a bare pressed-bits mask (0 when nothing is pressed) therefore
    // reports EVERY BUTTON AS HELD, permanently. The effect is silent and
    // vicious: the guest still executes real code and frames still complete, but
    // a game cannot get past its boot input handling because the keypad never
    // goes quiet — so it never reaches graphics setup (VRAMCNT stays 0, VRAM
    // stays empty, the screen stays blank). That is exactly the symptom this app
    // showed for both Pokémon Black and Diamond.
    uint32_t released = 0x00000FFF;   // 12 buttons, all up
    // The script's joypad.set{} overrides win over the physical pad, and are
    // consumed here: main.lua re-asserts them every frame, and BizHawk likewise
    // wipes its override table every frame before the script's loop runs. An
    // override of "pressed" sets the bit back (remember: pressed means CLEARED in
    // the mask); an override of "released" leaves it released.
    for (int i = 0; i < POKE_BTN_COUNT; i++)
        if (core->buttonsDown & (1u << i)) released &= ~(1u << i);   // clear = pressed
    for (int i = 0; i < POKE_BTN_COUNT; i++)
        if (core->buttonOverrides & (1u << i))
        {
            if (core->overrideValues & (1u << i)) released &= ~(1u << i);  // forced down
            else                                  released |= (1u << i);   // forced up
        }
    core->buttonOverrides = 0;
    core->overrideValues = 0;
    core->nds->SetKeyMask(released);

    if (core->touchDown)
        core->nds->TouchScreen(core->touchX, core->touchY);
    else
        core->nds->ReleaseScreen();
}

// Stamp the adapter clock, run its per-frame hook, and pump the announcement
// queue: called on every emulated frame, on all three console paths.
static void EndFrame(PokeCore* core)
{
    core->host.now_ms = CoreNowMs();
    // ⛔ The adapter hook, NOT EndFrame: a replace-all once turned this line
    // into a self-call, which overflowed the stack on the first frame of
    // every game (v0.4.0). -Werror=infinite-recursion now guards it.
    if (core->adapterAttached && core->adapter && core->adapter->on_frame)
        core->adapter->on_frame();
    if (core->announceQ) oga::announce_tick(core->announceQ, core->host.now_ms);
}

bool poke_frame(PokeCore* core)
{
    if (!core || !core->running) return false;
    if (!core->backend.ops || !core->backend.state) return false;

    // ⛔ ONE FRAME, FOR EVERY CONSOLE. This used to be three near-identical
    // blocks stacked behind isGba/isPsp, each carrying its own copy of the
    // bookkeeping below — which is how the Game Boy path came to run a frame
    // WITHOUT the per-frame adapter hook, leaving its adapter permanently
    // un-ready while the DS path worked.
    //
    // The backend runs the frame. Everything after that is console-independent:
    // advance the frame counter, let the backend do whatever per-frame work of
    // its own it has (the DS flushes its save once a second), then stamp the
    // adapter clock, drive its on_frame and tick the announcement queue. For
    // GBA that hook is what notices the game becoming readable; for PSP it is
    // Dissidia's automatic menu-speech watch.
    if (!core->backend.ops->frame(core->backend.state)) return false;
    core->frameCounter++;
    if (core->backend.ops->tick)
        core->backend.ops->tick(core->backend.state, (uint64_t) core->frameCounter);
    EndFrame(core);
    return true;
}

// --------------------------------------------------------------------- display

bool poke_framebuffer(PokeCore* core, int screen, int* width, int* height)
{
    // The app reads pixels through poke_framebuffer_ptr, which serves this
    // staging buffer.
    if (!core || !core->backend.ops || !core->backend.state) return false;

    const uint8_t* src = nullptr;
    if (!core->backend.ops->framebuffer(core->backend.state, screen, width, height, &src))
        return false;
    if (!src || *width <= 0 || *height <= 0) return false;

    size_t n = (size_t)(*width) * (size_t)(*height) * 4;
    // ⛔ ONE BACKEND CONVERTS IN PLACE. The DS's pixels are A8R8G8B8 and need a
    // per-pixel reorder, so its op writes into THIS buffer and hands back a
    // pointer to it; copying a buffer onto itself is a no-op at best. Every
    // other backend hands back its own frame, which is copied because that
    // pointer is only valid until its next frame.
    if (src != core->frameRGBA.data())
    {
        if (core->frameRGBA.size() != n) core->frameRGBA.resize(n);
        memcpy(core->frameRGBA.data(), src, n);
    }
    else if (core->frameRGBA.size() != n)
    {
        core->frameRGBA.resize(n);
    }
    core->frameScreen = screen;
    return true;
}

const uint8_t* poke_framebuffer_ptr(PokeCore* core, int screen)
{
    if (!core || core->frameRGBA.empty() || core->frameScreen != screen) return nullptr;
    return core->frameRGBA.data();
}

// ----------------------------------------------------------------------- input

void poke_set_button(PokeCore* core, int ds_button, bool down)
{
    if (!core || ds_button < 0) return;
    if (core->backend.ops && !IsPokeNds(core))
    {
        // Pad indices are the app's shared numbering; the backend translates
        // (Cross on PSP, dropped X/Y on Game Boy). The UI sends the same
        // numbers for every console and only the labels change.
        core->backend.ops->set_button(core->backend.state, ds_button, down);
        return;
    }
    if (ds_button >= POKE_BTN_COUNT) return;
    if (down) core->buttonsDown |= (1u << ds_button);
    else core->buttonsDown &= ~(1u << ds_button);
}

void poke_set_analog(PokeCore *core, float x, float y)
{
    if (!core || !core->backend.ops || !core->backend.ops->set_analog) return;
    core->backend.ops->set_analog(core->backend.state, x, y);
}

void poke_touch(PokeCore* core, int x, int y, bool down)
{
    if (!core) return;
    // A backend that has no touch screen has no member here, so the call is a
    // documented no-op rather than a pair of console checks that had to be kept
    // in sync with the console list.
    if (core->backend.ops && !IsPokeNds(core))
    {
        if (core->backend.ops->set_touch)
            core->backend.ops->set_touch(core->backend.state, x, y, down);
        return;
    }
    core->touchX = (uint16_t) x;
    core->touchY = (uint16_t) y;
    core->touchDown = down;
}

void poke_set_hotkey(PokeCore* core, const char* key, bool down)
{
    if (!core || !key || !*key) return;
    if (core->backend.ops && !IsPokeNds(core))
    {
        // Hotkeys go straight to the reader's command layer where there is one.
        // PSP has none (the native adapters are the only reader), and its ops
        // table says so by leaving the member NULL.
        if (core->backend.ops->set_hotkey)
            core->backend.ops->set_hotkey(core->backend.state, key, down);
        return;
    }
    char k = key[0];
    auto it = std::find(core->hotkeysDown.begin(), core->hotkeysDown.end(), k);
    if (down && it == core->hotkeysDown.end()) core->hotkeysDown.push_back(k);
    else if (!down && it != core->hotkeysDown.end()) core->hotkeysDown.erase(it);
}

// ----------------------------------------------------------------------- audio

int poke_read_audio(PokeCore* core, int16_t* out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    if (core->backend.ops && !IsPokeNds(core))
    {
        // No member means no audio path from this console yet (the Game Boy
        // reader's cues are text, so there is nothing to miss).
        if (!core->backend.ops->read_audio || !core->audioEnabled) return 0;
        return core->backend.ops->read_audio(core->backend.state, out, max_frames);
    }
    if (!core->nds || !core->audioEnabled) return 0;
    return core->nds->SPU.ReadOutput(out, max_frames);
}

void poke_set_audio_enabled(PokeCore* core, bool enabled)
{
    if (core) core->audioEnabled = enabled;
}

// ------------------------------------------------------------------ savestates

bool poke_save_state(PokeCore* core, const char* path)
{
    if (!core || !path) return false;
    if (core->backend.ops && !IsPokeNds(core))
    {
        if (!core->backend.ops->save_state(core->backend.state, path))
        {
            SetError(core, "Could not save the game state.");
            return false;
        }
        return true;
    }
    if (!core->nds) return false;
    Savestate state;
    core->nds->DoSavestate(&state);
    if (state.Error) { SetError(core, "Could not save the game state."); return false; }

    FILE* f = fopen(path, "wb");
    if (!f) { SetError(core, "Could not write the saved state."); return false; }
    fwrite(state.Buffer(), 1, state.Length(), f);
    fclose(f);
    return true;
}

bool poke_load_state(PokeCore* core, const char* path)
{
    if (!core || !path) return false;
    if (core->backend.ops && !IsPokeNds(core))
    {
        if (!core->backend.ops->load_state(core->backend.state, path))
        {
            SetError(core, "The saved state could not be loaded.");
            return false;
        }
        return true;
    }
    if (!core->nds) return false;
    FILE* f = fopen(path, "rb");
    if (!f) { SetError(core, "No saved state found."); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); SetError(core, "The saved state is empty."); return false; }

    std::vector<uint8_t> buffer((size_t) len);
    fread(buffer.data(), 1, buffer.size(), f);
    fclose(f);

    Savestate state(buffer.data(), (uint32_t) buffer.size(), false);
    core->nds->DoSavestate(&state);
    if (state.Error) { SetError(core, "The saved state could not be loaded."); return false; }
    return true;
}

// ==================================================================== NDS ops
//
// DEFINED last, on purpose: every function below calls something this file
// defines earlier (ApplyInput, FlushSave, BuildHost, AdapterReadScalar), and
// the table itself is forward-declared near the top.
//
// The DS is the one console whose ops live here rather than in Core/oga_core.cpp,
// because they need PokeCore's private type. It is also the only backend with an
// adapter layer and a Lua script, which is why `attach`, `on_frame` and `tick`
// are populated HERE and NULL for the other two. That asymmetry is real, and
// making a console pretend to have an adapter layer it lacks would be worse than
// a null check.

static bool NdsStart(void* state)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds) return false;
    core->nds->Start();
    return true;
}

static void NdsStop(void* state)
{
    PokeCore* core = (PokeCore*) state;
    if (core->nds) core->nds->Stop();
}

static bool NdsFrame(void* state)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds) return false;
    ApplyInput(core);
    core->nds->RunFrame();

    // The frame is finished; hand control to the accessibility script for this
    // frame. This is melonDS-lua's _Update() hook, minus the Qt dependency.
    if (core->scriptLoaded && core->coroutine)
    {
        int nresults = 0;   // Lua 5.4 requires a real pointer here
        int rc = lua_resume(core->coroutine, nullptr, 0, &nresults);
        if (rc != LUA_OK && rc != LUA_YIELD)
        {
            // A script error must not kill the loop silently: the player would
            // lose every bit of speech with no explanation.
            const char* err = lua_tostring(core->coroutine, -1);
            if (core->logCb) core->logCb(err ? err : "script error", core->logUserdata);
            core->scriptLoaded = false;
        }
    }
    return true;
}

// The DS's own per-frame bookkeeping: flush the save once a second. This used to
// sit inline in poke_frame, which is why it only ever happened for the DS.
static void NdsTick(void* state, uint64_t frame_index)
{
    PokeCore* core = (PokeCore*) state;
    (void) frame_index;
    if (core->frameCounter % 60 == 0 && !core->savePath.empty())
        FlushSave(core);
}

// Adapter attach: wire the callbacks once, lazily. Attaching at ROM load would
// hand the adapter a RAM image with none of the game's objects in it yet.
static void NdsAttach(void* state)
{
    PokeCore* core = (PokeCore*) state;
    if (core->adapterAttached) return;
    BuildHost(core);
    if (core->adapter && core->adapter->attach && core->adapter->attach(&core->host))
        core->adapterAttached = true;
}

static void NdsOnFrame(void* state)
{
    PokeCore* core = (PokeCore*) state;
    if (core->adapterAttached && core->adapter && core->adapter->on_frame)
        core->adapter->on_frame();
}

static bool NdsRead(void* state, uint32_t addr, int width, uint32_t* out)
{
    return AdapterReadScalar((PokeCore*) state, addr, width, out);
}

// A8R8G8B8 (alpha in the high byte) -> non-premultiplied RGBA with solid alpha.
// The DS is the only console here with two panels: `screen` picks top or bottom.
static bool NdsFramebuffer(void* state, int screen, int* w, int* h, const uint8_t** pixels)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds) return false;
    void* top = nullptr;
    void* bottom = nullptr;
    if (!core->nds->GPU.GetFramebuffers(&top, &bottom)) return false;
    void* src = (screen == POKE_SCREEN_TOP) ? top : bottom;
    if (!src) return false;

    const int W = 256, H = 192;
    size_t n = (size_t) W * H * 4;
    if (core->frameRGBA.size() != n) core->frameRGBA.resize(n);
    const uint32_t* in = static_cast<const uint32_t*>(src);
    uint8_t* dst = core->frameRGBA.data();
    for (int i = 0; i < W * H; i++)
    {
        uint32_t px = in[i];
        dst[i * 4 + 0] = (uint8_t) ((px >> 16) & 0xFF);
        dst[i * 4 + 1] = (uint8_t) ((px >> 8) & 0xFF);
        dst[i * 4 + 2] = (uint8_t) (px & 0xFF);
        dst[i * 4 + 3] = 0xFF;
    }
    if (w) *w = W;
    if (h) *h = H;
    if (pixels) *pixels = core->frameRGBA.data();
    return true;
}

static void NdsSetButton(void* state, int pad_button, bool down)
{
    PokeCore* core = (PokeCore*) state;
    if (pad_button < 0 || pad_button >= POKE_BTN_COUNT) return;
    if (down) core->buttonsDown |= (1u << pad_button);
    else      core->buttonsDown &= ~(1u << pad_button);
}

// DS keys are active-low; the mask is built in ApplyInput at frame time, because
// the script's per-frame joypad.set overrides have to be folded in first.
static void NdsSetTouch(void* state, int x, int y, bool down)
{
    PokeCore* core = (PokeCore*) state;
    core->touchX = (uint16_t) x;
    core->touchY = (uint16_t) y;
    core->touchDown = down;
}

static void NdsSetHotkey(void* state, const char* key, bool down)
{
    PokeCore* core = (PokeCore*) state;
    char k = key[0];
    auto it = std::find(core->hotkeysDown.begin(), core->hotkeysDown.end(), k);
    if (down && it == core->hotkeysDown.end()) core->hotkeysDown.push_back(k);
    else if (!down && it != core->hotkeysDown.end()) core->hotkeysDown.erase(it);
}

static int NdsReadAudio(void* state, int16_t* out, int max_frames)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds || !core->audioEnabled) return 0;
    return core->nds->SPU.ReadOutput(out, max_frames);
}

static bool NdsSaveState(void* state, const char* path)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds) return false;
    Savestate st;
    core->nds->DoSavestate(&st);
    if (st.Error) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(st.Buffer(), 1, st.Length(), f);
    fclose(f);
    return true;
}

static bool NdsLoadState(void* state, const char* path)
{
    PokeCore* core = (PokeCore*) state;
    if (!core->nds) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }
    std::vector<uint8_t> buffer((size_t) len);
    size_t got = fread(buffer.data(), 1, buffer.size(), f);
    fclose(f);
    if (got != buffer.size()) return false;
    Savestate st(buffer.data(), (uint32_t) buffer.size(), false);
    core->nds->DoSavestate(&st);
    return !st.Error;
}

static unsigned long long NdsFrames(void* state)
{
    return (unsigned long long) ((PokeCore*) state)->frameCounter;
}

static const char* NdsLastError(void* state)
{
    PokeCore* core = (PokeCore*) state;
    return core ? core->error : "no core";
}

// The DS's ops. `set_analog` is NULL: a DS has no stick. `set_script_dir` is
// NULL because the DS reader arrives as one concatenated string via
// poke_set_script, not as a directory of files.
//
// The table is a function-local static: built once, on the first call, and
// reachable from above (see NdsOpsTable's declaration) without a bare forward
// declaration, which C++ rejects for a const at namespace scope.
static const OgaCoreOps* NdsOpsTable(void)
{
    static const OgaCoreOps kTable = {
    "nds",
    NdsStart, NdsStop, NdsFrame,
    NdsTick,
    NdsRead,
    NdsAttach, NdsOnFrame,
    NdsFramebuffer,
    NdsSetButton, NULL, NdsSetTouch,
    NdsSetHotkey,
    NULL,                       /* set_script_dir: the DS reader is a script string, not a directory */
        NULL,                   /* reader_set: no bundled reader set for the DS */
        NdsReadAudio,
        NdsSaveState, NdsLoadState,
        NdsFrames, NdsLastError,
    };
    return &kTable;
}
