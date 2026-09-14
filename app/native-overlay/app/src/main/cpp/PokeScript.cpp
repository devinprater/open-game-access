/*
    PokeScript.cpp — the Lua accessibility host for the Android melonDS frontend.

    This is the Android port of pokemon-access-ios/Core/pokecore.cpp's scripting
    half. It hosts Lua 5.4, installs a BizHawk-compatible API surface on top of
    melonDS's own memory/input accessors, and runs Ola's pokemon-access main.lua
    UNMODIFIED inside a coroutine that is resumed once per emulated frame.

    Ported in spirit (and mostly in code) from pokecore.cpp:

      * the memory library: melonDS-lua's memory read/write functions with
        address first and an optional domain name last, plus getmemorydomainlist,
        read_bytes_as_array and write_bytes_as_array. Domain name matching
        strips spaces/underscores so the shim's "MainRAM" and melonDS-lua's
        "Main RAM" both resolve.
      * SafeToPeek: reading the ARM9/ARM7 system bus must not disturb hardware
        registers with side effects (key input, touch, IPC FIFOs).
      * the input library: HeldKeys / GetJoy / NDSTapDown / NDSTapUp, so the
        shim's joypad.setfrommnemonicstr and input.get have something to call.
      * gui.* as no-ops: the accessibility tool draws nothing (everything it
        produces is speech), but a script that calls gui.* must not error out.
      * `print` routed to the app's log, so the script's console.writeline
        trail is visible in logcat.
      * `hermes_tts.speak(text, mode)` — the global the iOS shim's speech.say
        calls — routed to the platform's TextToSpeech.

    Differences from pokecore.cpp, deliberately:

      * There is no NDS owned here. The script talks to the *running*
        MelonInstance's NDS, handed over with setNds() at ROM load.
      * Speech goes out through a callback installed by the JNI layer rather
        than a C API the app calls directly.
      * The per-frame resume is driven from MelonInstance::runFrame(), right
        after nds->RunFrame() — melonDS-lua's _Update() hook.

    ⛔ TWO Lua 5.4 TRAPS THAT COST REAL DEBUGGING TIME — DO NOT "SIMPLIFY":

      1. lua_resume's 4th argument must be a REAL pointer. Lua 5.4 added an
         `int* nresults` out-parameter and writes through it; passing nullptr
         segfaults inside ldo.c on the first resume.
      2. The concatenated shim+main.lua chunk is AT Lua's 200-locals-per-chunk
         ceiling, so the shim must contribute ZERO top-level locals. The shim
         already wraps everything in one `do ... end` block for this reason.
         Never edit main.lua, and never un-wrap the shim.
*/

#include "PokeScript.h"

#include <cstring>
#include <string>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "NDSCart/CartCommon.h"
#include "MemConstants.h"
#include "ARM.h"
#include "types.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace melonDS;

