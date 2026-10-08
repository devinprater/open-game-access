/* gba_core.cpp — the iOS-side app glue for mGBA + the Pokémon Access readers.
 *
 * WHAT THIS IS. The third core backend in this app, beside the DS core
 * (pokecore.cpp) and the native adapters. It embeds mGBA's GBA/GB/GBC engine
 * and hosts Ola's pokemon.lua reader set (v3.1.0, unmodified) in the core's
 * own vendored Lua 5.4 state, resumed once per frame from a coroutine —
 * the same hosting shape pokecore.cpp uses for main.lua.
 *
 * THE BINDING MAP. The reader set was written for BizHawk, but the
 * repository already carries the translation layer: lua/mgba_compat.lua
 * presents the BizHawk surface (emu.frameadvance, memory.readbyte, input.read,
 * registerexec/registerwrite, getregister) and forwards to mGBA's Lua API —
 * an object-oriented surface (emu:read8, emu:runFrame, ...). This file
 * provides that SAME mGBA-shaped surface as plain Lua tables backed by C:
 *
 *   emu.platform/currentFrame/runFrame/read8/read16/read32/readRange/
 *       readRegister/getKeys/setKeys
 *   console.log | input (dummy; the shim shadows it) | callbacks (dummy)
 *
 * mgba_compat.lua cannot tell the difference between these tables and mGBA's
 * userdata (it only ever makes colon calls, which plain tables accept), so
 * the shim, the bootstrap and every reader file load byte-identical.
 *
 * WHY NOT mGBA'S OWN SCRIPT ENGINE. mGBA inverts the BizHawk model: the
 * emulator owns the thread and scripts register frame callbacks, while the
 * reader owns `while true do emu.frameadvance() end` and never yields to an
 * event loop. Hosting Lua ourselves (like pokecore.cpp) keeps the reader's
 * loop intact: emu.runFrame() yields the coroutine and gba_frame() resumes it.
 *
 * SPEECH. The bootstrap routes tolk.output through _G.oga_say when present
 * and preserves a host-installed sink across the shim load — so this file
 * installs oga_say (a C function into the speech callback) BEFORE loading
 * the bootstrap, and speech flows with no further plumbing.
 *
 * VIDEO. mGBA renders into our buffer, which must be set BEFORE reset — the
 * renderer associates with the buffer at reset time only when one is already
 * present. Pixels are XBGR8, converted to RGBA8888 for Swift. GBA is 240x160, GB/GBC is 160x144.
 */
#include "gba_core.h"

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/gb/core.h>
#include <mgba-util/vfs.h>
// The emulated-audio ring mGBA's GB and GBA cores fill. Public API:
// mAudioBufferAvailable/Read/Capacity. Draining it is the whole fix for "no sound".
#include <mgba-util/audio-buffer.h>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <cctype>
#include <cstdlib>
#include <mgba/debugger/debugger.h>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#define GBA_FB_W 240
#define GBA_FB_H 160
#define GB_FB_W 160
#define GB_FB_H 144

/* ⛔ SIZED FOR THE WIDEST GB/GBA FRAME THAT CAN BE RENDERED, WHICH IS NOT
 * 240x160. A Game Boy in SGB MODEL mode renders 256x224 -- the border is drawn
 * INTO the frame. The app switches SGB borders OFF (see gba_load_rom), so it
 * gets 160x144, but the render target must not be a buffer overflow waiting for
 * the day that setting changes. 256x224 covers every SGB, GB, GBC and GBA
 * frame; the GBA's own 240x160 fits inside it.
 * ⛔ THE STRIDE MATTERS EVEN WITH BORDERS OFF: a 240-wide GBA frame in a
 * 256-wide target is not contiguous, so both setVideoBuffer and the RGBA copy
 * must use this stride rather than frameW. */
static const int kFbStride = 256;
static const size_t kFbPixels = (size_t) kFbStride * 224;

struct GbaCore {
    mCore* core = nullptr;
    int platform = GBA_PLATFORM_GBA;

    // ---- script engine (same shape as the DS core) ----
    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;
    bool scriptLoaded = false;
    std::string scriptDir;

    // ---- host callbacks ----
    GbaSpeechCallback speechCb = nullptr;
    void* speechUserdata = nullptr;
    // Positional sound cues from the reader's audio.play (see PokeSoundCallback).
    // Null means the cues are dropped, which is what happened before this existed.
    GbaSoundCallback soundCb = nullptr;
    void* soundUserdata = nullptr;
    GbaLogCallback logCb = nullptr;
    void* logUserdata = nullptr;

    // ---- input ----
    uint32_t buttonsDown = 0;          // GBA pad bitmask (bit i = button i)
    std::vector<char> hotkeysDown;     // letters the reader listens for
    bool hotkeysDirty = false;

    // ---- video ----
    uint32_t rawFb[kFbPixels];                    // mGBA's XBGR8 render target
    std::vector<uint8_t> frameRGBA;
    int frameW = GBA_FB_W, frameH = GBA_FB_H;
    int frameScreen = -1;

    // ---- emulated audio ----
    // ⛔ THE CORE PRODUCES AUDIO WHETHER OR NOT ANYONE DRAINS IT, and nothing drained it: the
    // ops table had no read_audio for this backend, so every Game Boy / GBC / GBA game was silent
    // while its own music and effects were generated and thrown away. mGBA's GB and GBA cores fill
    // `core->getAudioBuffer()` and answer `core->audioSampleRate`; this pulls from that ring.
    //
    // The ring's rate is the console's own (GBA ~32768 Hz, GB 131072 Hz) and differs from the app's
    // 32768 Hz output, so reads are resampled. `audioFrac` carries the phase across reads, exactly
    // as psp_read_audio does -- restarting the phase each call produces a periodic click.
    std::vector<int16_t> audio;        // staged interleaved stereo at kGbaMixRate
    size_t audioRead = 0;              // consumed frames (compacts when fully drained)
    double audioFrac = 0.0;
    bool audioPrimed = false;

