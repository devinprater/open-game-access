/*
    MGBACore.cpp — the Lua accessibility host for mGBA (Game Boy / GBC / GBA).

    This is the Game Boy / GBA twin of PokeScript.cpp. It hosts Lua 5.4,
    installs a VisualBoyAdvance-flavoured API surface on top of libmgba, and
    runs Pokémon Access v3.1.0's pokemon.lua UNMODIFIED inside a coroutine
    resumed once per emulated frame.

    ⛔ pokemon.lua / gb.lua / gba.lua ARE NEVER EDITED. Everything Windows-only
    is neutralised here, in the host:

      * tolk.lua does LuaJIT FFI on Tolk.dll          -> this host installs the
        `tolk` global itself and never loads the script's tolk.lua.
      * audio.dll / bass.dll via package.loadlib        -> `audio` global here.
      * win-controls.dll dialogs (hack-ROM setup only)  -> `controls` global here.
      * encoding.lua / crc32.lua via LuaJIT FFI         -> `require` overridden
        for those two names, so `local crc32 = require "crc32"` inside
        get_game_checksum() resolves to a host implementation.
      * `ffi` itself                                    -> never required.
      * BACKSLASH paths ("game\\emerald\\en\\")         -> NormalizePath(), and
        loadfile / io.open / audio.play / require all normalise first.
      * io.popen("dir /b ...")                          -> stub io.popen.
      * `bit` and `unpack` (LuaJIT / 5.1 globals)       -> installed here.

    The one core feature that is not a pure function translation is
    memory.registerexec. VBA fires a Lua callback when execution reaches an
    address; mGBA has no script engine in this build. It does have a debugger
    with hardware breakpoints, and — this is the load-bearing part — when a
    core has a debugger attached and `platform->hasBreakpoints()` is true, the
    frontend is expected to drive the core with step()+checkBreakpoints()
    instead of runLoop(). This host therefore owns an mDebugger, and MGBARunner
    switches the frame loop over whenever registerexec has live callbacks.
    Without that, gba.lua's whole graphics/text pipeline (draw_text_to_tile_buffer,
    copy_window_to_vram, the menu-cursor readers) never runs, because all of it
    is ROM-address callbacks.

    The GB path needs none of that: gb.lua reads the tilemap and text RAM
    directly, and its own while-loop in get_rom_table is bounded by a 0xFF
    sentinel inside the ROM.

    ⛔ TWO Lua 5.4 TRAPS (same as PokeScript.cpp — do not "simplify"):
      1. lua_resume's 4th argument must be a REAL pointer; it writes the result
         count through it. nullptr segfaults inside ldo.c on the first resume.
      2. A chunk has a 200-local ceiling. pokemon.lua is not near it (unlike the
         NDS main.lua), but every host helper here still lives behind a global
         or a registry handle rather than adding chunk locals to the script.
*/

#include "MGBACore.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

extern "C" {
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>
#include <mgba/debugger/debugger.h>
}

namespace MelonDSAndroid
{

// ------------------------------------------------------------------ helpers

std::string MGBAScript::normalizePath(const std::string& in) const
{
    std::string s = in;
    // debug.getinfo names arrive with a leading '@'.
    if (!s.empty() && s[0] == '@') s.erase(0, 1);
    // Windows separators -> POSIX. This is the whole point: the script builds
    // every data path with '\'.
    for (char& c : s) if (c == '\\') c = '/';
    // The script builds paths by concatenation and can leave doubled
    // separators ("game//emerald//en/").
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(s[i]);
    }
    // Relative paths are relative to the script directory, exactly as they
    // would be on Windows where the emulator's cwd is the lua folder.
    if (!out.empty() && out[0] != '/')
        out = scriptDir + out;
    return out;
}

// The host instance reachable from any lua_State (the script coroutine
// included) through the registry. Mirrors PokeScript's CoreFromLua.
static MGBAScript* gScript = nullptr;

