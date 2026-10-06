/* mesen_core.cpp — the NES backend (MesenCE): the host glue Core/nes_adapter.cpp was waiting for.
 *
 * WHY THIS FILE IS THE WHOLE TASK. Core/nes_adapter.cpp has been a deliberate REFUSAL since it was
 * written. The console was PLANNED with a chosen, ADMITTED core (44 NES + 41 Shared TUs compile for
 * aarch64-linux-android26) and no console to read from. An adapter with no console would have to
 * invent addresses, and speaking confident nonsense to a blind player is the one failure mode this
 * project treats as worse than silence.
 *
 * THE SHAPE IS DELIBERATELY THE PSP SHAPE. Every backend here is reached through one OgaCoreOps
 * table (Core/oga_core.h): start/stop/frame/read/framebuffer/input. A console that invents its own
 * interface is a second thing to learn and a second thing to get wrong.
 *
 * ⛔ WHAT THIS FILE DOES NOT DO, ON PURPOSE:
 *   * No Lua. The core's job is to be a console; the reader layer is where a game has one.
 *   * No memory WRITES. nes_read is the only memory entry point; adapters inspect and press real
 *     buttons. That is the adapter contract, and a debug-write helper here would be the thin end
 *     of teleporting game state.
 *   * No run-ahead, rewind, HD packs, video filters or debugger. Each is a feature with a cost and
 *     none is needed to prove a reader can see the game.
 *   * No savestates yet. Refusing says so out loud; a half-wired save that writes nothing while
 *     reporting success is worse than a save the player knows is not there.
 */
#include "mesen_core.h"

#include "pch.h"

#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/Interfaces/IConsole.h"
#include "Shared/BaseControlManager.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/MemoryType.h"
#include "NES/NesConsole.h"
#include "NES/BaseMapper.h"
#include "NES/NesTypes.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"

#include <cctype>
#include <cstdio>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <vector>

/* ---- the NES pad bit order, DERIVED not guessed ------------------------------------------
 * NesController::GetKeyNames() returns the string "UDLRSsBA", which is the order the controller's
 * state byte is shifted out in. So bit 0 is Up, bit 7 is A. Recorded with its source because a
 * wrong bit order does not crash: it presses the wrong button and looks like a game that ignores
 * input. The host proof presses Start and requires the game to advance, which is what confirms it.
 */
#define NES_BIT_UP     0
#define NES_BIT_DOWN   1
#define NES_BIT_LEFT   2
#define NES_BIT_RIGHT  3
#define NES_BIT_START  4
#define NES_BIT_SELECT 5
#define NES_BIT_B      6
#define NES_BIT_A      7

/* Declared before NesCore because the struct holds a pointer to it; DEFINED after
 * NesCore, because its SetInput reads the core's button array. */
class NesInputProvider;

struct NesCore {
    /* PIMPL: Mesen's headers stay out of the app's include path entirely, the same reason
     * pokecore.cpp hides melonDS. */
    std::unique_ptr<Emulator> emu;
    std::shared_ptr<IConsole> console;    /* the Emulator also holds one; we keep ours for reads */

    NesSpeechCallback speech = nullptr;
    void* speechUser = nullptr;
    NesLogCallback log = nullptr;
    void* logUser = nullptr;

    bool loaded = false;
    bool running = false;
    std::thread runThread;             /* Mesen owns the frame loop; see nes_start */
    std::atomic<bool> stopRequested{false};
    /* The host's input provider. Owned here; registered with the console's control manager. */
    NesInputProvider* inputProvider = nullptr;
    unsigned long long frames = 0;
    std::string error;
    std::string gameCode;
    uint8_t buttons[NES_BTN_COUNT] = {0};  /* 1 = pressed, in NES_BTN_* order */
    /* ---- the reader host (mirrors gba_core.cpp) ---- */
    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;
    bool scriptLoaded = false;
    std::string scriptDir;
    /* Named memory domain, as BizHawk's memory.usememorydomain sets it. "System Bus" is the NES CPU
     * address space; "CIRAM (nametables)" is PPU nametable RAM. Dragon Warrior asks for both. */
    std::string memDomain = "System Bus";
    /* ⛔ THE READERS SPEAK THROUGH A FILE, NOT AN API. Zelda 1 Access (and Dragon Warrior) write
     * "<sequence>|<message>" into a .txt their NVDA bridge polls. A reader that loads and runs
     * cleanly but says nothing is not broken -- it is talking to a listener that is not here. The
     * core polls that file and forwards new sequences to the speech sink, so the mod's own
     * protocol stays the single source of truth. */
    std::string speechFile;
    long lastSpeechSeq = -1;
    unsigned speechPollTick = 0;
    std::vector<uint8_t> frameRGBA;        /* our own copy: Mesen's pointer dies with the frame */
    int fbW = 0, fbH = 0;
};