    // ---- save ----
    std::string savePath;

    unsigned long long frameCounter = 0;
    bool running = false;
    char error[512] = {0};

    // ---- real exec hooks (see gba_set_exec_hook) ----
    // mGBA's debugger can stop at an exact instruction, which is the only way the reader's
    // register-reading hooks can see correct CPU state. Set up lazily so a game with no hooks
    // pays nothing.
    mDebugger debugger;
    mDebuggerModule dbgModule;
    bool dbgAttached = false;
    std::unordered_map<uint32_t, int> execHooks;      // address -> Lua registry ref
    std::unordered_map<uint32_t, ssize_t> hookIds;    // address -> breakpoint id
};

static void SetError(GbaCore* core, const char* fmt, ...)
{
    if (!core) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(core->error, sizeof(core->error), fmt, ap);
    va_end(ap);
}

static GbaCore* CoreFromLua(lua_State* L)
{
    lua_pushlightuserdata(L, (void*) &CoreFromLua);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* core = static_cast<GbaCore*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return core;
}

static void RegisterCore(lua_State* L, GbaCore* core)
{
    lua_pushlightuserdata(L, (void*) &CoreFromLua);
    lua_pushlightuserdata(L, core);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

// ------------------------------------------------------------- Lua: speech

static int LuaSay(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    const char* text = luaL_checkstring(L, 1);
    bool interrupt = true;
    if (lua_gettop(L) >= 2 && lua_isboolean(L, 2)) interrupt = lua_toboolean(L, 2) != 0;
    if (core && core->speechCb) core->speechCb(text, interrupt, core->speechUserdata);
    return 0;
}

static int LuaConsoleLog(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    // console:log(...) arrives with self at 1; accept either shape.
    int n = lua_gettop(L);
    const char* text = luaL_checkstring(L, n >= 2 ? 2 : 1);
    if (core && core->logCb) core->logCb(text, core->logUserdata);
    return 0;
}

static int LuaPrint(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++)
    {
        if (i > 1) luaL_addchar(&b, '\t');
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        luaL_addlstring(&b, s, len);
        lua_pop(L, 1);
    }
    luaL_pushresult(&b);
    const char* text = lua_tostring(L, -1);
    if (core && core->logCb && text) core->logCb(text, core->logUserdata);
    return 0;
}

// ---------------------------------------------------------------- Lua: emu
//
// The mGBA-shaped surface mgba_compat.lua forwards to. Every function ignores
// its first argument (the table itself, passed by the shim's colon calls).

static int LuaEmuPlatform(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    lua_pushinteger(L, core ? core->platform : GBA_PLATFORM_GBA);
    return 1;
}

static int LuaEmuCurrentFrame(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    lua_pushinteger(L, core ? (lua_Integer) core->frameCounter : 0);
    return 1;
}

// runFrame yields the reader coroutine back to gba_frame(), which advances
// the emulator and resumes. This is the coroutine contract pokecore.cpp uses
// for emu.frameadvance, applied to mGBA's runFrame name.
static int LuaEmuRunFrame(lua_State* L)
{
    (void) CoreFromLua(L);
    return lua_yield(L, 0);
}

static int LuaEmuRead8(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 2);
    lua_pushinteger(L, (core && core->core) ? core->core->busRead8(core->core, addr) : 0);
    return 1;
}

static int LuaEmuRead16(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 2);
    lua_pushinteger(L, (core && core->core) ? core->core->busRead16(core->core, addr) : 0);
    return 1;
}

static int LuaEmuRead32(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 2);
    lua_pushinteger(L, (core && core->core) ? core->core->busRead32(core->core, addr) : 0);
    return 1;
}

// readRange returns a STRING (mGBA semantics). The compat shim converts it to
// the 1-based table the readers index — do NOT convert here, or text reads
// silently empty out (see mgba_compat.lua's critical-mapping note).
static int LuaEmuReadRange(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 2);
    int n = (int) luaL_checkinteger(L, 3);
    if (n < 0) n = 0;
    if (n > 65536) n = 65536;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 0; i < n; i++)
    {
        uint8_t v = (core && core->core) ? core->core->busRead8(core->core, addr + (uint32_t) i) : 0;
        luaL_addchar(&b, (char) v);
    }
    luaL_pushresult(&b);
    return 1;
}

static int LuaEmuReadRegister(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    const char* name = luaL_checkstring(L, 2);
    int32_t out = 0;
    bool ok = core && core->core && core->core->readRegister(core->core, name, &out);
    if (!ok) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer) out);
    return 1;
}

static int LuaEmuGetKeys(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    lua_pushinteger(L, core ? (lua_Integer) core->buttonsDown : 0);
    return 1;
}

static int LuaEmuSetKeys(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t mask = (uint32_t) luaL_checkinteger(L, 2);
    if (core)
    {
        core->buttonsDown = mask & 0x3FF;
        if (core->core) core->core->setKeys(core->core, core->buttonsDown);
    }
    return 0;
}

static const luaL_Reg kEmuFuncs[] = {
    {"platform", LuaEmuPlatform},
    {"currentFrame", LuaEmuCurrentFrame},
    {"framecount", LuaEmuCurrentFrame},
    {"runFrame", LuaEmuRunFrame},
    {"read8", LuaEmuRead8},
    {"read16", LuaEmuRead16},
    {"read32", LuaEmuRead32},
    {"readRange", LuaEmuReadRange},
    {"readRegister", LuaEmuReadRegister},
    {"getKeys", LuaEmuGetKeys},
    {"setKeys", LuaEmuSetKeys},
    {nullptr, nullptr},
};