static void RegisterCore(lua_State* L, MGBAScript* core)
{
    lua_pushlightuserdata(L, (void*) &gScript);
    lua_pushlightuserdata(L, core);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

static MGBAScript* CoreFromLua(lua_State* L)
{
    lua_pushlightuserdata(L, (void*) &gScript);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* core = static_cast<MGBAScript*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return core;
}

// --------------------------------------------------------------- memory API
//
// The script's two families, and the mGBA call each maps to:
//
//   memory.readbyte / readbyteunsigned / readword / readdword / readbyterange /
//   readbytesigned / readdwordsigned
//       -> core->busRead8/16/32, i.e. the CPU bus (VBA's `readbyte` semantics:
//          ROM banks, VRAM, IWRAM, I/O as the game itself sees them).
//
//   memory.gbromreadbyte   (GB only: ROM header + the ROM tables rby.lua walks)
//       -> a flat read of the cart image. The CPU bus cannot serve these: bank 1
//          of a GB cart is only reachable through the MBC, and rby.lua reads
//          banks well past the 16 KiB window.

static uint32_t BusRead(mCore* core, uint32_t address, int width)
{
    if (!core) return 0;
    switch (width)
    {
    case 1: return core->busRead8(core, address);
    case 2: return core->busRead16(core, address);
    case 4: return core->busRead32(core, address);
    }
    return 0;
}

uint8_t MGBAScript::romByte(uint32_t address) const
{
    if (!romBase || address >= romSize) return 0xFF;
    return romBase[address];
}

void MGBAScript::refreshMemoryDomains()
{
    romBase = nullptr;
    romSize = 0;
    if (!corePtr) return;
    size_t size = 0;
    // 0x08000000 is the GBA cart base; on GB the cart is a flat block starting
    // at 0. Ask for both and keep whichever the core answers for.
    for (uint32_t base : { (uint32_t) 0x08000000u, (uint32_t) 0x00000000u })
    {
        void* block = mCoreGetMemoryBlock(corePtr, base, &size);
        if (block && size)
        {
            romBase = static_cast<const uint8_t*>(block);
            romSize = size;
            return;
        }
    }
}

template <int Width, bool Signed>
static int LuaMemoryRead(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    uint64_t value = BusRead(core ? core->core() : nullptr, address, Width);
    if (Width == 1)
        value &= 0xFF;
    else if (Width == 2)
        value &= 0xFFFF;
    else
        value &= 0xFFFFFFFFu;

    if (Signed)
    {
        if (Width == 1) lua_pushinteger(L, (int8_t) value);
        else if (Width == 2) lua_pushinteger(L, (int16_t) value);
        else lua_pushinteger(L, (int32_t) value);
    }
    else
    {
        lua_pushinteger(L, (lua_Integer) value);
    }
    return 1;
}

// readbyterange(a, n) -> 1-based table, matching VBA.
static int LuaReadByteRange(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    int count = (int) luaL_checkinteger(L, 2);
    if (count < 0) count = 0;
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i)
    {
        lua_pushinteger(L, BusRead(core ? core->core() : nullptr, address + i, 1) & 0xFF);
        lua_seti(L, -2, i + 1);
    }
    return 1;
}

// memory.gbromreadbyte(a) — the cart image, addressed flat.
static int LuaGBRomReadByte(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    lua_pushinteger(L, core ? core->romByte(address) : 0xFF);
    return 1;
}

static int LuaGBRomReadWord(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);
    uint32_t lo = core ? core->romByte(address) : 0xFF;
    uint32_t hi = core ? core->romByte(address + 1) : 0xFF;
    lua_pushinteger(L, lo | (hi << 8));
    return 1;
}

// ------------------------------------------------------------ CPU registers
//
// gba.lua's ROM-callback readers peek at r0..r3/r13/r15. mGBA exposes the name
// table through core->readRegister.

static int LuaGetRegister(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* name = luaL_checkstring(L, 1);
    if (!core || !core->core() || !name)
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    int32_t value = 0;
    if (core->core()->readRegister(core->core(), name, &value))
        lua_pushinteger(L, value);
    else
        lua_pushinteger(L, 0);
    return 1;
}

// ---------------------------------------------------------- registerexec / hooks

static int LuaRegisterExec(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    if (!core) return 0;
    if (lua_isnoneornil(L, 1)) return 0;
    uint32_t address = (uint32_t) luaL_checkinteger(L, 1);

    if (lua_isnoneornil(L, 2))
    {
        core->clearExecCallback(address);
        return 0;
    }

    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    core->addExecCallback(address, ref);
    return 0;
}

static int LuaRegisterWrite(lua_State* L)
{
    // memory.registerwrite has no mGBA equivalent in this build. It is
    // accepted-and-ignored: gba.lua uses it only to track VRAM DMA copies.
    // Ignoring it costs some menu/graphics text, but a raise here would kill
    // the whole reader (see the "one unguarded error ends the session" note).
    MGBAScript* core = CoreFromLua(L);
    if (core) core->log("[pokemon-access] memory.registerwrite is not supported on mGBA; ignored");
    return 0;
}

// ------------------------------------------------------------------ emu / misc

static int LuaFrameAdvance(lua_State* L)
{
    // The same trick as the NDS side: frameadvance() yields the script's
    // coroutine and the frame loop resumes it, so pokemon.lua's
    // `while true do emu.frameadvance() ... end` stays byte-identical.
    return lua_yield(L, 0);
}

static int LuaPlatform(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    mCore* c = core ? core->core() : nullptr;
    // VBA's emu.platform(): 0 = GBA, 1 = GB. pokemon.lua's get_device() reads
    // exactly those values, and they happen to be mPLATFORM_GBA/mPLATFORM_GB.
    int platform = 0;
    if (c && c->platform) platform = (int) c->platform(c);
    else if (c == nullptr) platform = -1;
    lua_pushinteger(L, platform);
    return 1;
}

static int LuaFramecount(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    lua_pushinteger(L, core ? (lua_Integer) core->framesRun() : 0);
    return 1;
}

static int LuaNoop(lua_State* L) { (void) L; return 0; }

