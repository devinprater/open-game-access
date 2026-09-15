#ifndef MGBACORE_H
#define MGBACORE_H

#include <cstdint>
#include <string>
#include <vector>

struct mCore;
struct mDebugger;
struct mDebuggerModule;

// Lua headers are C; this file is included from C++.
extern "C" {
struct lua_State;
}

namespace MelonDSAndroid
{

/// The speech sink, shared with the melonDS path (PokeScript.h). `text` is a
/// NUL-terminated UTF-8 string; a NULL `text` means "stop speaking".
typedef void (*PokeSpeechCallback)(const char* text, bool interrupt, void* userdata);
typedef void (*PokeLogCallback)(const char* text, void* userdata);
/// Short cue sound (the proximity/terrain beeps). `path` is a host path, the
/// rest are the game's own pan/volume numbers (out of 100, so they can be
/// negative).
typedef void (*PokeSoundCallback)(const char* path, int pan, int volume, void* userdata);

/// Game Boy / Game Boy Color / Game Boy Advance host for the Pokémon Access
/// v3.1.0 accessibility script (pokemon.lua + gb.lua/gba.lua).
///
/// This is the mGBA twin of PokeScript: it owns a Lua 5.4 state, installs the
/// VisualBoyAdvance-flavoured API surface the GB/GBA script was written against
/// (tolk, memory, emu, audio, win-controls, bit, a-star, serpent), and runs
/// pokemon.lua UNMODIFIED inside a coroutine that is resumed once per emulated
/// frame.
///
/// ⛔ NEVER EDIT pokemon.lua / gb.lua / gba.lua. Every Windows-ism (LuaJIT FFI,
/// backslash paths, io.popen("dir")) is neutralised HOST-SIDE here.
///
/// Three traps, all learned the hard way:
///
///   1. THE SCRIPT BUILDS EVERY PATH WITH BACKSLASHES. `scriptpath` comes from
///      debug.getinfo and ends in a separator, and the script then appends
///      "game\\emerald\\en\\" etc. Android has no backslash directories. The
///      host therefore normalises EVERY path crossing loadfile/io.open/
///      audio.play/require (`NormalizePath`).
///   2. THE SCRIPT LOADS ITS DATA WITH `loadfile` AT CALL TIME (not `require`),
///      so loadfile and io.open must be replaced, not merely package.path.
///   3. `unpack` and `bit` are Lua 5.1/LuaJIT globals. The script calls both;
///      Lua 5.4 has neither. They are installed as host globals.
class MGBAScript
{
public:
    MGBAScript();
    ~MGBAScript();

    MGBAScript(const MGBAScript&) = delete;
    MGBAScript& operator=(const MGBAScript&) = delete;

    void setSpeechCallback(PokeSpeechCallback cb, void* userdata);
    void setLogCallback(PokeLogCallback cb, void* userdata);
    void setSoundCallback(PokeSoundCallback cb, void* userdata);

    /// Absolute path of the extracted asset directory holding pokemon.lua.
    /// Used to (a) set scriptpath and (b) normalise the script's paths.
    void setScriptDirectory(std::string dir);
    const std::string& scriptDirectory() const { return scriptDir; }

    /// The core the script reads. Set on ROM load, cleared before it dies.
    void setCore(mCore* core);
    mCore* core() const { return corePtr; }

    /// Text of pokemon.lua itself, stored not compiled until start().
    void setScriptText(std::string text);
    bool hasScript() const { return !script.empty(); }

    /// Compiles pokemon.lua and runs it to its first emu.frameadvance() yield.
    /// Returns false and sets lastError() on failure.
    bool start();

    /// One emulated frame's worth of script work: resumes the coroutine once.
    void runFrame();

    /// Stops and tears the Lua state down. Idempotent.
    void stop();

    const std::string& lastError() const { return error; }
    bool isRunning() const { return scriptLoaded; }
    uint64_t framesRun() const { return framesRunCount; }

    /// Game Boy Advance buttons currently held, as mGBA's key bits
    /// (bit 0 = A, ... GBA_KEY_* — see include/mgba/internal/gba/input.h).
    void setJoypadState(uint32_t buttonsDown) { joypadDown = buttonsDown; }
    uint32_t joypadDownMask() const { return joypadDown; }

    /// Keys the script sees through input.read(), by ASCII code of an
    /// upper-case letter. Set true while held.
    void setHotkey(char key, bool down);
    bool isHotkeyHeld(char key) const;

    // ---- Lua-facing entry points (called from the C callbacks in the .cpp) ----
    void pushHeldKeys(lua_State* L) const;
    void log(const char* text);
    void speak(const char* text, bool interrupt);
    void stopSpeech();
    void playSound(const std::string& path, int pan, int volume);

    // ---- memory.registerexec support ----
    /// Registers `func` to run when the CPU reaches `address`.
    /// The host implements it with an mGBA hardware breakpoint: with a
    /// breakpoint present the core steps one instruction at a time and checks
    /// for a hit, which is exactly the VBA registerexec semantic.
    bool addExecCallback(uint32_t address, int functionRef);
    void clearExecCallback(uint32_t address);
    void clearAllExecCallbacks();
    /// Called from the debugger module when the core enters a breakpoint.
    void onExecBreakpoint(uint32_t address);
    /// How many registerexec callbacks are live, and how often they have fired.
    /// Diagnostics: a GBA game whose ROM-address hooks never fire reads as
    /// blank text with no error anywhere, so this is the number to check.
    size_t execCallbackCount() const { return execCallbacks.size(); }
    uint64_t execBreakpointCount() const { return execHits; }
    bool hasExecCallbacks() const { return !execCallbacks.empty(); }

    /// The mGBA debugger the RUNNER owns, so memory.registerexec can install
    /// hardware breakpoints. The script does not create it: mGBA's debugger has
    /// to be attached before the core starts, and it outlives this object.
    void setDebugger(mDebugger* debugger, mDebuggerModule* module);

    /// Reads the ROM through the core's memory blocks so bytes 0x100-0x14F of a
    /// GB cart (the header pokemon.lua detects the game from) are reachable
    /// through memory.gbromreadbyte.
    uint8_t romByte(uint32_t address) const;
    void refreshMemoryDomains();

    /// Registers a `require`-able module, so the script's own imports resolve
    /// out of the asset directory instead of package.path alone.
    void addAssetModuleName(const std::string& name);

    std::string normalizePath(const std::string& path) const;

private:
    friend struct MGBAScriptAccess;

    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;
    lua_State* execCoroutine = nullptr;

    PokeSpeechCallback speechCb = nullptr;
    void* speechUserdata = nullptr;
    PokeLogCallback logCb = nullptr;
    void* logUserdata = nullptr;
    PokeSoundCallback soundCb = nullptr;
    void* soundUserdata = nullptr;

    mCore* corePtr = nullptr;
    mDebugger* debuggerPtr = nullptr;
    mDebuggerModule* debuggerModulePtr = nullptr;
    bool debuggerAttached = false;

    std::string scriptDir;
    std::string script;
    std::string error;

    uint32_t joypadDown = 0;
    char hotkeys[128] = {0};

    bool scriptLoaded = false;
    uint64_t framesRunCount = 0;
    uint64_t execHits = 0;

    /// address -> registry ref of the Lua function
    std::vector<std::pair<uint32_t, int>> execCallbacks;

    /// Cached ROM pointer + length, refreshed on ROM load.
    const uint8_t* romBase = nullptr;
    size_t romSize = 0;
};

} // namespace MelonDSAndroid

#endif // MGBACORE_H