// memory.* — the BizHawk surface the readers call directly (161 readbyte
// sites). Same bus reads, no domain argument.
static int LuaMemRead(lua_State* L, int width)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 1);
    uint32_t v = 0;
    if (core && core->core)
    {
        if (width == 1) v = core->core->busRead8(core->core, addr);
        else if (width == 2) v = core->core->busRead16(core->core, addr);
        else v = core->core->busRead32(core->core, addr);
    }
    lua_pushinteger(L, (lua_Integer) v);
    return 1;
}
static int LuaMemRead8(lua_State* L) { return LuaMemRead(L, 1); }
static int LuaMemRead16(lua_State* L) { return LuaMemRead(L, 2); }
static int LuaMemRead32(lua_State* L) { return LuaMemRead(L, 4); }
static int LuaMemReadSigned(lua_State* L, int width, int64_t min, int64_t sub)
{
    LuaMemRead(L, width);
    int64_t v = (int64_t) lua_tointeger(L, -1);
    if (v >= min) v -= sub;
    lua_pushinteger(L, (lua_Integer) v);
    return 1;
}
static int LuaMemReadS8(lua_State* L) { return LuaMemReadSigned(L, 1, 0x80, 0x100); }
static int LuaMemReadS16(lua_State* L) { return LuaMemReadSigned(L, 2, 0x8000, 0x10000); }
static int LuaMemReadS32(lua_State* L) { return LuaMemReadSigned(L, 4, (int64_t) 0x80000000, (int64_t) 0x100000000); }

static int LuaMemReadRange(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t addr = (uint32_t) luaL_checkinteger(L, 1);
    int n = (int) luaL_checkinteger(L, 2);
    if (n < 0) n = 0;
    if (n > 65536) n = 65536;
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++)
    {
        uint8_t v = (core && core->core) ? core->core->busRead8(core->core, addr + (uint32_t) i) : 0;
        lua_pushinteger(L, v);
        lua_seti(L, -2, i + 1);   // 1-based: the readers index raw_text[i+j] from 1
    }
    return 1;
}

static int LuaMemGetRegister(lua_State* L)
{
    // BizHawk name (uppercase, pairs allowed) -> mGBA lowercase name.
    const char* name = luaL_checkstring(L, 1);
    char lower[16];
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof(lower); i++)
        lower[i] = (char) tolower((unsigned char) name[i]);
    lower[i] = 0;
    lua_pushstring(L, lower);
    lua_replace(L, 1);
    // Reuse the emu path (self arg already replaced by the name).
    lua_getglobal(L, "emu");
    lua_getfield(L, -1, "readRegister");
    lua_insert(L, -2);   // [emu, fn] -> [fn, emu]
    lua_pushvalue(L, 1); // name
    lua_call(L, 2, 1);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushinteger(L, 0); }
    return 1;
}

// registerexec/registerwrite are emulated inside mgba_compat.lua itself
// (effect polling + real watchpoints); from the host side they only need to
// exist so the reader's calls land. They are installed here as traps that
// log, and the compat file overrides them with the real emulation.
static int LuaMemHookTrap(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    if (core && core->logCb) core->logCb("memory hook called before the compat shim installed", core->logUserdata);
    return 0;
}

static const luaL_Reg kMemoryFuncs[] = {
    {"readbyte", LuaMemRead8},
    {"readbyteunsigned", LuaMemRead8},
    {"readbytesigned", LuaMemReadS8},
    {"readword", LuaMemRead16},
    {"readwordsigned", LuaMemReadS16},
    {"readdword", LuaMemRead32},
    {"readdwordsigned", LuaMemReadS32},
    {"readbyterange", LuaMemReadRange},
    {"gbromreadbyte", LuaMemRead8},
    {"gbromreadword", LuaMemRead16},
    {"getregister", LuaMemGetRegister},
    {"registerexec", LuaMemHookTrap},
    {"registerwrite", LuaMemHookTrap},
    {nullptr, nullptr},
};

// oga_play_sound(path, pan, volume) -- the reader's positional cue, delivered to
// the host. oga_audio.lua installs this as its sink when present (see its
// set_sink), so a host with no sound callback simply never receives anything.
static int LuaPlaySound(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    const char* path = lua_tostring(L, 1);
    int pan = (int) luaL_optinteger(L, 2, 0);
    int volume = (int) luaL_optinteger(L, 3, 100);
    // Clamp here as well as in Lua: this is the boundary, and a host that pans by
    // an out-of-range value can produce a loud artefact rather than a wrong cue.
    if (pan < -100) pan = -100; else if (pan > 100) pan = 100;
    if (volume < 0) volume = 0; else if (volume > 100) volume = 100;
    if (core && core->soundCb && path) core->soundCb(path, pan, volume, core->soundUserdata);
    return 0;
}

// oga_set_exec_hook(address, fn) -> true when the host installed a REAL breakpoint.
// The shim calls this from memory.registerexec; a false return means the caller must fall back to
// the movement poll. See the long note above GbaDebuggerEntered for why a real hook matters.
static int LuaSetExecHook(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!core) { lua_pushboolean(L, 0); return 1; }

    // One registry ref per callback: the debugger resolves it back to a function when the
    // breakpoint fires.
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (!gba_set_exec_hook(core, address, ref))
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int LuaClearExecHook(lua_State* L)
{
    GbaCore* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    lua_pushboolean(L, core && gba_clear_exec_hook(core, address) ? 1 : 0);
    return 1;
}