static int LuaPrint(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i)
    {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s) { out.append(s, len); lua_pop(L, 1); }
    }
    if (core) core->log(out.c_str());
    return 0;
}

// ------------------------------------------------------------------- speech
//
// tolk.output / tolk.silence are the two names pokemon.lua calls. The Windows
// tolk.lua (LuaJIT FFI on Tolk.dll) is never loaded.

static int LuaSpeak(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* text = luaL_checkstring(L, 1);
    if (core && text) core->speak(text, true);
    return 0;
}

static int LuaSilence(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    if (core) core->stopSpeech();
    return 0;
}

// -------------------------------------------------------------------- audio
//
// audio.play(path, loops, pan, vol) / audio.stop(handle) / audio.pitch(freq, h)
// The 33 cue WAVs are 8- or 16-bit PCM; only play/stop matter on the frame
// path, so play forwards to the platform and pitch is a no-op stub (it is
// called once per frame by the Berry Blender reader only).

static int LuaAudioPlay(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* path = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
    int pan = lua_isnumber(L, 3) ? (int) lua_tointeger(L, 3) : 0;
    int vol = lua_isnumber(L, 4) ? (int) lua_tointeger(L, 4) : 50;
    if (core && path) core->playSound(path, pan, vol);
    // A non-nil handle, because rse.lua stores it and later calls
    // audio.pitch(..., handle.handle) and audio.stop(handle).
    lua_pushinteger(L, 1);
    return 1;
}

static int LuaAudioStop(lua_State* L)
{
    (void) L;
    return 0;
}

// --------------------------------------------------------------- win-controls
//
// The only users are the hack-ROM setup dialogs (base game / expansion / data
// folder / language). On Android there is no dialog to show before the script
// has even loaded, so both return nil: pokemon.lua treats a nil result as
// "user cancelled" and returns early. Normal gameplay never calls them.

static int LuaCancelDialog(lua_State* L)
{
    (void) L;
    lua_pushnil(L);
    return 1;
}

// ----------------------------------------------------- host file/require layer

static int LuaHostDir(lua_State* L)
{
    (void) L;
    lua_pushnil(L);
    return 1;
}

// loadfile(path) -> function or nil, with the path normalised. Returns nil
// (never raises) on a missing file, which is what the script checks for.
static int LuaHostLoadFile(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* raw = luaL_checkstring(L, 1);
    std::string path = core ? core->normalizePath(raw) : std::string(raw);
    if (luaL_loadfile(L, path.c_str()) != LUA_OK)
    {
        // nil, error-message — matching loadfile's own contract. pokemon.lua
        // deliberately loadfile()s files that may not exist (game/<game>/main.lua
        // only exists for the GB titles), so this is not an error, only a trail.
        if (core)
        {
            std::string message = std::string("[pokemon-access] optional loadfile skipped: ") + path;
            core->log(message.c_str());
        }
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

static int LuaHostOpen(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* raw = luaL_checkstring(L, 1);
    const char* mode = lua_isstring(L, 2) ? lua_tostring(L, 2) : "r";
    std::string path = core ? core->normalizePath(raw) : std::string(raw);
    lua_getglobal(L, "io");
    lua_getfield(L, -1, "open_raw");
    lua_pushstring(L, path.c_str());
    lua_pushstring(L, mode);
    lua_call(L, 2, 1);
    lua_remove(L, -2);
    return 1;
}

// io.popen("dir /b ..."): pokemon.lua's list_files/list_directories/
// get_game_expansions call it. It is on the hack-ROM config path only. A nil
// handle would make `for filename in pfile:lines()` raise, so return a handle
// whose :lines() yields nothing and whose :close() does nothing.
static int LuaHostPopen(lua_State* L)
{
    (void) L;
    lua_newtable(L);
    lua_pushcfunction(L, LuaHostDir);           // empty iterator
    lua_setfield(L, -2, "lines");
    lua_pushcfunction(L, LuaNoop);              // close()
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, LuaNoop);              // read()
    lua_setfield(L, -2, "read");
    return 1;
}

// ------------------------------------------------------------------ bit ops
// bit.band / bor / bnot / lshift / rshift — the script's only five.

static int LuaBitOp(lua_State* L)
{
    int op = (int) lua_tointeger(L, lua_upvalueindex(1));
    uint32_t a = (uint32_t) luaL_checkinteger(L, 1);
    uint32_t b = lua_isnoneornil(L, 2) ? 0 : (uint32_t) luaL_checkinteger(L, 2);
    switch (op)
    {
    case 0: lua_pushinteger(L, (lua_Integer) (a & b)); break;
    case 1: lua_pushinteger(L, (lua_Integer) (a | b)); break;
    case 2: lua_pushinteger(L, (lua_Integer) (a ^ b)); break;
    case 3: lua_pushinteger(L, (lua_Integer) (~a & 0xFFFFFFFFu)); break;
    case 4: lua_pushinteger(L, (lua_Integer) (a << (b & 31))); break;
    default: lua_pushinteger(L, (lua_Integer) (a >> (b & 31))); break;
    }
    return 1;
}

// crc32.crc32(data, crc) — LuaJIT FFI on crc32.dll in the original. zlib's
// polynomial, table-free bitwise implementation is plenty for game detection.
static uint32_t Crc32Bytes(const uint8_t* data, size_t len, uint32_t crc)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t) (-(int32_t) (crc & 1)));
    }
    return ~crc;
}

