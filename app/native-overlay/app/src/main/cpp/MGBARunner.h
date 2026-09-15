#ifndef MGBARUNNER_H
#define MGBARUNNER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct mCore;
struct mDebugger;
struct mDebuggerModule;

namespace MelonDSAndroid
{

class MGBAScript;

/// Owns one mGBA core plus the accessibility script host, and drives the frame
/// loop the way mGBA requires when a debugger with breakpoints is attached.
///
/// ⛔ THE FRAME LOOP IS NOT JUST `core->runFrame()`. gba.lua implements its
/// whole text/graphics pipeline as memory.registerexec callbacks on ROM
/// addresses (draw_text_to_tile_buffer at 0x80c6e40, copy_window_to_vram, the
/// menu-cursor readers, ...). Those are served here by mGBA hardware
/// breakpoints, and mGBA's contract is: once a core has a debugger with live
/// breakpoints, the FRONTEND must drive stepping —
///
///     core->step(core); debugger->platform->checkBreakpoints(debugger->platform);
///
/// (see mDebuggerRunTimeout in src/debugger/debugger.c). Calling runFrame()
/// instead runs ARMRunLoop, which executes an entire instruction batch without
/// ever checking for a breakpoint, so the callbacks would never fire and the
/// game would read as blank text with no error anywhere.
///
/// When no registerexec callbacks are live the fast path (runFrame) is used
/// again, so normal play costs nothing.
class MGBARunner
{
public:
    MGBARunner();
    ~MGBARunner();

    MGBARunner(const MGBARunner&) = delete;
    MGBARunner& operator=(const MGBARunner&) = delete;

    /// Loads a .gb/.gbc/.gba ROM from `romPath`, creating and resetting the
    /// core. `savePath` may be empty. Returns false if no mGBA core accepts the
    /// file. On success `platformName()` says which machine it is.
    bool loadRom(const std::string& romPath, const std::string& savePath);

    /// Points the accessibility script at the freshly loaded core.
    void unloadRom();

    /// Runs one emulated frame (fast path or breakpoint stepping path),
    /// then resumes the script's coroutine once.
    void runFrame();

    /// RGBA8888 framebuffer of the current frame, width 240x160 (GBA) or
    /// 160x144 (GB). Owned by the runner; valid until the next runFrame().
    const uint32_t* framebuffer(int* width, int* height, int* stride) const;

    mCore* core() const { return corePtr.get(); }
    MGBAScript* script() const { return scriptPtr.get(); }

    /// "gba" / "gb" / "" — what the core reports for the loaded cart.
    const std::string& platformName() const { return platform; }

    void setKeys(uint32_t keys) { pendingKeys = keys; }
    uint32_t keys() const { return pendingKeys; }

    /// 0 = GBA, 1 = GB, -1 = nothing loaded.
    int platformId() const { return platformEnum; }

private:
    struct CoreDeleter { void operator()(mCore*) const; };

    /// Detaches and frees the debugger in the only order that does not crash.
    /// See the comment on the definition.
    void teardownDebugger();

    std::unique_ptr<mCore, CoreDeleter> corePtr;
    std::unique_ptr<MGBAScript> scriptPtr;

    // The debugger is owned here, not by libmgba, and must outlive the core's
    // use of it. It is only attached when a ROM is loaded.
    mDebugger* debuggerPtr = nullptr;
    mDebuggerModule* debuggerModulePtr = nullptr;

    std::string romStorage;
    std::vector<uint32_t> videoBuffer;
    uint32_t pendingKeys = 0;
    uint32_t lastFrameCounter = 0;
    std::string platform;
    int platformEnum = -1;
    int frameWidth = 0;
    int frameHeight = 0;
    bool frameDumped = false;
};

/// Builds the accessibility script host for a loaded mGBA core: creates the
/// Lua state, installs the host API and runs pokemon.lua to its first yield.
/// Returns the host (owned by the caller) or nullptr on failure, with the error
/// in `errorOut`.
MGBAScript* StartMGBAccessibilityScript(
    MGBARunner* runner,
    std::string* errorOut);

} // namespace MelonDSAndroid

#endif // MGBARUNNER_H
