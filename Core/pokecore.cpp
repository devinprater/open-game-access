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
#include "Savestate.h"
#include "Platform.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
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

struct PokeCore {
    std::unique_ptr<NDS> nds;
    char error[512] = {0};
    std::string savePath;

    // ---- script engine ----
    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;
    bool scriptLoaded = false;
    std::string script;
    int frameCounter = 0;

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
    if (core && core->speechCb && text) core->speechCb(text, interrupt, core->speechUserdata);
    return 0;
}

// speech.stop — the script's "stop talking now" (its R key). It MUST reach the
// platform: the callback's contract is that a NULL text means stop, and without
// forwarding that, the on-screen "Stop speech" button and the R key do nothing
// on iOS while working fine on Android. Speech has no other way to be stopped.
static int LuaStopSpeech(lua_State* L)
{
    PokeCore* core = CoreFromLua(L);
    if (core && core->speechCb) core->speechCb(nullptr, true, core->speechUserdata);
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
    gCurrentCore = core;
    return core;
}

void poke_destroy(PokeCore* core)
{
    if (!core) return;
    if (core->nds) core->nds->Stop();
    if (core->L) lua_close(core->L);
    if (gCurrentCore == core) gCurrentCore = nullptr;
    delete core;
}

void poke_set_speech_callback(PokeCore* core, PokeSpeechCallback cb, void* userdata)
{
    if (!core) return;
    core->speechCb = cb;
    core->speechUserdata = userdata;
}

void poke_set_log_callback(PokeCore* core, PokeLogCallback cb, void* userdata)
{
    if (!core) return;
    core->logCb = cb;
    core->logUserdata = userdata;
}

const char* poke_last_error(PokeCore* core) { return core ? core->error : "no core"; }
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

bool poke_load_rom(PokeCore* core, const char* rom_path, const char* save_path)
{
    if (!core || !rom_path) { SetError(core, "No ROM path given."); return false; }
    core->error[0] = 0;

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
    if (!core || !core->nds) { SetError(core, "Load a game first."); return false; }
    core->nds->Start();
    if (!StartScript(core)) return false;
    core->running = true;
    return true;
}

void poke_stop(PokeCore* core)
{
    if (!core) return;
    core->running = false;
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

bool poke_frame(PokeCore* core)
{
    if (!core || !core->nds || !core->running) return false;

    ApplyInput(core);
    core->nds->RunFrame();
    core->frameCounter++;

    // The frame is finished; hand control to the accessibility script for this
    // frame. This is melonDS-lua's _Update() hook, minus the Qt dependency.
    if (core->scriptLoaded && core->coroutine)
    {
        int nresults = 0;   // same Lua 5.4 requirement as the initial resume
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

    // Flush the save once a second.
    if (core->frameCounter % 60 == 0 && !core->savePath.empty())
        FlushSave(core);

    return true;
}

// --------------------------------------------------------------------- display

bool poke_framebuffer(PokeCore* core, int screen, int* width, int* height)
{
    if (width) *width = 256;
    if (height) *height = 192;
    if (!core || !core->nds) return false;

    void* top = nullptr;
    void* bottom = nullptr;
    if (!core->nds->GPU.GetFramebuffers(&top, &bottom)) return false;
    void* src = (screen == POKE_SCREEN_TOP) ? top : bottom;
    if (!src) return false;

    if (core->frameRGBA.size() != 256 * 192 * 4) core->frameRGBA.resize(256 * 192 * 4);
    const uint32_t* pixels = static_cast<const uint32_t*>(src);
    uint8_t* dst = core->frameRGBA.data();
    // The core's framebuffers are A8R8G8B8 (alpha in the high byte); the app
    // wants non-premultiplied RGBA with a solid alpha.
    for (int i = 0; i < 256 * 192; i++)
    {
        uint32_t p = pixels[i];
        dst[i * 4 + 0] = (uint8_t) ((p >> 16) & 0xFF);
        dst[i * 4 + 1] = (uint8_t) ((p >> 8) & 0xFF);
        dst[i * 4 + 2] = (uint8_t) (p & 0xFF);
        dst[i * 4 + 3] = 0xFF;
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
    if (!core || ds_button < 0 || ds_button >= POKE_BTN_COUNT) return;
    if (down) core->buttonsDown |= (1u << ds_button);
    else core->buttonsDown &= ~(1u << ds_button);
}

void poke_touch(PokeCore* core, int x, int y, bool down)
{
    if (!core) return;
    core->touchX = (uint16_t) x;
    core->touchY = (uint16_t) y;
    core->touchDown = down;
}

void poke_set_hotkey(PokeCore* core, const char* key, bool down)
{
    if (!core || !key || !*key) return;
    char k = key[0];
    auto it = std::find(core->hotkeysDown.begin(), core->hotkeysDown.end(), k);
    if (down && it == core->hotkeysDown.end()) core->hotkeysDown.push_back(k);
    else if (!down && it != core->hotkeysDown.end()) core->hotkeysDown.erase(it);
}

// ----------------------------------------------------------------------- audio

int poke_read_audio(PokeCore* core, int16_t* out, int max_frames)
{
    if (!core || !core->nds || !core->audioEnabled || !out || max_frames <= 0) return 0;
    return core->nds->SPU.ReadOutput(out, max_frames);
}

void poke_set_audio_enabled(PokeCore* core, bool enabled)
{
    if (core) core->audioEnabled = enabled;
}

// ------------------------------------------------------------------ savestates

bool poke_save_state(PokeCore* core, const char* path)
{
    if (!core || !core->nds || !path) return false;
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
    if (!core || !core->nds || !path) return false;
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