static int LuaCrc32(lua_State* L)
{
    uint32_t crc = lua_isnoneornil(L, 2) ? 0 : (uint32_t) luaL_checkinteger(L, 2);
    if (lua_istable(L, 1))
    {
        size_t n = (size_t) luaL_len(L, 1);
        std::vector<uint8_t> bytes(n);
        for (size_t i = 0; i < n; ++i)
        {
            lua_geti(L, 1, (lua_Integer) i + 1);
            bytes[i] = (uint8_t) lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        lua_pushinteger(L, (lua_Integer) Crc32Bytes(bytes.data(), bytes.size(), crc));
        return 1;
    }
    if (lua_isstring(L, 1))
    {
        size_t n = 0;
        const char* s = lua_tolstring(L, 1, &n);
        lua_pushinteger(L, (lua_Integer) Crc32Bytes((const uint8_t*) s, n, crc));
        return 1;
    }
    lua_pushinteger(L, 0);
    return 1;
}

// require(): the script's four imports (a-star, serpent, message, win-controls)
// are files next to pokemon.lua. Rather than relying on package.path — which
// the script itself mutates expectations about — resolve them out of the
// script directory, and answer the two Windows-FFI modules from the host.
static const luaL_Reg kBuiltinModules[] = {
    { "bit", nullptr },     // handled specially below
    { nullptr, nullptr }
};

// input.read(): a table keyed by held letter names. A named function rather
// than a lambda, because lua_pushcfunction is a macro and a braced initialiser
// list inside its argument needs parentheses the compiler will not guess.
static int LuaInputRead(lua_State* L)
{
    MGBAScript* c = CoreFromLua(L);
    static const char* names[] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
    };
    lua_newtable(L);
    if (c)
    {
        for (int code = 'A'; code <= 'Z'; ++code)
        {
            if (!c->isHotkeyHeld((char) code)) continue;
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, names[code - 'A']);
        }
    }
    return 1;
}

// Lua 5.1's unpack, which table.unpack replaced.
static int LuaUnpack(lua_State* L)
{
    int n = (int) luaL_len(L, 1);
    int i = lua_isnoneornil(L, 2) ? 1 : (int) luaL_checkinteger(L, 2);
    int j = lua_isnoneornil(L, 3) ? n : (int) luaL_checkinteger(L, 3);
    int count = j - i + 1;
    if (count < 0) count = 0;
    luaL_checkstack(L, count, "unpack");
    for (int k = 0; k < count; ++k)
        lua_geti(L, 1, i + k);
    return count;
}

// encoding.to_utf8 / to_utf16, answered from the host instead of kernel32.
static int LuaIdentity(lua_State* L)
{
    lua_pushvalue(L, 1);
    return 1;
}

// The value luaopen_audio returns in the host: a function that yields the
// already-installed `audio` table, so
// `assert(package.loadlib("audio.dll","luaopen_audio"))()` works unchanged.
static int LuaAudioOpener(lua_State* L)
{
    lua_getglobal(L, "audio");
    return 1;
}

// package.loadlib(path, symbol): no shared libraries are loaded on Android, and
// none are needed — the only call site wants the audio module. Returns a
// "loader" function, exactly like the real loadlib on success, so the script's
// `assert(...)()` chain returns the table instead of tripping the assert.
static int LuaLoadLib(lua_State* L)
{
    (void) L;
    lua_pushcfunction(L, LuaAudioOpener);
    return 1;
}