static void InstallLibraries(GbaCore* core)
{
    lua_State* L = core->L;
    RegisterCore(L, core);

    lua_pushcfunction(L, LuaPrint);
    lua_setglobal(L, "print");

    // The host speech sink FIRST: the bootstrap preserves it across the shim
    // load, and tolk.output routes through it.
    lua_pushcfunction(L, LuaSay);
    lua_setglobal(L, "oga_say");

    // The host sound sink, beside the speech sink. oga_audio.lua reads this at load
    // time and forwards every audio.play cue to it (see CuePlayer on the app side).
    lua_pushcfunction(L, LuaPlaySound);
    lua_setglobal(L, "oga_play_sound");

    // The real exec-hook API. Registered here so the shim can turn
    // memory.registerexec into an actual mGBA breakpoint.
    lua_pushcfunction(L, LuaSetExecHook);
    lua_setglobal(L, "oga_set_exec_hook");
    lua_pushcfunction(L, LuaClearExecHook);
    lua_setglobal(L, "oga_clear_exec_hook");

    luaL_newlib(L, kEmuFuncs);
    lua_setglobal(L, "emu");

    luaL_newlib(L, kMemoryFuncs);
    lua_setglobal(L, "memory");

    // console.log -> reading log. input/callbacks are mGBA-owned userdata on
    // the real frontend; the shim never touches them except through its own
    // tables, but they must exist so the bootstrap's probes do not fail.
    lua_newtable(L);
    lua_pushcfunction(L, LuaConsoleLog);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, LuaConsoleLog);
    lua_setfield(L, -2, "error");
    lua_setglobal(L, "console");
    lua_newtable(L);
    lua_setglobal(L, "input");
    lua_newtable(L);
    lua_setglobal(L, "callbacks");

    // The reader set builds every data path with Windows backslashes
    // (scriptpath .. "game\\" .. game .. ...). Do NOT edit the scripts —
    // normalise at the boundary, in the host, before the bootstrap runs
    // (its own paths already use forward slashes, so this is a no-op for it).
    //
    // ⛔ NEVER FORWARD EXPLICIT NILS TO loadfile. loadfile decides whether to
    // keep the default _ENV by ARG ABSENCE (lua_isnone(L, 3)), not by value:
    // loadfile(path) keeps _G, but loadfile(path, nil, nil) SETS _ENV TO NIL,
    // and every file loaded that way dies with "index nil (upvalue '_ENV')".
    // So the wrapper re-spells the call with only the arguments it got.
    const char* normalize =
        "local function oga_fixpath(p)\n"
        "  if type(p) == 'string' then p = p:gsub('\\\\', '/') end\n"
        "  return p\n"
        "end\n"
        "local orig_loadfile = loadfile\n"
        "loadfile = function(p, m, e)\n"
        "  p = oga_fixpath(p)\n"
        "  if e ~= nil then return orig_loadfile(p, m, e)\n"
        "  elseif m ~= nil then return orig_loadfile(p, m)\n"
        "  else return orig_loadfile(p) end\n"
        "end\n"
        "local orig_dofile = dofile\n"
        "dofile = function(p) return orig_dofile(oga_fixpath(p)) end\n"
        "local orig_open = io.open\n"
        "io.open = function(p, m)\n"
        "  if m ~= nil then return orig_open(oga_fixpath(p), m)\n"
        "  else return orig_open(oga_fixpath(p)) end\n"
        "end\n";
    if (luaL_dostring(L, normalize) != LUA_OK)
    {
        if (core->logCb) core->logCb(lua_tostring(L, -1), core->logUserdata);
        lua_pop(L, 1);
    }
}

// Push the current hotkey set through the compat shim's oga_set_keys, so the
// reader's input.read() sees fresh edges. Called once per frame before the
// resume; a no-op until the shim defines it.
static void PushHotkeys(GbaCore* core)
{
    lua_State* L = core->L;
    if (!L) return;
    lua_getglobal(L, "oga_set_keys");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_createtable(L, (int) core->hotkeysDown.size(), 0);
    for (size_t i = 0; i < core->hotkeysDown.size(); i++)
    {
        char key[2] = {core->hotkeysDown[i], 0};
        lua_pushstring(L, key);
        lua_seti(L, -2, (lua_Integer) i + 1);
    }
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) lua_pop(L, 1);
}

// ------------------------------------------------------------------ lifecycle

GbaCore* gba_create(void)
{
    mLogSetDefaultLogger(nullptr);
    return new GbaCore();
}

void gba_destroy(GbaCore* core)
{
    if (!core) return;
    gba_stop(core);
    delete core;
}

void gba_set_speech_callback(GbaCore* core, GbaSpeechCallback cb, void* userdata)
{
    if (!core) return;
    core->speechCb = cb;
    core->speechUserdata = userdata;
}

void gba_set_log_callback(GbaCore* core, GbaLogCallback cb, void* userdata)
{
    if (!core) return;
    core->logCb = cb;
    core->logUserdata = userdata;
}

void gba_set_sound_callback(GbaCore* core, GbaSoundCallback cb, void* userdata)
{
    if (!core) return;
    core->soundCb = cb;
    core->soundUserdata = userdata;
}

void gba_set_script_dir(GbaCore* core, const char* dir)
{
    if (!core) return;
    core->scriptDir = dir ? dir : "";
}

static bool ReadHeader(const char* path, unsigned char* out, size_t n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(out, 1, n, f);
    fclose(f);
    return got == n;
}