/* The mapping, at global scope so this class and the core's own code share ONE definition. */
const uint8_t kBtnToBit[NES_BTN_COUNT] = {
    NES_BIT_A, NES_BIT_B, NES_BIT_SELECT, NES_BIT_START,
    NES_BIT_UP, NES_BIT_DOWN, NES_BIT_LEFT, NES_BIT_RIGHT,
};

class NesInputProvider : public IInputProvider {
public:
    explicit NesInputProvider(NesCore* core) : _core(core) {}

    bool SetInput(BaseControlDevice* device) override {
        if (!_core || !device) return false;
        /* Same table the core uses everywhere else: our NES_BTN_* order -> the controller's bit
         * order. Kept in one place so a change cannot desync the two. */
        for (int i = 0; i < NES_BTN_COUNT; i++)
            device->SetBitValue(kBtnToBit[i], _core->buttons[i] != 0);
        return true;      /* handled: stop the provider chain, as Mesen's own providers do */
    }

private:
    NesCore* _core;
};

namespace {

/* Our header's NES_BTN_* order -> the controller's own bit order. One table, so the mapping is
 * visible rather than implied by arithmetic. */

void SetError(NesCore* c, const char* msg) { if (c) c->error = msg ? msg : "unknown error"; }


/* The game's identity. Mesen's RomInfo carries the FILE, not a database name, so the honest source
 * is the filename. A code derived from a filename can be wrong for a file named carelessly, so it
 * is an IDENTIFIER for the registry, never a spoken title: the reader names the game. */
void ResolveGameCode(NesCore* c, const char* romPath) {
    c->gameCode.clear();
    std::string name;
    if (c->emu) name = c->emu->GetRomInfo().RomFile.GetFileName();
    if (name.empty() && romPath) name = romPath;

    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);

    std::string token;
    for (char ch : name) {
        unsigned char u = (unsigned char) ch;
        if (std::isalnum(u)) { if (token.size() < 12) token.push_back((char) std::toupper(u)); }
        else if (!token.empty() && token.back() != '-') token.push_back('-');
    }
    while (!token.empty() && token.back() == '-') token.pop_back();
    c->gameCode = token;   /* may legitimately be empty: an unknown game reads as unknown */
}


/* ======================================================================== Lua host
 *
 * Mirrors Core/gba_core.cpp's host so this repo has ONE reader-hosting pattern. See that file for
 * the fuller commentary; the differences here are all NES-specific.
 */

NesCore* CoreFromLua(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "nes_core");
    NesCore* c = (NesCore*) lua_touserdata(L, -1);
    lua_pop(L, 1);
    return c;
}

void RegisterCore(lua_State* L, NesCore* core) {
    lua_pushlightuserdata(L, core);
    lua_setfield(L, LUA_REGISTRYINDEX, "nes_core");
}

/* The one place a domain name becomes an address space. "System Bus" is what both readers mean by
 * default; "CIRAM (nametables)" is Dragon Warrior's screen-text inspection. */
/* ⛔ THE DOMAIN IS RESOLVED PER CALL, NOT FROM ONE STICKY FLAG.
 *
 * Measured failure this fixes: DW selects "CIRAM (nametables)" to read screen text and switches
 * back; with a single sticky string, a read landing in between returned NAMETABLE bytes for the
 * player's HP addresses (0x00C5/0x00CA), so max_hp read as 0xA9 instead of the real 0x00 and a
 * "Critical health" warning fired on a game that had not started. The reader's own `max_hp <= 0`
 * guard could not catch it, because the number was plausible garbage.
 *
 * BizHawk's contract: an EXPLICIT domain argument wins; the sticky default applies only when the
 * domain is omitted. That is what both readers rely on -- they pass "System Bus" explicitly for RAM.
 */
/* ⛔ THE LUA `bit` LIBRARY. BizHawk's LuaJIT exposes a global `bit` table; stock Lua 5.4 does not, so
 * a reader that uses it dies with "attempt to index a nil value (global 'bit')". Zelda uses
 * bit.band / bit.rshift / bit.lshift while decoding its overworld tables and Dragon Warrior uses
 * bit.band -- and the reader's own guard turns the failure into a spoken "Navigation error".
 *
 * 32-bit semantics, matching LuaJIT's. Lua 5.4 has native integers, so this is mostly masking; the
 * one trap is the shift count, which LuaJIT masks to 5 bits and which C leaves undefined at 32. */
static uint32_t BitNorm(lua_Integer v) { return (uint32_t) (v & 0xFFFFFFFFLL); }