static int LuaHostRequire(lua_State* L)
{
    MGBAScript* core = CoreFromLua(L);
    const char* name = luaL_checkstring(L, 1);
    if (!core)
    {
        lua_pushnil(L);
        return 1;
    }

    // Already loaded?
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1))
    {
        lua_remove(L, -2);   // _LOADED
        return 1;
    }
    lua_pop(L, 1);           // nil
    lua_pop(L, 1);           // _LOADED

    // The two modules the Windows build answered from a DLL, plus the two
    // script files that exist only to FFI into one — win-controls.lua (the
    // hack-ROM dialogs) and tolk.lua (speech). Neither is bundled, and neither
    // is needed: pokemon.lua just wants a table back for those names.
    if (std::strcmp(name, "crc32") == 0 || std::strcmp(name, "encoding") == 0 ||
        std::strcmp(name, "win-controls") == 0 || std::strcmp(name, "tolk") == 0)
    {
        lua_newtable(L);
        if (std::strcmp(name, "crc32") == 0)
        {
            lua_pushcfunction(L, LuaCrc32);
            lua_setfield(L, -2, "crc32");
        }
        else if (std::strcmp(name, "encoding") == 0)
        {
            // encoding.to_utf8 / to_utf16 are only used to talk to Windows APIs
            // (Tolk, the dialogs). Identity is the correct answer on Android.
            lua_pushcfunction(L, LuaIdentity);
            lua_setfield(L, -2, "to_utf8");
            lua_pushcfunction(L, LuaIdentity);
            lua_setfield(L, -2, "to_utf16");
        }
        else if (std::strcmp(name, "win-controls") == 0)
        {
            lua_pushcfunction(L, LuaCancelDialog);
            lua_setfield(L, -2, "inputbox");
            lua_pushcfunction(L, LuaCancelDialog);
            lua_setfield(L, -2, "combobox");
        }
        else  // tolk
        {
            lua_getglobal(L, "tolk");
            lua_pushvalue(L, -1);
            lua_remove(L, -3);   // drop the fresh table; use the host's
        }
        // Cache it like a real require would.
        std::string key = name;
        lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, key.c_str());
        lua_pop(L, 1);
        return 1;
    }

    // Otherwise load <scriptdir>/<name>.lua.
    std::string path = core->normalizePath(std::string(name) + ".lua");
    if (luaL_loadfile(L, path.c_str()) != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        std::string message = "[pokemon-access] require failed: " + std::string(name) +
                              " -> " + path + " (" + (err ? err : "?") + ")";
        core->log(message.c_str());
        return lua_error(L);
    }
    lua_call(L, 0, 1);

    std::string key = name;
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, key.c_str());
    lua_pop(L, 1);
    return 1;
}

// ============================================================ public methods

MGBAScript::MGBAScript()
{
    gScript = this;
}

MGBAScript::~MGBAScript()
{
    stop();
    if (gScript == this) gScript = nullptr;
}

void MGBAScript::setSpeechCallback(PokeSpeechCallback cb, void* userdata)
{
    speechCb = cb;
    speechUserdata = userdata;
}

void MGBAScript::setLogCallback(PokeLogCallback cb, void* userdata)
{
    logCb = cb;
    logUserdata = userdata;
}

void MGBAScript::setSoundCallback(PokeSoundCallback cb, void* userdata)
{
    soundCb = cb;
    soundUserdata = userdata;
}

void MGBAScript::setScriptDirectory(std::string dir)
{
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');
    scriptDir = std::move(dir);
}

void MGBAScript::setCore(mCore* core)
{
    corePtr = core;
    refreshMemoryDomains();
}

void MGBAScript::setScriptText(std::string text)
{
    script = std::move(text);
}

void MGBAScript::setHotkey(char key, bool down)
{
    unsigned char k = (unsigned char) key;
    if (k >= 128) return;
    hotkeys[k] = down ? 1 : 0;
}

bool MGBAScript::isHotkeyHeld(char key) const
{
    unsigned char k = (unsigned char) key;
    return k < 128 && hotkeys[k] != 0;
}

void MGBAScript::pushHeldKeys(lua_State* L) const
{
    for (int i = 0; i < 128; ++i)
    {
        if (!hotkeys[i]) continue;
        lua_pushboolean(L, 1);
        lua_seti(L, -2, (lua_Integer) i);
    }
}

void MGBAScript::log(const char* text)
{
    if (logCb) logCb(text ? text : "", logUserdata);
}

void MGBAScript::speak(const char* text, bool interrupt)
{
    if (speechCb) speechCb(text, interrupt, speechUserdata);
}

void MGBAScript::stopSpeech()
{
    if (speechCb) speechCb(nullptr, true, speechUserdata);
}

void MGBAScript::playSound(const std::string& path, int pan, int volume)
{
    // The script builds this path with backslashes too
    // (scriptpath .. "sounds\\gb\\menusel.wav"), so it needs the same
    // normalisation the file APIs get.
    std::string resolved = normalizePath(path);
    if (soundCb) soundCb(resolved.c_str(), pan, volume, soundUserdata);
}

// ------------------------------------------------------- registerexec plumbing

namespace
{
MGBAScript* gExecHost = nullptr;
}

void MGBAScript::setDebugger(mDebugger* debugger, mDebuggerModule* module)
{
    debuggerPtr = debugger;
    debuggerModulePtr = module;
}

bool MGBAScript::addExecCallback(uint32_t address, int functionRef)
{
    if (!debuggerPtr || !debuggerModulePtr)
    {
        log("[pokemon-access] registerexec ignored: no debugger attached");
        return false;
    }

    // Replace an existing hook at the same address.
    for (auto& entry : execCallbacks)
    {
        if (entry.first == (address & ~1u))
        {
            luaL_unref(L, LUA_REGISTRYINDEX, entry.second);
            entry.second = functionRef;
            gExecHost = this;
            return true;
        }
    }

    struct mBreakpoint bp;
    std::memset(&bp, 0, sizeof(bp));
    bp.id = 0;
    // mGBA clears the Thumb bit itself; hardware breakpoints compare against
    // the PC, so an ARM address must be aligned to 4 and a Thumb one to 2.
    bp.address = address;
    bp.segment = -1;
    bp.type = BREAKPOINT_HARDWARE;
    bp.condition = nullptr;
    bp.disabled = false;
    bp.isTemporary = false;

    ssize_t id = debuggerPtr->platform->setBreakpoint(debuggerPtr->platform, debuggerModulePtr, &bp);
    if (id < 0)
    {
        log("[pokemon-access] registerexec: core refused the breakpoint");
        return false;
    }

    execCallbacks.emplace_back(address & ~1u, functionRef);
    gExecHost = this;
    return true;
}