bool gba_load_rom(GbaCore* core, const char* rom_path, const char* save_path,
                  char code_out[16], int* platform_out)
{
    if (!core || !rom_path) return false;
    core->error[0] = 0;
    if (code_out) code_out[0] = 0;

    unsigned char header[0xC0];
    if (!ReadHeader(rom_path, header, sizeof(header)))
    {
        SetError(core, "Could not open the game file.");
        return false;
    }

    mCore* mcore = mCoreFind(rom_path);
    if (!mcore)
    {
        SetError(core, "This game file is not a Game Boy or GBA ROM.");
        return false;
    }
    memset(core->rawFb, 0, sizeof(core->rawFb));
    mcore->init(mcore);
    // The video buffer is set further down, deliberately: mCoreLoadConfig
    // rebuilds the renderer and NULLs the buffer, so anything set here is
    // discarded. See the ordering note beside setVideoBuffer below.
    // Identify the cartridge from the file header (the same bytes mGBA
    // booted, so the code cannot disagree with the running game).
    enum mPlatform plat = mcore->platform(mcore);
    // mCoreFind keys off the extension; confirm GBA by its fixed header logo
    // region rather than trusting the filename. The Nintendo logo bitmap sits
    // at 0x04..0x9C; checking the first and last bytes is enough to reject a
    // misnamed file without embedding the whole bitmap.
    bool looksGba = (header[0x04] == 0xCE && header[0x05] == 0xED &&
                     header[0x9B] == 0x21 && header[0x9C] == 0x06);
    if (looksGba) plat = mPLATFORM_GBA;

    if (plat == mPLATFORM_GBA)
    {
        core->platform = GBA_PLATFORM_GBA;
        core->frameW = GBA_FB_W; core->frameH = GBA_FB_H;
        if (code_out)
        {
            for (int i = 0; i < 4; i++) code_out[i] = (char) header[0xAC + i];
            code_out[4] = 0;
        }
    }
    else if (plat == mPLATFORM_GB)
    {
        core->platform = GBA_PLATFORM_GB;
        core->frameW = GB_FB_W; core->frameH = GB_FB_H;
        if (code_out)
        {
            // GB/GBC titles carry a 16-byte title at 0x134, not a 4-char code;
            // the reader matches on it, so pass it through (space-padded).
            int n = 0;
            for (; n < 15 && header[0x134 + n] >= 0x20 && header[0x134 + n] < 0x7F; n++)
                code_out[n] = (char) header[0x134 + n];
            code_out[n] = 0;
        }
    }
    else
    {
        SetError(core, "This game file is not a Game Boy or GBA ROM.");
        return false;
    }
    if (platform_out) *platform_out = core->platform;

    memset(core->rawFb, 0, sizeof(core->rawFb));
    if (!mCoreLoadFile(mcore, rom_path))
    {
        SetError(core, "The game file could not be loaded.");
        return false;
    }
    // ⛔ THE ORDER OF THE NEXT FOUR CALLS IS LOAD-BEARING FOR GAME BOY ROMS.
    // mCoreLoadConfig reaches gb/core.c's reloadConfigOption, which calls
    // renderer->init() -- and the software renderer's init sets outputBuffer to
    // NULL. So the buffer must be set AFTER the config is loaded, and reset must
    // come after the buffer is set: a GB reset runs GBSkipBIOS -> GBIOWrite(LCDC)
    // -> _regenerateSGBBorder, which DRAWS through outputBuffer. Setting the buffer
    // before mCoreLoadConfig (as this did) left it NULL at reset, and an
    // SGB-enhanced .gb segfaulted on load. Measured on Pokemon Blue (SGB).
    mCoreConfigInit(&mcore->config, "oga");
    struct mCoreOptions opts;
    memset(&opts, 0, sizeof(opts));
    // ⛔ opts.volume IS THE MASTER VOLUME, AND ZERO MEANS SILENCE. memset leaves it 0, and
    // mGBA's _GBCoreLoadConfig / _GBACoreLoadConfig copy it straight into audio.masterVolume:
    //
    //     if (core->opts.mute) gb->audio.masterVolume = 0;
    //     else                 gb->audio.masterVolume = core->opts.volume;
    //
    // and every sample is multiplied by it on the way out:
    //
    //     sampleLeft = (sampleLeft * audio->masterVolume * 6) >> 7;
    //
    // So the emulator produced sound correctly and this host threw all of it away -- the ring
    // drained clean and held nothing but zeros. 0x100 = GB_AUDIO_VOLUME_MAX, the same default
    // GBAudioInit uses.
    opts.volume = 0x100;
    opts.mute = false;
    mCoreConfigLoadDefaults(&mcore->config, &opts);
    // ⛔ SGB BORDERS OFF. A Game Boy in SGB model mode renders 256x224 -- the
    // border is drawn INTO the frame -- and this frontend has no border surface
    // to put it in. mGBA's own Qt frontend forces this to 0 for the same reason
    // (CoreController.cpp). With it off the core reports 160x144 and the border
    // routine never runs. Without it, on top of the crash above, the frame would
    // not fit a 240x160 target at all.
    mCoreConfigSetIntValue(&mcore->config, "sgb.borders", 0);
    mCoreLoadConfig(mcore);

    // ⛔ THE BUFFER GOES BEFORE THE RESET -- and this call has MOVED to after
    // mCoreLoadConfig for that to be true. See the note above; the comment that
    // used to sit here was correct and was being defeated by the order.
    mcore->setVideoBuffer(mcore, core->rawFb, kFbStride);
    mcore->reset(mcore);

    if (save_path && save_path[0])
    {
        core->savePath = save_path;
        mCoreLoadSaveFile(mcore, save_path, false);
    }

    core->core = mcore;
    return true;
}