static int LuaBitBand(lua_State* L) {
    int n = lua_gettop(L);
    uint32_t r = 0xFFFFFFFFu;
    for (int i = 1; i <= n; i++) r &= BitNorm(luaL_checkinteger(L, i));
    lua_pushinteger(L, (lua_Integer) r);
    return 1;
}
static int LuaBitBor(lua_State* L) {
    int n = lua_gettop(L);
    uint32_t r = 0;
    for (int i = 1; i <= n; i++) r |= BitNorm(luaL_checkinteger(L, i));
    lua_pushinteger(L, (lua_Integer) r);
    return 1;
}
static int LuaBitBxor(lua_State* L) {
    int n = lua_gettop(L);
    uint32_t r = 0;
    for (int i = 1; i <= n; i++) r ^= BitNorm(luaL_checkinteger(L, i));
    lua_pushinteger(L, (lua_Integer) r);
    return 1;
}
static int LuaBitBnot(lua_State* L) {
    lua_pushinteger(L, (lua_Integer) ~BitNorm(luaL_checkinteger(L, 1)));
    return 1;
}
static int LuaBitRshift(lua_State* L) {
    uint32_t v = BitNorm(luaL_checkinteger(L, 1));
    int s = (int) (luaL_checkinteger(L, 2) & 31);
    lua_pushinteger(L, (lua_Integer) (s ? (v >> s) : v));
    return 1;
}
static int LuaBitLshift(lua_State* L) {
    uint32_t v = BitNorm(luaL_checkinteger(L, 1));
    int s = (int) (luaL_checkinteger(L, 2) & 31);
    lua_pushinteger(L, (lua_Integer) (s ? (v << s) : v));
    return 1;
}
static int LuaBitArshift(lua_State* L) {
    int32_t v = (int32_t) BitNorm(luaL_checkinteger(L, 1));
    int s = (int) (luaL_checkinteger(L, 2) & 31);
    lua_pushinteger(L, (lua_Integer) (s ? (v >> s) : v));
    return 1;
}
static int LuaBitTohex(lua_State* L) {
    uint32_t v = BitNorm(luaL_checkinteger(L, 1));
    int digits = (lua_gettop(L) > 1) ? (int) luaL_checkinteger(L, 2) : 8;
    char buf[48];
    snprintf(buf, sizeof(buf), "%0*X", digits, v);
    lua_pushstring(L, buf);
    return 1;
}

static const luaL_Reg kBitLib[] = {
    { "band",    LuaBitBand    }, { "bor",     LuaBitBor     },
    { "bxor",    LuaBitBxor    }, { "bnot",    LuaBitBnot    },
    { "rshift",  LuaBitRshift  }, { "lshift",  LuaBitLshift  },
    { "arshift", LuaBitArshift }, { "tohex",   LuaBitTohex   },
    { NULL, NULL }
};

static void InstallBitLibrary(lua_State* L) {
    luaL_newlib(L, kBitLib);
    lua_setglobal(L, "bit");
}

int LuaMemReadDomain(NesCore* c, uint32_t addr, const char* domain) {
    if (!c) return 0;
    bool nametable = domain && strcmp(domain, "CIRAM (nametables)") == 0;
    if (!nametable) nametable = (!domain && c->memDomain == "CIRAM (nametables)");

    if (nametable) {
        /* Nametables live at 0x2000+ in the PPU's space. An address below that is NOT a nametable,
         * and answering it with tile bytes is how the false health reading happened. Refuse it. */
        if (addr < 0x2000) return 0;
        return (int) nes_read_nametable(c, addr);
    }
    /* ⛔ CART ROM IS ITS OWN DOMAIN. Zelda's rom_read() asks for "PRG ROM" explicitly, the same way
     * it asks for "System Bus" for RAM. Treating an unknown domain as system RAM returned LIVE RAM
     * BYTES for cart-ROM addresses -- silently wrong data, not an obvious zero. */
    if (domain && (strcmp(domain, "PRG ROM") == 0 || strcmp(domain, "PRG ROM (Mapper)") == 0))
        return (int) nes_read_prg_rom(c, addr);

    uint32_t v = 0;
    if (!nes_read(c, addr, 1, &v)) return 0;
    return (int) v;
}

int LuaMemRead(NesCore* c, uint32_t addr) { return LuaMemReadDomain(c, addr, nullptr); }

int LuaMemRead8(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    uint32_t a = (uint32_t) luaL_checkinteger(L, 1);
    /* BizHawk's read_u8 takes an OPTIONAL domain as its second argument, and an explicit domain
     * must win over the sticky default -- see LuaMemReadDomain for the bug that proved it. */
    const char* dom = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    lua_pushinteger(L, LuaMemReadDomain(c, a, dom));
    return 1;
}

/* ⛔ WIDTH-AWARE, because binding the 16/32-bit names to the 8-bit function is SILENT: the reader
 * would get one byte of a two-byte value and read a truncated address. little-endian, like BizHawk. */
int LuaMemReadWide(lua_State* L, int width) {
    NesCore* c = CoreFromLua(L);
    uint32_t a = (uint32_t) luaL_checkinteger(L, 1);
    const char* dom = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    uint32_t v = 0;
    for (int i = 0; i < width; i++)
        v |= (uint32_t) LuaMemReadDomain(c, a + (uint32_t) i, dom) << (8 * i);
    lua_pushinteger(L, (lua_Integer) v);
    return 1;
}
int LuaMemRead16(lua_State* L) { return LuaMemReadWide(L, 2); }
int LuaMemRead32(lua_State* L) { return LuaMemReadWide(L, 4); }