void MGBAScript::clearExecCallback(uint32_t address)
{
    if (!debuggerPtr) return;
    for (size_t i = 0; i < execCallbacks.size(); ++i)
    {
        if (execCallbacks[i].first != (address & ~1u)) continue;
        debuggerPtr->platform->clearBreakpoint(debuggerPtr->platform, (ssize_t) (i + 1));
        luaL_unref(L, LUA_REGISTRYINDEX, execCallbacks[i].second);
        execCallbacks.erase(execCallbacks.begin() + i);
        return;
    }
}

void MGBAScript::clearAllExecCallbacks()
{
    if (!debuggerPtr)
    {
        execCallbacks.clear();
        return;
    }
    // Walk the breakpoint list rather than keeping ids: the core renumbers
    // nothing, but ids are per-platform and we never stored them.
    struct mBreakpointList list;
    mBreakpointListInit(&list, 0);
    debuggerPtr->platform->listBreakpoints(debuggerPtr->platform, debuggerModulePtr, &list);
    std::vector<ssize_t> ids;
    for (size_t i = 0; i < mBreakpointListSize(&list); ++i)
        ids.push_back(mBreakpointListGetPointer(&list, i)->id);
    mBreakpointListDeinit(&list);
    for (ssize_t id : ids)
        debuggerPtr->platform->clearBreakpoint(debuggerPtr->platform, id);

    for (auto& entry : execCallbacks)
        if (L) luaL_unref(L, LUA_REGISTRYINDEX, entry.second);
    execCallbacks.clear();
}

void MGBAScript::onExecBreakpoint(uint32_t address)
{
    if (!L) return;
    for (auto& entry : execCallbacks)
    {
        if (entry.first != (address & ~1u)) continue;
        ++execHits;
        // Run the callback in its own coroutine so a callback that itself
        // yields (gba.lua's callbacks do not) cannot corrupt the main one.
        lua_rawgeti(L, LUA_REGISTRYINDEX, entry.second);
        int nresults = 0;
        int rc;
        if (execCoroutine)
        {
            lua_xmove(L, execCoroutine, 1);
            rc = lua_resume(execCoroutine, nullptr, 0, &nresults);   // real pointer (Lua 5.4)
        }
        else
        {
            rc = lua_pcall(L, 0, 0, 0);
        }
        if (rc != LUA_OK && rc != LUA_YIELD)
        {
            const char* err = lua_tostring(execCoroutine ? execCoroutine : L, -1);
            error = std::string("Exec callback error: ") + (err ? err : "?");
            log(error.c_str());
            if (execCoroutine) lua_pop(execCoroutine, 1); else lua_pop(L, 1);
        }
        return;
    }
}

// ------------------------------------------------------------ library install

