#ifndef POKESCRIPT_H
#define POKESCRIPT_H

#include <cstdint>
#include <string>

// Lua headers are C; this file is included from C++.
extern "C" {
struct lua_State;
}

namespace melonDS { class NDS; }

namespace MelonDSAndroid
{

/// The speech sink. `text` is a NUL-terminated UTF-8 string; when `interrupt`
/// is false the platform should queue the sentence after whatever is already
/// speaking instead of cutting it off. A NULL `text` means "stop speaking".
typedef void (*PokeSpeechCallback)(const char* text, bool interrupt, void* userdata);
typedef void (*PokeLogCallback)(const char* text, void* userdata);
/// Synthetic touch the script drives through the compat shim's
/// joypad.setfrommnemonicstr (the Musical, the town map).
typedef void (*PokeTouchCallback)(int x, int y, bool down, void* userdata);

/// Owns the Lua state, the BizHawk-compatibility library surface and the
/// per-frame coroutine that runs the Pokémon Access `main.lua`.
///
/// One instance per emulator. The script reads the live melonDS::NDS, so the
/// NDS pointer must be set before the script is started, and cleared before
/// that NDS is destroyed. The pointer survives NDS::Reset().
class PokeScript
{
public:
    PokeScript();
    ~PokeScript();

    PokeScript(const PokeScript&) = delete;
    PokeScript& operator=(const PokeScript&) = delete;

    /// The NDS the compat API reads. Set on ROM load.
    void setNds(melonDS::NDS* nds);
    melonDS::NDS* nds() const { return ndsPtr; }

    void setSpeechCallback(PokeSpeechCallback cb, void* userdata);
    void setLogCallback(PokeLogCallback cb, void* userdata);
    void setTouchCallback(PokeTouchCallback cb, void* userdata);

    /// Concatenated shim + main.lua text. Stored, not compiled, until start().
    void setScriptText(std::string text);
    bool hasScript() const { return !script.empty(); }

    /// Compiles the script and runs it up to its first emu.frameadvance() yield.
    /// Returns false and sets lastError() on failure.
    bool start();

    /// One emulated frame's worth of script work: resumes the coroutine once.
    /// Safe to call when the script is not running (does nothing).
    void runFrame();

    /// Stops and tears the Lua state down. Idempotent.
    void stop();

    const std::string& lastError() const { return error; }

    /// True once the script has yielded for the first time and is being resumed
    /// each frame. Goes false if the script dies.
    bool isRunning() const { return scriptLoaded; }

    /// Frames the script has been resumed for (diagnostics).
    uint64_t framesRun() const { return framesRunCount; }

    // ---- input the host feeds the script ----
    /// DS buttons currently held, as the DS key bitmask (bit = 1 means pressed).
    void setJoypadState(uint32_t buttonsDown) { joypadDown = buttonsDown; }
    uint32_t joypadDownMask() const { return joypadDown; }
    /// Keys the script sees through input.get(), as the ASCII code of an
    /// upper-case letter ("R", "C", ...). Set true while held.
    void setHotkey(char key, bool down);
    bool isHotkeyHeld(char key) const;

    // ---- Lua-facing entry points (called from the C callbacks in the .cpp) ----
    void pushHeldKeys(lua_State* L) const;
    void log(const char* text);
    void speak(const char* text, bool interrupt);
    void stopSpeech();
    void touchDown(int x, int y);
    void touchUp();

private:
    friend struct PokeScriptAccess;

    // Raw lua_State pointers, kept opaque so lua.h stays out of the header.
    lua_State* L = nullptr;
    lua_State* coroutine = nullptr;

    PokeSpeechCallback speechCb = nullptr;
    void* speechUserdata = nullptr;
    PokeLogCallback logCb = nullptr;
    void* logUserdata = nullptr;
    PokeTouchCallback touchCb = nullptr;
    void* touchUserdata = nullptr;

    melonDS::NDS* ndsPtr = nullptr;

    std::string script;
    std::string error;

    uint32_t joypadDown = 0;
    char hotkeys[128] = {0};

    bool scriptLoaded = false;
    int touchX = 128;
    int touchY = 96;
    uint64_t framesRunCount = 0;
};

} // namespace MelonDSAndroid

#endif // POKESCRIPT_H