int LuaMemReadRange(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    uint32_t a = (uint32_t) luaL_checkinteger(L, 1);
    int n = (int) luaL_checkinteger(L, 2);
    lua_newtable(L);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, LuaMemRead(c, a + (uint32_t) i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int LuaMemWrite8(lua_State* L) {
    /* ⛔ WRITES ARE ACCEPTED BUT DO NOTHING TO GUEST MEMORY. A reader script occasionally writes to
     * its own state; letting a script write RAM would break the read-only contract every adapter in
     * this project holds to. Refusing silently is deliberate: the readers guard these calls in
     * pcall and work fine without them, and a hard error would break a working reader instead. */
    (void) L;
    lua_pushnil(L);
    return 1;
}

int LuaMemUseDomain(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    if (!c) return 0;
    const char* d = luaL_checkstring(L, 1);
    /* Record it, and be honest about the ones this console cannot serve. Unknown domains keep the
     * previous setting rather than silently reading the wrong space. */
    if (strcmp(d, "System Bus") == 0 || strcmp(d, "CIRAM (nametables)") == 0 ||
        strcmp(d, "PRG ROM") == 0 || strcmp(d, "PRG ROM (Mapper)") == 0)
        c->memDomain = d;
    return 0;
}

int LuaEmuFrameCount(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    /* ⛔ THE REAL FRAME COUNT. The existing shim increments a Lua counter, which is a CALL count --
     * Dragon Warrior uses frame numbers for timing (11 call sites), so a counter would drift. */
    lua_pushinteger(L, (lua_Integer) nes_frames_completed(c));
    return 1;
}

int LuaEmuFrameAdvance(lua_State* L) {
    /* The reader's `while true do emu.frameadvance() end` yields here; nes_frame resumes it. */
    return lua_yield(L, 0);
}

int LuaConsoleLog(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    const char* s = luaL_tolstring(L, 1, nullptr);
    if (c && c->log && s) c->log(s, c->logUser);
    lua_pop(L, 1);
    return 0;
}

int LuaGuiText(lua_State* L) {
    /* gui.text draws an overlay; OGA has no overlay, so this is a no-op that must exist because the
     * readers call it unconditionally. */
    (void) L;
    return 0;
}

int LuaJoypadSet(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    if (!c || !lua_istable(L, 1)) return 0;
    /* Press through the SAME path the app uses, so a script can never do more than a player. */
    static const char* kNames[NES_BTN_COUNT] = { "A", "B", "Select", "Start", "Up", "Down", "Left", "Right" };
    for (int i = 0; i < NES_BTN_COUNT; i++) {
        lua_getfield(L, 1, kNames[i]);
        bool down = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        nes_set_button(c, i, down);
    }
    return 0;
}

int LuaJoypadGet(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    static const char* kNames[NES_BTN_COUNT] = { "A", "B", "Select", "Start", "Up", "Down", "Left", "Right" };
    lua_newtable(L);
    for (int i = 0; i < NES_BTN_COUNT; i++) {
        lua_pushboolean(L, c && c->buttons[i]);
        lua_setfield(L, -2, kNames[i]);
    }
    return 1;
}

const luaL_Reg kMemoryFuncs[] = {
    { "read_u8",            LuaMemRead8 },
    { "read_u16_le",        LuaMemRead16 },
    { "read_u32_le",        LuaMemRead32 },
    { "read_bytes_as_array", LuaMemReadRange },
    { "write_u8",           LuaMemWrite8 },
    { "usememorydomain",    LuaMemUseDomain },
    { NULL, NULL }
};

const luaL_Reg kEmuFuncs[] = {
    { "framecount",    LuaEmuFrameCount },
    { "frameadvance",  LuaEmuFrameAdvance },
    { NULL, NULL }
};

const luaL_Reg kJoypadFuncs[] = {
    { "set", LuaJoypadSet },
    { "get", LuaJoypadGet },
    { NULL, NULL }
};

const luaL_Reg kConsoleFuncs[] = {
    { "log", LuaConsoleLog },
    { NULL, NULL }
};

int LuaOgaSetSpeechFile(lua_State* L) {
    NesCore* c = CoreFromLua(L);
    if (!c) return 0;
    const char* p = luaL_checkstring(L, 1);
    /* Set explicitly by our wrapper, from the reader's own SPEECH_FILE declaration. Never guessed:
     * the sound-command file uses the same "<seq>|text" shape and a shape test picks the wrong one. */
    c->speechFile = (p && *p) ? p : "";
    c->lastSpeechSeq = -1;
    if (c->log) c->log(("speech file set: " + c->speechFile).c_str(), c->logUser);
    return 0;
}

const luaL_Reg kOgaFuncs[] = {
    { "set_speech_file", LuaOgaSetSpeechFile },
    { NULL, NULL }
};

const luaL_Reg kGuiFuncs[] = {
    { "text", LuaGuiText },
    { NULL, NULL }
};

void InstallLibraries(NesCore* core) {
    lua_State* L = core->L;
    RegisterCore(L, core);

    lua_newtable(L); luaL_setfuncs(L, kMemoryFuncs, 0); lua_setglobal(L, "memory");
    lua_newtable(L); luaL_setfuncs(L, kEmuFuncs, 0);    lua_setglobal(L, "emu");
    lua_newtable(L); luaL_setfuncs(L, kJoypadFuncs, 0); lua_setglobal(L, "joypad");
    lua_newtable(L); luaL_setfuncs(L, kConsoleFuncs, 0);lua_setglobal(L, "console");
    lua_newtable(L); luaL_setfuncs(L, kGuiFuncs, 0);    lua_setglobal(L, "gui");
    lua_newtable(L); luaL_setfuncs(L, kOgaFuncs, 0);    lua_setglobal(L, "oga");

    /* `print` should reach the log, not stdout, so a reader's debug lines are visible in the app. */
    lua_pushcfunction(L, LuaConsoleLog);
    lua_setglobal(L, "print");
}


/* Read the reader's speech file and forward a NEW sequence to the speech sink.
 *
 * Format, from the reader's own write_speech():  "<sequence>|<message>"  (Lua: string.format("%d|%s", ...)).
 * A leading byte-order mark may be present (the shipped file has one); it is stripped.
 *
 * ⛔ A REWRITE WITH THE SAME SEQUENCE IS NOT A NEW UTTERANCE. The reader rewrites the whole file
 * every time, so only a CHANGED sequence number means new words. Without that check the reader's
 * own "stale content for a frame" concern becomes a stutter here instead.
 */
void PollReaderSpeech(NesCore* c) {
    if (!c || !c->speech || c->speechFile.empty()) return;
    FILE* f = fopen(c->speechFile.c_str(), "rb");
    if (!f) return;
    char buf[1024] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return;

    const char* s = buf;
    if ((unsigned char) s[0] == 0xEF && (unsigned char) s[1] == 0xBB && (unsigned char) s[2] == 0xBF)
        s += 3;                                   /* UTF-8 BOM */
    char* bar = strchr(const_cast<char*>(s), '|');
    if (!bar) return;
    *bar = 0;
    long seq = strtol(s, nullptr, 10);
    const char* msg = bar + 1;

    if (seq != c->lastSpeechSeq && *msg) {
        c->lastSpeechSeq = seq;
        c->speech(msg, false, c->speechUser);
    }
}

} // namespace

extern "C" {

NesCore* nes_create(void) {
    return new (std::nothrow) NesCore();
}

void nes_destroy(NesCore* c) {
    if (!c) return;
    if (c->running) {
        if (c->emu) c->emu->Stop(false, true, true);
        if (c->runThread.joinable()) c->runThread.join();
        c->running = false;
    }
    c->console.reset();
    c->emu.reset();
    delete c;
}

void nes_set_speech_callback(NesCore* c, NesSpeechCallback cb, void* userdata) {
    if (!c) return;
    c->speech = cb;
    c->speechUser = userdata;
}

void nes_set_log_callback(NesCore* c, NesLogCallback cb, void* userdata) {
    if (!c) return;
    c->log = cb;
    c->logUser = userdata;
}

void nes_set_script_dir(NesCore* c, const char* dir) {
    if (!c) return;
    c->scriptDir = (dir && *dir) ? dir : "";
    c->speechFile.clear();
    c->lastSpeechSeq = -1;
    /* ⛔ THE SPEECH FILE IS TOLD TO US, NEVER GUESSED. Both readers also ship a
     * sound_bridge_command.txt whose contents look like "<seq>|text" -- the same shape as real
     * speech -- so a content heuristic picks the sound file and reports hearing "reset" while the
     * player hears nothing. The wrapper reads the reader's own SPEECH_FILE declaration and calls
     * oga.set_speech_file(). See Resources/nes-lua/<game>/oga_nes_reader.lua, which parses the
     * reader's own `SPEECH_FILE = DATA_DIR .. "/<name>.txt"` line. */
}

bool nes_script_loaded(NesCore* c) { return c && c->scriptLoaded; }

bool nes_load_rom(NesCore* c, const char* rom_path, const char* save_path, char code_out[16]) {
    if (!c || !rom_path) return false;
    c->error.clear();

    // ⛔ MESEN REFUSES TO LOAD ANYTHING WITHOUT A HOME FOLDER, and it does so by THROWING from
    // inside LoadRom, where Emulator's own catch turns it into a bare `false`. The first symptom was
    // "nes_load_rom returned false" for a ROM that is verifiably valid; the actual message, once
    // MessageManager's log was captured, was "Home folder not specified".
    //
    // FolderUtilities::SetHomeFolder is what Mesen's own front ends call at startup and a library
    // user must too: save data, savestates, firmware and the game database all resolve under it. Set
    // it from the save directory when the caller gives one, else the process's working directory --
    // never leave it unset, because the failure is a swallowed exception rather than a clear error.
    {
        std::string home = (save_path && *save_path) ? std::string(save_path) : std::string(".");
        FolderUtilities::SetHomeFolder(home);
    }

    c->emu.reset(new (std::nothrow) Emulator());
    if (!c->emu) { SetError(c, "Could not create the emulator."); return false; }

    /* ⛔ INITIALIZE BEFORE LOADING. Mesen wires settings, memory registrations and its console
     * factory here. LoadRom on an uninitialized Emulator fails in ways that look like a bad ROM. */
    c->emu->Initialize();

    VirtualFile rom((std::string(rom_path)));
    if (!rom.IsValid()) { SetError(c, "The ROM could not be opened."); return false; }

    // ⛔ LoadRom SWALLOWS ITS OWN EXCEPTION. Emulator::LoadRom wraps InternalLoadRom in
    // try/catch(std::exception&) and routes the message to MessageManager::DisplayMessage, which
    // in a headless build goes nowhere -- so a genuine exception surfaces only as `false`. Catch it
    // here so the real reason reaches the log instead of "Mesen refused the ROM" telling us nothing.
    bool loaded = false;
    try {
        loaded = c->emu->LoadRom(rom, VirtualFile(), false);
    } catch (const std::exception& ex) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "Mesen threw while loading: %s", ex.what());
        SetError(c, buf);
        return false;
    } catch (...) {
        SetError(c, "Mesen threw a non-std exception while loading.");
        return false;
    }
    if (!loaded) {
        SetError(c, "Mesen refused the ROM (LoadRom returned false; see the log for GameLoadFailed).");
        return false;
    }

    c->console = c->emu->GetConsole();
    if (!c->console) { SetError(c, "The ROM loaded but no console was created."); return false; }

    ResolveGameCode(c, rom_path);
    if (code_out) std::snprintf(code_out, 16, "%s", c->gameCode.c_str());

    (void) save_path;   /* battery flush belongs in nes_stop; nothing to do at load time */
    c->loaded = true;
    c->running = false;
    c->frames = 0;
    return true;
}