void InstallLibraries(MGBAScript* core, lua_State* L)
{
    RegisterCore(L, core);

    lua_pushcfunction(L, LuaPrint);
    lua_setglobal(L, "print");

    // ---- memory
    static const luaL_Reg memoryFuncs[] = {
        { "readbyte",        LuaMemoryRead<1, false> },
        { "readbyteunsigned",LuaMemoryRead<1, false> },
        { "readbytesigned",  LuaMemoryRead<1, true>  },
        { "readword",        LuaMemoryRead<2, false> },
        { "readwordsigned",  LuaMemoryRead<2, true>  },
        { "readdword",       LuaMemoryRead<4, false> },
        { "readdwordsigned", LuaMemoryRead<4, true>  },
        { "readbyterange",   LuaReadByteRange },
        { "gbromreadbyte",   LuaGBRomReadByte },
        { "gbromreadword",   LuaGBRomReadWord },
        { "getregister",     LuaGetRegister },
        { "registerexec",    LuaRegisterExec },
        { "registerwrite",   LuaRegisterWrite },
        { nullptr, nullptr }
    };
    luaL_newlib(L, memoryFuncs);
    lua_setglobal(L, "memory");

    // ---- emu
    static const luaL_Reg emuFuncs[] = {
        { "frameadvance", LuaFrameAdvance },
        { "platform",     LuaPlatform },
        { "framecount",   LuaFramecount },
        { "yield",        LuaFrameAdvance },
        { nullptr, nullptr }
    };
    luaL_newlib(L, emuFuncs);
    lua_setglobal(L, "emu");

    // ---- tolk. What the Windows build answered from Tolk.dll.
    lua_newtable(L);
    lua_pushcfunction(L, LuaSpeak);
    lua_setfield(L, -2, "output");
    lua_pushcfunction(L, LuaSilence);
    lua_setfield(L, -2, "silence");
    lua_setglobal(L, "tolk");

    // ---- audio
    lua_newtable(L);
    lua_pushcfunction(L, LuaAudioPlay);
    lua_setfield(L, -2, "play");
    lua_pushcfunction(L, LuaAudioStop);
    lua_setfield(L, -2, "stop");
    lua_pushcfunction(L, LuaNoop);
    lua_setfield(L, -2, "pitch");
    lua_pushcfunction(L, LuaNoop);
    lua_setfield(L, -2, "load");
    lua_setglobal(L, "audio");

    // ---- controls (win-controls.lua's two entry points)
    lua_newtable(L);
    lua_pushcfunction(L, LuaCancelDialog);
    lua_setfield(L, -2, "inputbox");
    lua_pushcfunction(L, LuaCancelDialog);
    lua_setfield(L, -2, "combobox");
    lua_setglobal(L, "controls");

    // ---- bit (LuaJIT). Five operations, five closures.
    lua_newtable(L);
    struct { const char* name; int op; } ops[] = {
        { "band", 0 }, { "bor", 1 }, { "bxor", 2 }, { "bnot", 3 },
        { "lshift", 4 }, { "rshift", 5 },
    };
    for (auto& o : ops)
    {
        lua_pushinteger(L, o.op);
        lua_pushcclosure(L, LuaBitOp, 1);
        lua_setfield(L, -2, o.name);
    }
    lua_setglobal(L, "bit");

    // ---- input.read() -> { KEYNAME = bool, one entry per held letter }.
    // pokemon.lua's handle_user_actions() calls input.read() and then MUTATES
    // the result (kbd.xmouse = nil), so `input` has to be a TABLE with a read
    // field — a bare function raises "attempt to index a function value" and
    // kills the reader on the first frame.
    lua_newtable(L);
    lua_pushcfunction(L, LuaInputRead);
    lua_setfield(L, -2, "read");
    lua_setglobal(L, "input");

    // ---- bit's companion from Lua 5.1: unpack
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "unpack");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushcfunction(L, LuaUnpack);
        lua_setglobal(L, "unpack");
    }
    else
    {
        lua_pop(L, 2);
    }

    // ---- `module()` and `package.seeall`.
    //
    // a-star.lua opens with `module("astar", package.seeall)` — a Lua 5.1
    // idiom Lua 5.4 removed. pokemon.lua then calls the module's functions as
    // `astar.dist_between`, `astar.path`, `astar.distance`, so they must land
    // in a global `astar` table. Without this the chunk dies on its first line,
    // require() raises, and the whole script never loads.
    //
    // module() reimplements the 5.1 contract with 5.4's machinery: the calling
    // chunk's _ENV upvalue is repointed at the module table, so every function
    // the file declares afterwards becomes a field of it. Nothing in the
    // script's files is touched.
    lua_pushcfunction(L, [](lua_State* l) -> int {
        const char* name = luaL_checkstring(l, 1);
        // _G[name] = _G[name] or {}
        lua_getglobal(l, "_G");
        lua_getfield(l, -1, name);
        if (!lua_istable(l, -1))
        {
            lua_pop(l, 1);
            lua_newtable(l);
            lua_pushvalue(l, -1);
            lua_setfield(l, -3, name);
        }
        // Module table is now on top; give it _G as its fallback (package.seeall).
        lua_newtable(l);
        lua_getglobal(l, "_G");
        lua_setfield(l, -2, "__index");
        lua_setmetatable(l, -2);

        lua_pushstring(l, name);
        lua_setfield(l, -2, "_NAME");
        lua_pushvalue(l, -1);
        lua_setfield(l, -2, "_M");
        lua_pushstring(l, name);
        lua_setfield(l, -2, "_PACKAGE");

        // Repoint the caller's _ENV at the module table.
        // Lua 5.4 dropped the `func` field of lua_Debug; lua_getinfo("f")
        // pushes the function onto the stack instead.
        lua_Debug info;
        if (lua_getstack(l, 1, &info) && lua_getinfo(l, "f", &info))
        {
            const char* upName = lua_getupvalue(l, -1, 1);
            if (upName && std::strcmp(upName, "_ENV") == 0)
            {
                lua_pop(l, 1);              // the current _ENV
                lua_pushvalue(l, -2);       // module table
                lua_setupvalue(l, -2, 1);
            }
            else if (upName)
            {
                lua_pop(l, 2);              // upvalue + function
            }
            lua_pop(l, 1);                  // the function
        }
        return 1;                           // module() returns the module table
    });
    lua_setglobal(L, "module");

    lua_getglobal(L, "package");
    if (lua_istable(L, -1))
    {
        lua_pushcfunction(L, LuaNoop);      // package.seeall is a no-op marker
        lua_setfield(L, -2, "seeall");
    }
    lua_pop(L, 1);

    // ---- math.pow, also removed in 5.3. a-star.lua's dist() uses it.
    lua_getglobal(L, "math");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "pow");
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_pushcfunction(L, [](lua_State* l) -> int {
                lua_Number base = luaL_checknumber(l, 1);
                lua_Number exponent = luaL_checknumber(l, 2);
                lua_pushnumber(l, pow(base, exponent));
                return 1;
            });
            lua_setfield(L, -2, "pow");
        }
        else
        {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // ---- scriptpath. pokemon.lua derives it from debug.getinfo(1,"S").source
    // and the result ends in a separator; give it the asset directory with a
    // trailing '/' so both its backslash-joined and plain joins normalise.
    std::string dir = core->scriptDirectory();
    if (dir.empty()) dir = "./";
    lua_pushstring(L, dir.c_str());
    lua_setglobal(L, "scriptpath");

    // ---- io: normalising open + a stub popen. io.popen is the ONLY file API
    // the script uses that would otherwise shell out.
    lua_getglobal(L, "io");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "open");
        lua_setfield(L, -2, "open_raw");
        lua_pushcfunction(L, LuaHostOpen);
        lua_setfield(L, -2, "open");
        lua_pushcfunction(L, LuaHostPopen);
        lua_setfield(L, -2, "popen");
    }
    lua_pop(L, 1);

    // ---- loadfile / dofile / require with path normalisation.
    lua_pushcfunction(L, LuaHostLoadFile);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, LuaHostRequire);
    lua_setglobal(L, "require");

    // ---- package.path so a plain require of a data file also resolves.
    lua_getglobal(L, "package");
    if (lua_istable(L, -1))
    {
        std::string pattern = dir + "?.lua";
        lua_pushstring(L, pattern.c_str());
        lua_setfield(L, -2, "path");

        // pokemon.lua ends with:
        //     assert(package.loadlib("audio.dll", "luaopen_audio"))()
        // It loads the sound DLL and calls what comes back to get the `audio`
        // table it then overwrites... except it does NOT overwrite it: the
        // module table returned by loadlib is what its audio.play/audio.stop
        // calls resolve against on Windows. Here the global `audio` table is
        // already installed, so loadlib returns a function that hands that back.
        lua_pushcfunction(L, LuaLoadLib);
        lua_setfield(L, -2, "loadlib");
    }
    lua_pop(L, 1);

    // ---- console / gui / savestate: accepted and ignored. A missing function
    // raises and kills the whole reader; a no-op costs nothing.
    lua_newtable(L);
    lua_pushcfunction(L, LuaPrint);
    lua_setfield(L, -2, "writeline");
    lua_setglobal(L, "console");

    lua_newtable(L);
    lua_pushcfunction(L, LuaNoop);
    lua_setfield(L, -2, "clear");
    lua_setglobal(L, "gui");

    lua_newtable(L);
    lua_pushcfunction(L, LuaNoop);
    lua_setfield(L, -2, "save");
    lua_pushcfunction(L, LuaNoop);
    lua_setfield(L, -2, "load");
    lua_setglobal(L, "savestate");

    // `joypad` gets defined by the script's own shim needs on BizHawk. Not
    // used by pokemon.lua; left absent on purpose so a typo would be loud.
    (void) kBuiltinModules;
}