bool gba_start(GbaCore* core)
{
    if (!core || !core->core) { SetError(core, "Load a game first."); return false; }
    if (core->scriptDir.empty()) { SetError(core, "No reader script directory was set."); return false; }

    lua_State* L = luaL_newstate();
    if (!L) { SetError(core, "Could not create the script engine."); return false; }
    luaL_openlibs(L);
    core->L = L;
    InstallLibraries(core);

    // The bootstrap finds its siblings from its own path, so loading it by
    // absolute path is the whole install step. It ends in the reader's own
    // `while true do emu.frameadvance() end`, which yields back here through
    // emu.runFrame — hence the coroutine, same as the DS core.
    std::string boot = core->scriptDir + "/oga_bootstrap.lua";
    lua_State* co = lua_newthread(L);
    core->coroutine = co;
    if (luaL_loadfile(co, boot.c_str()) != LUA_OK)
    {
        SetError(core, "Reader load error: %s", lua_tostring(co, -1));
        return false;
    }
    int nresults = 0;   // Lua 5.4's resume writes through this; nullptr segfaults
    int rc = lua_resume(co, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        SetError(core, "Reader error: %s", lua_tostring(co, -1));
        return false;
    }
    core->scriptLoaded = true;
    core->running = true;
    return true;
}

void gba_stop(GbaCore* core)
{
    if (!core) return;
    // Silence the game as well as stopping it: samples staged but never consumed would play as a
    // burst of stale audio the moment a new game starts.
    core->audio.clear();
    core->audioRead = 0;
    core->audioFrac = 0.0;
    core->running = false;
    core->scriptLoaded = false;
    core->coroutine = nullptr;
    if (core->L) { lua_close(core->L); core->L = nullptr; }
    if (core->core) { core->core->deinit(core->core); core->core = nullptr; }
}

bool gba_running(GbaCore* core) { return core && core->running; }


// ------------------------------------------------------------- real exec hooks
//
// ⛔ WHY THIS EXISTS. pokemon.lua registers callbacks with
// memory.registerexec(address, fn), and the reader's TEXT and MENU hooks read CPU REGISTERS inside
// those callbacks (memory.getregister("r1") and friends) to find what the game was doing at that
// instruction. mGBA has no exec hook in its LUA API, so the shim approximated them by firing on
// player movement -- and a register read a frame late returns unrelated data, which the reader
// then speaks ("49154:58718", "4", spaces). Measured: 18 of gba.lua's 68 hook functions and 6 of
// rse.lua's 19 read a register; that is the set this fixes.
//
// mGBA the EMULATOR does have real breakpoints, and the debugger TUs are already in our build
// (ENABLE_DEBUGGERS is in MGBA_DEFS). They never fired because gba_frame() called
// core->runFrame(), and runFrame -> ARMRunLoop does not check breakpoints; only
// mDebuggerRunFrame() does. Measured with tools/gba-debug-hook-spike.c: the breakpoint fires at
// the exact PC, registers are correct at that instant, and the cost is ~2.2x baseline
// (0.33 ms/frame vs 0.15) -- still ~50x faster than real time.
//
// ⛔ THE CALLBACK MUST CLEAR isPaused. mDebuggerEnter sets module->isPaused = true BEFORE calling
// this, then mDebuggerUpdatePaused moves the debugger to DEBUGGER_PAUSED, where
// mDebuggerRunTimeout waits on a timeout instead of executing -- the run stops dead at the first
// breakpoint with no output and no error. mGBA's own scripting layer clears it at the end of
// _scriptDebuggerEntered for exactly this reason.

// The module is embedded in GbaCore, so recover the owner without a side table.
static GbaCore* CoreFromModule(struct mDebuggerModule* m)
{
    return reinterpret_cast<GbaCore*>(reinterpret_cast<char*>(m) - offsetof(GbaCore, dbgModule));
}

static void GbaDebuggerEntered(struct mDebuggerModule* module, enum mDebuggerEntryReason reason,
                               struct mDebuggerEntryInfo* info)
{
    GbaCore* core = CoreFromModule(module);
    // ⛔ CLEAR isPaused FIRST, before any Lua runs: a callback that errors must not leave the
    // debugger paused, or the next frame blocks forever.
    module->isPaused = false;
    if (reason != DEBUGGER_ENTER_BREAKPOINT || !core || !core->L || !info) return;

    auto it = core->execHooks.find((uint32_t) info->address);
    if (it == core->execHooks.end()) return;

    // Call on the MAIN state, not the reader coroutine: the coroutine is suspended (yielded at
    // frameadvance) and its stack is mid-call, while L is idle. The registry is shared, so the
    // stored ref resolves either way.
    lua_State* L = core->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        if (core->logCb) core->logCb(err ? err : "exec hook error", core->logUserdata);
        lua_pop(L, 1);
    }
}

// Attach the debugger on first use. Returns false if this build has no debugger platform.
static bool EnsureDebugger(GbaCore* core)
{
    if (core->dbgAttached) return true;
    if (!core->core || !core->core->debuggerPlatform) return false;

    mDebuggerInit(&core->debugger);
    mDebuggerAttach(&core->debugger, core->core);   // sets debugger.platform itself
    memset(&core->dbgModule, 0, sizeof(core->dbgModule));
    core->dbgModule.type = DEBUGGER_CUSTOM;
    core->dbgModule.entered = GbaDebuggerEntered;
    core->dbgModule.isPaused = false;
    mDebuggerAttachModule(&core->debugger, &core->dbgModule);
    core->dbgAttached = true;
    return true;
}