int nes_read_audio(NesCore* c, int16_t* out, int max_frames) {
    /* ⛔ NO AUDIO PATH YET, AND IT SAYS SO. Mesen mixes through Core/Shared/Audio into a device we
     * have not wired. Returning 0 is honest; the host reads 0 as "dry", not as an error, and the
     * reader's speech does not depend on it. Wire this when a game's sound matters (Zelda's
     * SoundBridge does -- it is in the port notes), not before. */
    (void) c; (void) out; (void) max_frames;
    return 0;
}

bool nes_start(NesCore* c) {
    if (!c || !c->loaded || !c->emu) { SetError(c, "Load a game first."); return false; }
    if (c->running) return true;

    // ⛔ MESEN'S FRAME LOOP IS `Run()`, AND IT MUST OWN A THREAD. Run() is `while(!_stopFlag)` and
    // it is also the only place `_frameLimiter` is created -- and the PPU calls
    // Emulator::ProcessEndOfFrame (which dereferences it) at the end of EVERY frame. So there is no
    // supported way to step one frame synchronously; the library's model is Run() on its own thread
    // with the host reading state, which is exactly how Mesen's own front ends drive it.
    /* ---- the reader script, if one was pointed at ---- */
    if (!c->scriptDir.empty()) {
        lua_State* L = luaL_newstate();
        if (!L) { SetError(c, "Could not create the script engine."); return false; }
        luaL_openlibs(L);
        InstallBitLibrary(L);   /* BizHawk/LuaJIT global the readers expect */
        c->L = L;
        InstallLibraries(c);

        /* The entry file. Both NES readers are a single main script that the mod's own loader
         * normally starts, so accept either name rather than demanding one. */
        /* First match wins, in this order: a Game-Boy-style bootstrap, then a plain main.lua, then
         * the NES wrapper (which installs the NES-shaped BizHawk surface and then runs the mod's own
         * entry file -- see Resources/nes-lua/<game>/oga_nes_reader.lua). */
        std::string boot;
        for (const char* name : { "/oga_bootstrap.lua", "/main.lua", "/oga_nes_reader.lua" }) {
            std::string cand = c->scriptDir + name;
            if (FILE* f = fopen(cand.c_str(), "rb")) { fclose(f); boot = cand; break; }
        }
        if (boot.empty()) {
            SetError(c, "No reader entry script (oga_bootstrap.lua or main.lua) in that directory.");
            return false;
        }

        lua_State* co = lua_newthread(L);
        c->coroutine = co;
        if (luaL_loadfile(co, boot.c_str()) != LUA_OK) {
            { char buf[512]; std::snprintf(buf, sizeof(buf), "Reader load error: %s",
                                        lua_tostring(co, -1) ? lua_tostring(co, -1) : "(no message)");
              SetError(c, buf); }
            return false;
        }
        int nresults = 0;   /* Lua 5.4 resume writes through this; nullptr segfaults */
        int rc = lua_resume(co, nullptr, 0, &nresults);
        if (rc != LUA_OK && rc != LUA_YIELD) {
            { char buf[512]; std::snprintf(buf, sizeof(buf), "Reader error: %s",
                                        lua_tostring(co, -1) ? lua_tostring(co, -1) : "(no message)");
              SetError(c, buf); }
            return false;
        }
        c->scriptLoaded = true;
    }

    /* Register the host's input provider BEFORE the frame loop starts, so the very first frame
     * already sees real input rather than a cleared pad. */
    if (c->console) {
        BaseControlManager* cm = c->console->GetControlManager();
        if (cm) {
            if (!c->inputProvider) c->inputProvider = new NesInputProvider(c);
            cm->RegisterInputProvider(c->inputProvider);
            if (c->log) c->log("nes: host input provider registered", c->logUser);
        }
    }

    c->stopRequested = false;
    c->runThread = std::thread([c]() {
        try {
            c->emu->Run();
        } catch (...) {
            // An exception escaping the emulation thread would terminate the process. Record it.
            c->stopRequested = true;
        }
    });
    c->running = true;
    return true;
}