// ---------------------------------------------------------------- lifecycle

bool MGBAScript::start()
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

    // pokemon.lua is loaded as @<real path> so debug.getinfo inside it (should
    // anything else call it) resolves to the asset directory.
    std::string chunkName = "@" + scriptDir + "pokemon.lua";
    if (luaL_loadbuffer(L, script.c_str(), script.size(), chunkName.c_str()) != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        error = std::string("Script load error: ") + (err ? err : "?");
        log(error.c_str());
        lua_pop(L, 1);
        return false;
    }

    lua_State* co = lua_newthread(L);
    this->coroutine = co;
    this->execCoroutine = co;
    lua_insert(L, -2);              // [thread, chunk]
    lua_xmove(L, co, 1);            // chunk -> coroutine stack

    // Lua 5.4: lua_resume's 4th argument is an out-parameter and must be real.
    int nresults = 0;
    int rc = lua_resume(co, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        const char* err = lua_tostring(co, -1);
        error = std::string("Script error: ") + (err ? err : "?");
        log(error.c_str());
        return false;
    }
    scriptLoaded = true;
    return true;
}

void MGBAScript::runFrame()
{
    if (!scriptLoaded || !coroutine) return;
    framesRunCount++;

    int nresults = 0;   // same Lua 5.4 requirement as the initial resume
    int rc = lua_resume(coroutine, nullptr, 0, &nresults);
    if (rc != LUA_OK && rc != LUA_YIELD)
    {
        // A script error must not kill the loop silently: the player would lose
        // every bit of speech with no explanation.
        const char* err = lua_tostring(coroutine, -1);
        error = std::string("Script error: ") + (err ? err : "?");
        log(error.c_str());
        lua_pop(coroutine, 1);
        scriptLoaded = false;
    }
}

void MGBAScript::stop()
{
    scriptLoaded = false;
    execCallbacks.clear();
    if (L)
    {
        lua_close(L);
        L = nullptr;
        coroutine = nullptr;
        execCoroutine = nullptr;
    }
}

} // namespace MelonDSAndroid