// Install (or replace) a real breakpoint at `address`. Returns false if unsupported.
static bool InstallExecHook(GbaCore* core, uint32_t address, int luaRef)
{
    if (!EnsureDebugger(core)) return false;
    struct mDebuggerPlatform* plat = core->debugger.platform;
    if (!plat || !plat->setBreakpoint) return false;

    // Replace an existing one at this address.
    auto old = core->hookIds.find(address);
    if (old != core->hookIds.end())
    {
        if (plat->clearBreakpoint) plat->clearBreakpoint(plat, old->second);
        core->hookIds.erase(old);
        auto oldRef = core->execHooks.find(address);
        if (oldRef != core->execHooks.end())
        {
            if (core->L) luaL_unref(core->L, LUA_REGISTRYINDEX, oldRef->second);
            core->execHooks.erase(oldRef);
        }
    }

    struct mBreakpoint bp;
    memset(&bp, 0, sizeof(bp));
    bp.address = address;
    bp.segment = -1;
    bp.type = BREAKPOINT_HARDWARE;
    ssize_t id = plat->setBreakpoint(plat, &core->dbgModule, &bp);

    core->execHooks[address] = luaRef;
    core->hookIds[address] = id;
    return true;
}

bool gba_set_exec_hook(GbaCore* core, uint32_t address, int lua_ref)
{
    if (!core) return false;
    return InstallExecHook(core, address, lua_ref);
}

bool gba_clear_exec_hook(GbaCore* core, uint32_t address)
{
    if (!core) return false;
    auto it = core->execHooks.find(address);
    if (it == core->execHooks.end()) return false;
    if (core->L) luaL_unref(core->L, LUA_REGISTRYINDEX, it->second);
    core->execHooks.erase(it);
    auto id = core->hookIds.find(address);
    if (id != core->hookIds.end())
    {
        struct mDebuggerPlatform* plat = core->debugger.platform;
        if (core->dbgAttached && plat && plat->clearBreakpoint) plat->clearBreakpoint(plat, id->second);
        core->hookIds.erase(id);
    }
    return true;
}

bool gba_hooks_active(GbaCore* core)
{
    return core && !core->execHooks.empty();
}

// The ring rate the app outputs at: the same 32768 Hz the PSP path uses, so the Swift side's
// AVAudioEngine format needs no per-console change.
static const unsigned kGbaAppRate = 32768;

// Pull whatever the console has produced into `core->audio`, resampled to kGbaAppRate.
// Cheap and silent when the console produced nothing or the ring is unavailable.
static void GbaDrainAudio(GbaCore* core)
{
    if (!core || !core->core) return;
    if (!core->core->getAudioBuffer || !core->core->audioSampleRate) return;

    struct mAudioBuffer* ring = core->core->getAudioBuffer(core->core);
    if (!ring) return;
    size_t avail = mAudioBufferAvailable(ring);
    if (avail == 0) return;

    // The console's own rate. Guard against a zero before dividing by it.
    unsigned srcRate = core->core->audioSampleRate(core->core);
    if (srcRate == 0) return;

    // Bounded by the ring: never let a long pause balloon one read.
    if (avail > 8192) avail = 8192;
    static thread_local std::vector<int16_t> tmp;
    tmp.resize(avail * 2);
    size_t got = mAudioBufferRead(ring, tmp.data(), avail);
    if (got == 0) return;

    // Linear resample srcRate -> kGbaAppRate, carrying the phase across calls.
    const double step = (double) srcRate / (double) kGbaAppRate;
    double pos = core->audioFrac;
    while (pos < (double) (got - 1)) {
        size_t i = (size_t) pos;
        double frac = pos - (double) i;
        int16_t l0 = tmp[i * 2],     r0 = tmp[i * 2 + 1];
        int16_t l1 = tmp[(i + 1) * 2], r1 = tmp[(i + 1) * 2 + 1];
        core->audio.push_back((int16_t) ((double) l0 + ((double) l1 - (double) l0) * frac));
        core->audio.push_back((int16_t) ((double) r0 + ((double) r1 - (double) r0) * frac));
        pos += step;
    }
    core->audioFrac = pos - (double) got;   // carry the surplus into the next read
    if (core->audioFrac < 0.0) core->audioFrac = 0.0;
    core->audioPrimed = true;
}

// Drain the console's audio ring and hand back interleaved stereo s16 at kGbaAppRate.
// Returns the number of FRAMES written, 0 when there is nothing to give.
int gba_read_audio(GbaCore* core, int16_t* out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    GbaDrainAudio(core);

    size_t have = core->audio.size() / 2 - core->audioRead;
    if (have == 0) return 0;
    if (have > (size_t) max_frames) have = (size_t) max_frames;

    memcpy(out, core->audio.data() + core->audioRead * 2, have * 2 * sizeof(int16_t));
    core->audioRead += have;

    // Compact only when fully drained: memmove-ing every call would be the cost, and a partial
    // drain cannot be compacted without moving data the reader has not taken yet.
    if (core->audioRead * 2 >= core->audio.size()) {
        core->audio.clear();
        core->audioRead = 0;
    }
    return (int) have;
}

bool gba_frame(GbaCore* core)
{
    if (!core || !core->core || !core->running) return false;

    if (core->core->setKeys) core->core->setKeys(core->core, core->buttonsDown);
    // ⛔ THE FRAME DRIVER DEPENDS ON WHETHER HOOKS EXIST. Breakpoints are only checked by
    // mDebuggerRunFrame; a game with no exec hooks keeps the cheaper plain runFrame, so this
    // change costs nothing for the vast majority of titles.
    if (!core->execHooks.empty() && core->dbgAttached)
        mDebuggerRunFrame(&core->debugger);
    else
        core->core->runFrame(core->core);
    core->frameCounter++;

    if (core->scriptLoaded && core->coroutine)
    {
        if (core->hotkeysDirty) { PushHotkeys(core); core->hotkeysDirty = false; }
        int nresults = 0;
        int rc = lua_resume(core->coroutine, nullptr, 0, &nresults);
        if (rc != LUA_OK && rc != LUA_YIELD)
        {
            const char* err = lua_tostring(core->coroutine, -1);
            if (core->logCb) core->logCb(err ? err : "reader error", core->logUserdata);
            core->scriptLoaded = false;
        }
    }

    // Battery-backed SRAM write-through, same cadence as the DS core's flush.
    if (core->frameCounter % 600 == 0 && !core->savePath.empty() && core->core->savedataClone)
    {
        void* sram = nullptr;
        size_t size = core->core->savedataClone(core->core, &sram);
        if (sram && size)
        {
            FILE* f = fopen(core->savePath.c_str(), "wb");
            if (f) { fwrite(sram, 1, size, f); fclose(f); }
            free(sram);
        }
    }
    return true;
}