void nes_stop(NesCore* c) {
    if (!c) return;
    if (c->L) { lua_close(c->L); c->L = nullptr; c->coroutine = nullptr; c->scriptLoaded = false; }
    if (c->inputProvider) {
        if (c->console) {
            BaseControlManager* cm = c->console->GetControlManager();
            if (cm) cm->UnregisterInputProvider(c->inputProvider);
        }
        delete c->inputProvider;
        c->inputProvider = nullptr;
    }
    if (!c->emu) return;
    if (c->running) {
        c->emu->Stop(false, true, true);     /* sets _stopFlag, so Run() returns */
        if (c->runThread.joinable()) c->runThread.join();
        c->running = false;
    }
}

bool nes_running(NesCore* c) { return c && c->running; }

void nes_set_uncapped(NesCore *core, bool uncapped) {
    if (!core || !core->emu) return;
    /* EmulationFlags::MaximumSpeed -> GetEmulationSpeed() == 0 -> GetFrameDelay() == 0, so the
     * frame limiter stops sleeping. Test/tooling only; the shipped app stays paced. */
    EmuSettings *settings = core->emu->GetSettings();
    if (!settings) return;
    settings->SetFlagState(EmulationFlags::MaximumSpeed, uncapped);
    if (core->log) core->log(uncapped ? "nes: uncapped (test mode)" : "nes: paced", core->logUser);
}