namespace MelonDSAndroid
{

// ------------------------------------------------------------------ memory
//
// This mirrors melonDS-lua's own `memory` library (LuaMemory.cpp): the same
// domain names, the same argument order (address first, domain last), the same
// function names, so bizhawk_compat.lua and main.lua need no changes at all.

enum class Bus { NotBus, Arm9, Arm7 };

struct MemoryDomain
{
    const char* name;
    uint8_t* base;
    uint64_t size;
    Bus bus;
    /// Guest address this flat domain starts at. The script reads and writes by
    /// GUEST ADDRESS (`0x02000000 + off` for Main RAM), so every access must be
    /// rebased to a buffer offset by subtracting this first. Without it, a read
    /// of `0x02146284` is compared against a 4 MiB size, rejected, and silently
    /// returns 0 — which looks exactly like "the game's memory is empty".
    uint64_t start;
};

static std::vector<MemoryDomain> DomainsFor(NDS* nds)
{
    std::vector<MemoryDomain> domains;
    if (!nds) return domains;

    // Main RAM is 4 MiB on the DS and 16 MiB on the DSi. Do NOT divide by 4 for
    // the DS: the flat buffer is `MainRAMMaxSize` bytes regardless, and halving
    // it only hides the real problem (addresses must be rebased anyway).
    const uint64_t mainRamSize = (uint64_t) MainRAMMaxSize;
    domains.push_back({"Main RAM", nds->MainRAM, mainRamSize, Bus::NotBus, 0x02000000});
    domains.push_back({"Shared WRAM", nds->SharedWRAM, SharedWRAMSize, Bus::NotBus, 0x03000000});
    domains.push_back({"ARM7 WRAM", nds->ARM7WRAM, ARM7WRAMSize, Bus::NotBus, 0x03800000});
    if (auto* cart = nds->GetNDSCart())
    {
        // ROM and save data have no linear guest mapping: address them by offset.
        domains.push_back({"SRAM", nds->GetNDSSave(), nds->GetNDSSaveLength(), Bus::NotBus, 0});
        domains.push_back({"ROM", const_cast<uint8_t*>(cart->GetROM()), cart->GetROMLength(), Bus::NotBus, 0});
    }
    domains.push_back({"Instruction TCM", nds->ARM9.ITCM, ITCMPhysicalSize, Bus::NotBus, 0});
    domains.push_back({"Data TCM", nds->ARM9.DTCM, DTCMPhysicalSize, Bus::NotBus, 0});
    domains.push_back({"ARM9 BIOS", const_cast<uint8_t*>(nds->GetARM9BIOS().data()), ARM9BIOSSize, Bus::NotBus, 0xFFFF0000});
    domains.push_back({"ARM7 BIOS", const_cast<uint8_t*>(nds->GetARM7BIOS().data()), ARM7BIOSSize, Bus::NotBus, 0});
    domains.push_back({"ARM9 System Bus", nullptr, 0x10000000, Bus::Arm9, 0});
    domains.push_back({"ARM7 System Bus", nullptr, 0x10000000, Bus::Arm7, 0});
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
    while (count-- > 0)
    {
        switch (d.bus)
        {
        case Bus::NotBus:
        {
            // Rebase the GUEST address to a buffer offset. The script addresses
            // Main RAM as 0x02000000+off, so indexing the flat buffer directly
            // rejects every real address and returns 0 (see MemoryDomain::start).
            const int64_t off = address - (int64_t) d.start;
            *out = (off >= 0 && (uint64_t) off < d.size && d.base) ? d.base[off] : 0;
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
    while (count-- > 0)
    {
        switch (d.bus)
        {
        case Bus::NotBus:
        {
            const int64_t off = address - (int64_t) d.start;
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
static bool ResolveDomain(NDS* nds, lua_State* L, int argIndex, MemoryDomain& out)
{
    static std::vector<MemoryDomain> cache;
    cache = DomainsFor(nds);
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
                std::string t;
                for (char c : s) if (c != ' ' && c != '_') t.push_back(c);
                s = std::move(t);
            };
            strip(a); strip(b);
            if (a == b) { out = d; return true; }
        }
        return false;
    }
    return true;
}

// The host the Lua state belongs to, reachable from any lua_State (the
// coroutine included) through the registry.
static PokeScript* gScript = nullptr;

static void RegisterCore(lua_State* L, PokeScript* core)
{
    lua_pushlightuserdata(L, (void*) &gScript);
    lua_pushlightuserdata(L, core);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

static PokeScript* CoreFromLua(lua_State* L)
{
    lua_pushlightuserdata(L, (void*) &gScript);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* core = static_cast<PokeScript*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return core;
}

template <typename T>
static int LuaRead(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    NDS* nds = core ? core->nds() : nullptr;
    if (!nds) { lua_pushinteger(L, 0); return 1; }
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    MemoryDomain domain;
    ResolveDomain(nds, L, 2, domain);

    int64_t value = 0;
    uint8_t bits[sizeof(int64_t)] = {0};
    // Bounds-check the REBASED offset, not the guest address (see MemoryDomain::start).
    const int64_t off = (int64_t) address - (int64_t) domain.start;
    if (off >= 0 && (uint64_t) off + sizeof(T) <= domain.size)
        DomainRead(domain, nds, bits, address, sizeof(T));
    std::memcpy(&value, bits, sizeof(T));
    lua_pushinteger(L, (lua_Integer) value);
    return 1;
}

template <typename T>
static int LuaWrite(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    NDS* nds = core ? core->nds() : nullptr;
    if (!nds) return 0;
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    int64_t value = (int64_t) luaL_checkinteger(L, 2);
    MemoryDomain domain;
    if (!ResolveDomain(nds, L, 3, domain)) return 0;
    const int64_t off = (int64_t) address - (int64_t) domain.start;
    if (off < 0 || (uint64_t) off + sizeof(T) > domain.size) return 0;

    uint8_t bits[sizeof(int64_t)] = {0};
    std::memcpy(bits, &value, sizeof(T));
    DomainWrite(domain, nds, bits, address, sizeof(T));
    return 0;
}

static int LuaGetDomainList(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    auto domains = DomainsFor(core ? core->nds() : nullptr);
    lua_createtable(L, (int) domains.size(), 0);
    for (size_t i = 0; i < domains.size(); i++)
    {
        lua_pushstring(L, domains[i].name);
        lua_seti(L, -2, (lua_Integer) i + 1);
    }
    return 1;
}

// main.lua uses memory.read_bytes_as_array for every windowed read (dialogue
// buffers, name tables, dex rows). It handles both 0- and 1-indexing, so the
// core's 1-indexed table is passed straight through.
static int LuaReadBytesAsArray(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    NDS* nds = core ? core->nds() : nullptr;
    if (!nds) { lua_createtable(L, 0, 0); return 1; }
    int64_t address = (int64_t) luaL_checkinteger(L, 1);
    int count = (int) luaL_checkinteger(L, 2);
    if (count < 0) count = 0;
    MemoryDomain domain;
    ResolveDomain(nds, L, 3, domain);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; i++)
    {
        uint8_t byte = 0;
        int64_t addr = address + i;
        const int64_t off = addr - (int64_t) domain.start;
        if (off >= 0 && (uint64_t) off < domain.size)
            DomainRead(domain, nds, &byte, addr, 1);
        lua_pushinteger(L, byte);
        lua_seti(L, -2, i + 1);
    }
    return 1;
}

static int LuaWriteBytesAsArray(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    NDS* nds = core ? core->nds() : nullptr;
    if (!nds) return 0;
    int64_t address = (int64_t) luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    MemoryDomain domain;
    ResolveDomain(nds, L, 3, domain);
    lua_Integer count = luaL_len(L, 2);
    for (lua_Integer i = 1; i <= count; i++)
    {
        lua_geti(L, 2, i);
        int v = (int) lua_tointeger(L, -1);
        lua_pop(L, 1);
        uint8_t byte = (uint8_t) v;
        int64_t addr = address + (i - 1);
        const int64_t off = addr - (int64_t) domain.start;
        if (off >= 0 && (uint64_t) off < domain.size)
            DomainWrite(domain, nds, &byte, addr, 1);
    }
    return 0;
}

// ------------------------------------------------------------------- input
//
// melonDS-lua's input library, reduced to what the accessibility shim uses.
// Hotkeys arrive as single letters; the shim's input.get() passes them through.

static int LuaHeldKeys(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    lua_createtable(L, 0, 16);
    if (core) core->pushHeldKeys(L);
    return 1;
}

static int LuaGetJoy(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    lua_createtable(L, 0, 12);
    // melonDS-lua reports the pad as a name->bool table; main.lua's controller
    // mod layer asks for the core's button list here and uses it to block every
    // button while its modifier is held. Reporting the real DS buttons (and
    // their true state) is what makes that layer behave.
    static const char* names[] = {"A","B","Select","Start","Right","Left","Up","Down","R","L","X","Y"};
    uint32_t down = core ? core->joypadDownMask() : 0;
    for (int i = 0; i < 12; i++)
    {
        lua_pushboolean(L, (down & (1u << i)) != 0);
        lua_setfield(L, -2, names[i]);
    }
    return 1;
}

static int LuaNDSTapDown(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    if (core) core->touchDown((int) luaL_checkinteger(L, 1), (int) luaL_checkinteger(L, 2));
    return 0;
}

static int LuaNDSTapUp(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    if (core) core->touchUp();
    return 0;
}

// -------------------------------------------------------------------- gui
//
// The script's overlay canvas API. The accessibility tool draws nothing —
// everything it produces is speech — so these are accepted and ignored, which
// keeps a script that calls gui.* from erroring out and killing the loop.

static int LuaNoop(lua_State* L) { (void) L; return 0; }
static int LuaMakeCanvas(lua_State* L) { lua_pushinteger(L, 1); return 1; }

// ------------------------------------------------------------------- misc

static int LuaPrint(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; i++)
    {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s) { out.append(s, len); lua_pop(L, 1); }
    }
    if (core) core->log(out.c_str());
    return 0;
}

// speech.say / speech.stop — the one channel the player actually hears. The
// compat shim prefers a global named `hermes_tts`; this provides it.
static int LuaSpeak(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    const char* text = luaL_checkstring(L, 1);
    bool interrupt = true;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2))
    {
        const char* mode = lua_tostring(L, 2);
        interrupt = !(mode && std::strcmp(mode, "queue") == 0);
    }
    if (core && text) core->speak(text, interrupt);
    return 0;
}

static int LuaStopSpeech(lua_State* L)
{
    PokeScript* core = CoreFromLua(L);
    if (core) core->stopSpeech();
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
    {"read_bytes_as_array",  LuaReadBytesAsArray},
    {"write_bytes_as_array", LuaWriteBytesAsArray},
    {"getmemorydomainlist",  LuaGetDomainList},
    {nullptr, nullptr}
};

static const luaL_Reg kInputFuncs[] = {
    {"HeldKeys",   LuaHeldKeys},
    {"GetJoy",     LuaGetJoy},
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

// ------------------------------------------------------------------ lifecycle

PokeScript::PokeScript()
{
    gScript = this;
}

PokeScript::~PokeScript()
{
    stop();
    if (gScript == this) gScript = nullptr;
}

void PokeScript::setNds(melonDS::NDS* nds) { ndsPtr = nds; }

void PokeScript::setSpeechCallback(PokeSpeechCallback cb, void* userdata)
{
    speechCb = cb;
    speechUserdata = userdata;
}

void PokeScript::setLogCallback(PokeLogCallback cb, void* userdata)
{
    logCb = cb;
    logUserdata = userdata;
}

void PokeScript::setTouchCallback(PokeTouchCallback cb, void* userdata)
{
    touchCb = cb;
    touchUserdata = userdata;
}

void PokeScript::setScriptText(std::string text)
{
    script = std::move(text);
}

void PokeScript::setHotkey(char key, bool down)
{
    unsigned char k = (unsigned char) key;
    if (k >= 128) return;
    hotkeys[k] = down ? 1 : 0;
}

bool PokeScript::isHotkeyHeld(char key) const
{
    unsigned char k = (unsigned char) key;
    return k < 128 && hotkeys[k] != 0;
}

void PokeScript::pushHeldKeys(lua_State* L) const
{
    for (int i = 0; i < 128; i++)
    {
        if (!hotkeys[i]) continue;
        // The shim's input.get() keys this table by ASCII code and translates
        // to letters; sending the code directly means the same table shape
        // arrives at main.lua's poll_keys as on the melonDS-lua side.
        lua_pushboolean(L, 1);
        lua_seti(L, -2, (lua_Integer) i);
    }
}

void PokeScript::log(const char* text)
{
    if (logCb) logCb(text ? text : "", logUserdata);
}

void PokeScript::speak(const char* text, bool interrupt)
{
    if (speechCb) speechCb(text, interrupt, speechUserdata);
}

void PokeScript::stopSpeech()
{
    if (speechCb) speechCb(nullptr, true, speechUserdata);
}

void PokeScript::touchDown(int x, int y)
{
    touchX = x;
    touchY = y;
    if (touchCb) touchCb(x, y, true, touchUserdata);
}

void PokeScript::touchUp()
{
    if (touchCb) touchCb(touchX, touchY, false, touchUserdata);
}

// ------------------------------------------------------------ library install

void InstallLibraries(PokeScript* core, lua_State* L)
{
    RegisterCore(L, core);

    // Replace `print` with the console bridge so the script's dev trail reaches
    // the app's log instead of a stdout nobody sees. main.lua's say() also calls
    // console.writeline, which the shim maps to print — so this is also how
    // every spoken line becomes visible in logcat.
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
}

bool PokeScript::start()
{
    error.clear();

    if (script.empty())
    {
        error = "No accessibility script was bundled.";
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L)
    {
        error = "Could not create the script engine.";
        return false;
    }
    luaL_openlibs(L);
    this->L = L;

    InstallLibraries(this, L);

    if (luaL_loadbuffer(L, script.c_str(), script.size(), "@main.lua") != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        error = std::string("Script load error: ") + (err ? err : "?");
        lua_pop(L, 1);
        return false;
    }

    // Run the script inside a coroutine: emu.frameadvance() yields it and the
    // frame loop resumes it, which is what keeps main.lua byte-identical to the
    // BizHawk original instead of being rewritten around a different loop shape.
    lua_State* co = lua_newthread(L);
    this->coroutine = co;
    lua_insert(L, -2);              // [thread, chunk]
    lua_xmove(L, co, 1);            // chunk -> coroutine stack

    // Lua 5.4's lua_resume takes a 4th argument and WRITES the result count
    // through it, so it must be a real pointer — passing nullptr segfaults
    // inside ldo.c at the first resume.
    int nresults = 0;
    int rc = lua_resume(co, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        const char* err = lua_tostring(co, -1);
        error = std::string("Script error: ") + (err ? err : "?");
        if (logCb) logCb(error.c_str(), logUserdata);
        return false;
    }
    scriptLoaded = true;
    return true;
}

void PokeScript::runFrame()
{
    if (!scriptLoaded || !coroutine) return;
    framesRunCount++;

    // Diagnostic: is the emulated DS producing an image, and is the CPU making
    // progress? A game that never renders is a different failure from a script
    // that renders but does not speak, and only this counter tells them apart.
    if ((framesRunCount % 300) == 0 && ndsPtr)
    {
        void* top = nullptr;
        void* bottom = nullptr;
        bool isSoftware = ndsPtr->GPU.GetFramebuffers(&top, &bottom);
        char msg[256];
        if (isSoftware && top && bottom)
        {
            u32* fb = (u32*) top;
            size_t nonBlack = 0;
            for (size_t i = 0; i < 256u * 192u; i++)
                if ((fb[i] & 0x00FFFFFFu) != 0) nonBlack++;
            snprintf(msg, sizeof(msg),
                     "[ds-diag] frame %llu: nonBlack=%zu sample=0x%08X",
                     (unsigned long long) framesRunCount, nonBlack, fb[0]);
            if (logCb) logCb(msg, logUserdata);

            // Dump the DS top screen as a PPM so a stalled/invisible screen can
            // be inspected from real pixels instead of guessed at. Overwrites.
            {
                FILE* fp = std::fopen(
                    "/data/data/me.magnum.melonds.dev/cache/dsframe.ppm", "wb");
                if (fp)
                {
                    std::fprintf(fp, "P6\n256 192\n255\n");
                    for (int y = 0; y < 192; y++)
                        for (int x = 0; x < 256; x++)
                        {
                            u32 p = fb[y * 256 + x];
                            unsigned char rgb[3] = {
                                (unsigned char) (p & 0xFF),
                                (unsigned char) ((p >> 8) & 0xFF),
                                (unsigned char) ((p >> 16) & 0xFF) };
                            std::fwrite(rgb, 1, 3, fp);
                        }
                    std::fclose(fp);
                }
            }
        }
        else
        {
            snprintf(msg, sizeof(msg),
                     "[ds-diag] frame %llu: renderer is not software (top=%p bottom=%p)",
                     (unsigned long long) framesRunCount, top, bottom);
        }
        if (logCb) logCb(msg, logUserdata);
    }

    // The frame is finished; hand control to the accessibility script for this
    // frame — melonDS-lua's _Update() hook, minus the Qt dependency.
    int nresults = 0;   // same Lua 5.4 requirement as the initial resume
    int rc = lua_resume(coroutine, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        // A script error must not kill the loop silently: the player would lose
        // every bit of speech with no explanation.
        const char* err = lua_tostring(coroutine, -1);
        error = std::string("Script error: ") + (err ? err : "?");
        if (logCb) logCb(error.c_str(), logUserdata);
        scriptLoaded = false;
    }
}

void PokeScript::stop()
{
    scriptLoaded = false;
    if (L)
    {
        lua_close(L);
        L = nullptr;
        coroutine = nullptr;
    }
}

} // namespace MelonDSAndroid