// ------------------------------------------------------------------ display

bool gba_framebuffer(GbaCore* core, int* width, int* height)
{
    if (width) *width = core ? core->frameW : GBA_FB_W;
    if (height) *height = core ? core->frameH : GBA_FB_H;
    if (!core || !core->core) return false;

    size_t n = (size_t) core->frameW * (size_t) core->frameH;
    if (core->frameRGBA.size() != n * 4) core->frameRGBA.resize(n * 4);
    uint8_t* dst = core->frameRGBA.data();
    // ⛔ ROW BY ROW, NOT ONE FLAT RUN. The render target is a fixed 256-wide
    // array and a GBA row is 240, so the live frame is only contiguous for a
    // 160-wide GB. A flat copy of frameW*frameH would read the correct first
    // row and then walk diagonal garbage through the rest of the image.
    const size_t stride = (size_t) kFbStride;
    // mGBA's pixels are XBGR8 (0x00BBGGRR); the app wants RGBA8888 solid.
    for (size_t y = 0; y < (size_t) core->frameH; y++)
    for (size_t x = 0; x < (size_t) core->frameW; x++)
    {
        size_t i = y * stride + x;
        uint32_t p = core->rawFb[i];
        size_t o = (y * (size_t) core->frameW + x) * 4;
        dst[o + 0] = (uint8_t) (p & 0xFF);
        dst[o + 1] = (uint8_t) ((p >> 8) & 0xFF);
        dst[o + 2] = (uint8_t) ((p >> 16) & 0xFF);
        dst[o + 3] = 0xFF;
    }
    core->frameScreen = 0;
    return true;
}

const uint8_t* gba_framebuffer_ptr(GbaCore* core)
{
    if (!core || core->frameRGBA.empty()) return nullptr;
    return core->frameRGBA.data();
}

// -------------------------------------------------------------------- input

void gba_set_button(GbaCore* core, int gba_button, bool down)
{
    if (!core || gba_button < 0 || gba_button >= GBA_BTN_COUNT) return;
    if (down) core->buttonsDown |= (1u << gba_button);
    else core->buttonsDown &= ~(1u << gba_button);
}

void gba_set_hotkey(GbaCore* core, const char* key, bool down)
{
    if (!core || !key || !*key) return;
    char k = key[0];
    auto it = std::find(core->hotkeysDown.begin(), core->hotkeysDown.end(), k);
    if (down && it == core->hotkeysDown.end()) core->hotkeysDown.push_back(k);
    else if (!down && it != core->hotkeysDown.end()) core->hotkeysDown.erase(it);
    core->hotkeysDirty = true;
}

// ------------------------------------------------------------------ states

bool gba_save_state(GbaCore* core, const char* path)
{
    if (!core || !core->core || !path) return false;
    // ⛔ O_RDWR, NOT O_WRONLY. THIS IS WHY SAVE STATES WERE SILENTLY EMPTY.
    //
    // mGBA writes a savestate by MAPPING the file: serialize.c does
    //     vf->truncate(vf, stateSize);
    //     void* state = vf->map(vf, stateSize, MAP_WRITE);
    //     core->saveState(core, state);
    // and its fd VFS implements map() as
    //     mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
    // POSIX requires the descriptor to be open for READING as well as writing for
    // a MAP_SHARED PROT_WRITE mapping. With O_WRONLY the mmap fails with EACCES,
    // map() returns NULL, mCoreSaveStateNamed returns false — and the truncated
    // file is left as a full-size block of ZEROS. Measured directly:
    //     O_WRONLY|O_CREAT|O_TRUNC  -> mmap FAILED: Permission denied
    //     O_RDWR  |O_CREAT|O_TRUNC  -> mmap ok
    // O_RDWR satisfies both the write and the read half, and still truncates.
    struct VFile* vf = VFileOpen(path, O_RDWR | O_CREAT | O_TRUNC);
    if (!vf) { SetError(core, "Could not write the saved state."); return false; }
    bool ok = mCoreSaveStateNamed(core->core, vf, 0);
    vf->close(vf);
    if (!ok) SetError(core, "Could not save the game state.");
    return ok;
}

bool gba_load_state(GbaCore* core, const char* path)
{
    if (!core || !core->core || !path) return false;
    struct VFile* vf = VFileOpen(path, O_RDONLY);
    if (!vf) { SetError(core, "No saved state found."); return false; }
    bool ok = mCoreLoadStateNamed(core->core, vf, 0);
    vf->close(vf);
    if (!ok) SetError(core, "The saved state could not be loaded.");
    return ok;
}

unsigned long long gba_frames_completed(GbaCore *core)
{
    return core ? core->frameCounter : 0ULL;
}

// Host-only: raw bus read so a test can compare the binding against the file.
uint32_t gba_debug_read(GbaCore *core, uint32_t addr, int width)
{
    if (!core || !core->core) return 0;
    if (width == 1) return core->core->busRead8(core->core, addr);
    if (width == 2) return core->core->busRead16(core->core, addr);
    return core->core->busRead32(core->core, addr);
}

const char* gba_last_error(GbaCore* core)
{
    return core ? core->error : "";
}