bool nes_frame(NesCore* c) {
    if (!c || !c->loaded || !c->emu) return false;

    /* ⛔ NO PER-FRAME BUTTON POKE HERE. Writing the pad from this function does not work: Mesen's
     * UpdateInputState() calls ClearState() on every device at the start of each frame, so the bits
     * are wiped before the frame runs. Input goes through the registered IInputProvider instead
     * (see NesInputProvider) -- the path Mesen intends for a host. */

    // One nes_frame call == one emulated frame, with the APP owning its clock. The frame is
    // produced by Mesen's own thread (see nes_start); this waits for the console's frame counter to
    // advance. ⛔ BOUNDED ON PURPOSE -- a wait that never returns must surface as an error, not a
    // hang, because a silent hang is indistinguishable from a load failure and cost real time here.
    if (!c->console) return false;

    uint32_t before = c->console->GetFrameCount();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (c->console->GetFrameCount() == before) {
        if (c->stopRequested) { SetError(c, "The NES core stopped unexpectedly."); return false; }
        if (std::chrono::steady_clock::now() > deadline) {
            SetError(c, "The NES core did not produce a frame within 5 seconds.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    c->frames++;

    /* ⛔ AFTER the frame, so the script observes the state that just completed rather than the
     * previous frame's. A reader answering one frame stale reports the old room, the old HP. */
    if (c->scriptLoaded && c->coroutine) {
        int nresults = 0;
        int rc = lua_resume(c->coroutine, nullptr, 0, &nresults);
        if (rc != LUA_OK && rc != LUA_YIELD) {
            const char* err = lua_tostring(c->coroutine, -1);
            if (c->log) c->log(err ? err : "reader error", c->logUser);
            c->scriptLoaded = false;
        }
    }

    /* Forward the reader's file-based speech. Cheap (one small file per frame) and it is the ONLY
     * way a reader using this protocol reaches the player. */
    PollReaderSpeech(c);

    /* Copy the framebuffer out: Mesen's FrameBuffer pointer is only valid until the next frame, so
     * the core owns its copy -- the same contract psp_framebuffer_ptr documents. */
    if (c->console) {
        PpuFrameInfo frame = c->console->GetPpuFrame();
        if (frame.FrameBuffer && frame.Width && frame.Height) {
            c->fbW = (int) frame.Width;
            c->fbH = (int) frame.Height;
            size_t n = (size_t) c->fbW * (size_t) c->fbH;
            c->frameRGBA.resize(n * 4);
            std::memcpy(c->frameRGBA.data(), frame.FrameBuffer, n * 4);
        }
    }
    return true;
}

bool nes_framebuffer(NesCore* c, int* width, int* height) {
    if (!c || c->fbW <= 0 || c->fbH <= 0) return false;
    if (width) *width = c->fbW;
    if (height) *height = c->fbH;
    return true;
}

const uint8_t* nes_framebuffer_ptr(NesCore* c) {
    return (c && !c->frameRGBA.empty()) ? c->frameRGBA.data() : nullptr;
}

void nes_set_button(NesCore* c, int nes_button, bool down) {
    if (!c || nes_button < 0 || nes_button >= NES_BTN_COUNT) return;
    c->buttons[nes_button] = down ? 1 : 0;
}

bool nes_save_state(NesCore* c, const char* path) {
    (void) path;
    SetError(c, "NES save states are not implemented yet.");
    return false;
}

bool nes_load_state(NesCore* c, const char* path) {
    (void) path;
    SetError(c, "NES save states are not implemented yet.");
    return false;
}

unsigned long long nes_frames_completed(NesCore* c) { return c ? c->frames : 0; }

int nes_is_paused(NesCore* c) { return (c && c->emu) ? (c->emu->IsPaused() ? 1 : 0) : -1; }

unsigned nes_console_frame_count(NesCore* c) {
    return (c && c->console) ? c->console->GetFrameCount() : 0u;
}

const char* nes_last_error(NesCore* c) { return (c && !c->error.empty()) ? c->error.c_str() : ""; }

bool nes_read(NesCore* c, uint32_t addr, int width, uint32_t* out) {
    if (!c || !c->console || !out) return false;
    if (width != 1 && width != 2) return false;

    /* ⛔ READ THROUGH THE CONSOLE, NOT THE RAM BLOCK. Mesen's NesConsole::DebugRead is the entry
     * point the debugger's own MemoryDumper uses (Core/Debugger/MemoryDumper.cpp:383). Reading the
     * RAM block directly would answer a different question than the game's code asks, because
     * mapper banking decides which PRG bytes a 0x8000+ address resolves to. */
    NesConsole* nes = dynamic_cast<NesConsole*>(c->console.get());
    if (!nes) return false;

    uint16_t a = (uint16_t) (addr & 0xFFFFu);   /* the NES CPU address space is 64 KB */
    uint32_t v = 0;
    for (int i = 0; i < width; i++) {
        /* ⛔ A 16-bit read must NOT wrap past the top of the space; a wrapped read is a silent
         * wrong answer, which is exactly the failure an adapter cannot detect. */
        if ((uint32_t) a + (uint32_t) i > 0xFFFFu) return false;
        v |= (uint32_t) nes->DebugRead((uint16_t) ((uint32_t) a + (uint32_t) i)) << (8 * i);
    }
    *out = v;
    return true;
}

uint8_t nes_read_prg_rom(NesCore* c, uint32_t addr) {
    if (!c || !c->emu) return 0;
    /* MemoryType::NesPrgRom is the cartridge's PRG ROM as Mesen's own MemoryDumper exposes it, and
     * ConsoleMemoryInfo is simply a buffer plus a length, so a byte is an array index.
     * This is the address space the readers' rom_read() indexes (0x18500, 0x19324, ...). */
    ConsoleMemoryInfo m = c->emu->GetMemory(MemoryType::NesPrgRom);
    if (!m.Memory || addr >= m.Size) return 0;
    return ((const uint8_t*) m.Memory)[addr];
}

uint8_t nes_read_nametable(NesCore* c, uint32_t addr) {
    if (!c || !c->console) return 0;
    NesConsole* nes = dynamic_cast<NesConsole*>(c->console.get());
    if (!nes) return 0;

    /* ⛔ THE PUBLIC DEBUGGER PATH. BaseMapper::GetNametable is PROTECTED, so a library caller cannot
     * use it -- and indexing a raw buffer would bypass the mapper's mirroring, which is the whole
     * reason nametables are reachable at all. NesConsole::DebugReadVram is public and is what
     * Mesen's own MemoryDumper uses for the NesPpuMemory type. Going through the console means the
     * mapper's own mirroring decides where a logical nametable lands.
     *
     * The caller passes a CIRAM offset; the PPU's nametables live at 0x2000 in its 14-bit space. */
    uint16_t vramAddr = (uint16_t) (0x2000u + (addr & 0x0FFFu));
    return nes->DebugReadVram(vramAddr);
}

uint32_t nes_ram_base(NesCore* c) {
    (void) c;
    return NES_RAM_BASE;
}

} // extern "C"
