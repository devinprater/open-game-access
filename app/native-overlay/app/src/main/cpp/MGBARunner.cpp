/*
    MGBARunner.cpp — owns an mGBA core for the Android frontend.

    Responsibilities, and nothing else:

      * load a .gb/.gbc/.gba cart into the right core (mCoreFind does the
        sniffing: GB carts and GBA carts have different ROM signatures);
      * drive the frame, and know WHEN to use the stepping path (see
        MGBARunner.h — a live registerexec breakpoint means step+check);
      * produce an RGBA8888 framebuffer the shell can upload as an OpenGL
        texture, for cores whose renderer is a software one;
      * own the Lua accessibility host and keep it pointed at the live core.

    ⛔ THE ROM IS LOADED FROM MEMORY, NOT FROM A PATH. mCoreLoadFile goes
    through the core's VFS directory machinery, which on Android means
    mDirectorySetOpenPath + the platform's POSIX file layer, and that has to
    match mGBA's expectation of a single absolute path. Handing the core a
    VFileFromMemory over bytes we already read avoids that entirely and works
    for both GB and GBA carts. The bytes are kept alive for the cart's lifetime
    because the GB/GBA cores reference the ROM image in place (romBase /
    romBank), they do not copy it.
*/

#include "MGBARunner.h"
#include "MGBACore.h"

#include <android/log.h>
#define LOG_TAG "PokemonAccess"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>
#include <mgba-util/image.h>
#include <mgba/debugger/debugger.h>
}

namespace MelonDSAndroid
{

// ------------------------------------------------------------ debugger module
//
// One module, permanently registered with the debugger. It does two things:
// clears its own paused flag (so the core keeps running after an entry) and
// dispatches the breakpoint to the script host's registerexec table.

namespace
{
MGBARunner* gRunnerForDebugger = nullptr;
}

static void MGBAExecModuleEnter(struct mDebuggerModule* module,
                              enum mDebuggerEntryReason reason,
                              struct mDebuggerEntryInfo* info)
{
    // Always clear the pause flag: leaving it set would keep the debugger state
    // machine in DEBUGGER_PAUSED and the core would never advance again.
    module->isPaused = false;

    if (reason != DEBUGGER_ENTER_BREAKPOINT || !info) return;
    if (!gRunnerForDebugger) return;
    MGBAScript* script = gRunnerForDebugger->script();
    if (!script) return;
    script->onExecBreakpoint(info->address);
}

void MGBARunner::CoreDeleter::operator()(mCore* core) const
{
    if (!core) return;
    // ⛔ core->deinit() ALREADY FREES THE CORE (every _*CoreDeinit ends in
    // `free(core)`), and the debugger must be detached by
    // MGBARunner::teardownDebugger() BEFORE this runs — the detach path walks
    // the breakpoint list through the debugger's own point-owner Table.
    // Adding a free() here double-frees a pointer that came out of
    // anonymousMemoryMap (mmap), which Scudo then reports as
    // "invalid chunk state when deallocating address ..." and aborts the
    // process — on the GB path only, and only at teardown, which is exactly
    // how it presented.
    if (core->debugger) core->debugger = nullptr;
    if (core->deinit) core->deinit(core);
}

// ⛔ ORDER IS LOAD-BEARING:
//   1. stop the script (drops its Lua function refs),
//   2. unregister the module,
//   3. detach the debugger FROM THE CORE — this is what walks the breakpoint
//      list and frees each point through debugger->pointOwner,
//   4. only then mDebuggerDeinit() that Table and delete the debugger,
//   5. and only then destroy the core.
void MGBARunner::teardownDebugger()
{
    if (debuggerModulePtr && debuggerPtr)
        mDebuggerDetachModule(debuggerPtr, debuggerModulePtr);
    delete debuggerModulePtr;
    debuggerModulePtr = nullptr;

    if (debuggerPtr && corePtr)
    {
        if (corePtr->detachDebugger) corePtr->detachDebugger(corePtr.get());
        corePtr->debugger = nullptr;
    }

    if (debuggerPtr)
    {
        mDebuggerDeinit(debuggerPtr);
        delete debuggerPtr;
        debuggerPtr = nullptr;
    }
}

MGBARunner::MGBARunner()
{
    videoBuffer.assign(240 * 160, 0xFF000000u);
}

MGBARunner::~MGBARunner()
{
    if (scriptPtr) scriptPtr->stop();
    scriptPtr.reset();

    teardownDebugger();

    if (corePtr) corePtr.reset();
    if (gRunnerForDebugger == this) gRunnerForDebugger = nullptr;
}

bool MGBARunner::loadRom(const std::string& romPath, const std::string& savePath)
{
    unloadRom();

    FILE* f = std::fopen(romPath.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long length = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (length <= 0)
    {
        std::fclose(f);
        return false;
    }
    romStorage.resize((size_t) length);
    size_t nread = std::fread(&romStorage[0], 1, (size_t) length, f);
    std::fclose(f);
    if (nread != (size_t) length)
        return false;

    // Sniff which machine the cart is for, then create exactly that core.
    // mCoreFindVF needs a VFile; a const-memory view is enough and lets the
    // core keep the buffer by pointer.
    struct VFile* vf = VFileFromConstMemory(romStorage.data(), romStorage.size());
    if (!vf)
        return false;
    enum mPlatform detected = mCoreIsCompatible(vf);
    vf->close(vf);
    if (detected != mPLATFORM_GBA && detected != mPLATFORM_GB)
        return false;

    mCore* core = mCoreCreate(detected);
    if (!core)
        return false;
    corePtr.reset(core);

    core->init(core);
    mCoreInitConfig(core, nullptr);

    // ⛔ THE VIDEO BUFFER MUST BE SET BEFORE THE RESET THAT ASSOCIATES THE
    // RENDERER. GB's _GBVLPReset() only calls GBVideoAssociateRenderer() when
    // gbcore->renderer.outputBuffer is already non-NULL:
    //
    //     } else if (gbcore->renderer.outputBuffer) {
    //         GBVideoAssociateRenderer(&gb->video, renderer);
    //     }
    //
    // Setting it after the last reset leaves the renderer unassociated, so the
    // core runs perfectly (memory reads and the accessibility script all work)
    // while writing ZERO pixels into our buffer — a permanently black screen.
    // Measured: nonBlack=0 across 19,320 consecutive frames before this fix.
    //
    // The geometry is known here from the platform, and currentVideoSize is
    // re-read below to confirm it.
    frameWidth = detected == mPLATFORM_GBA ? 240 : 160;
    frameHeight = detected == mPLATFORM_GBA ? 160 : 144;
    videoBuffer.assign((size_t) frameWidth * frameHeight, 0xFF000000u);
    core->setVideoBuffer(core, reinterpret_cast<mColor*>(videoBuffer.data()),
                         (size_t) frameWidth);
    core->reset(core);

    // Cart insert, then a second reset: mGBA's own frontends do
    // new -> init -> reset -> loadROM -> reset -> start, and the RAM mask /
    // timing tables are only correct after the reset that follows the cart.
    // The video buffer is already set, so this reset associates the renderer.
    struct VFile* romVf = VFileFromConstMemory(romStorage.data(), romStorage.size());
    if (!romVf || !core->loadROM(core, romVf))
        return false;
    mCoreInitConfig(core, nullptr);
    core->reset(core);

    if (!savePath.empty())
    {
        struct VFile* saveVf = VFileOpen(savePath.c_str(), O_RDWR | O_CREAT);
        if (saveVf)
        {
            core->loadSave(core, saveVf);
        }
    }

    // Video: mGBA's software renderers write mColor pixels (XBGR8 without
    // COLOR_16_BIT) into the buffer we hand them.
    core->setVideoBuffer(core, reinterpret_cast<mColor*>(videoBuffer.data()), 240);

    // Input: start with nothing held.
    core->setKeys(core, 0);

    // ---- debugger, for memory.registerexec
    debuggerPtr = new mDebugger();
    mDebuggerInit(debuggerPtr);
    mDebuggerAttach(debuggerPtr, core);
    debuggerModulePtr = new mDebuggerModule();
    std::memset(debuggerModulePtr, 0, sizeof(mDebuggerModule));
    debuggerModulePtr->entered = MGBAExecModuleEnter;
    debuggerModulePtr->isPaused = false;
    debuggerModulePtr->needsCallback = false;
    mDebuggerAttachModule(debuggerPtr, debuggerModulePtr);
    debuggerPtr->state = DEBUGGER_RUNNING;

    gRunnerForDebugger = this;

    platformEnum = (int) core->platform(core);
    platform = platformEnum == mPLATFORM_GBA ? "gba" : (platformEnum == mPLATFORM_GB ? "gb" : "");

    unsigned w = 0, h = 0;
    core->currentVideoSize(core, &w, &h);
    frameWidth = (int) w;
    frameHeight = (int) h;
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        frameWidth = platformEnum == mPLATFORM_GBA ? 240 : 160;
        frameHeight = platformEnum == mPLATFORM_GBA ? 160 : 144;
    }
    if ((size_t) (frameWidth * frameHeight) > videoBuffer.size())
        videoBuffer.assign((size_t) frameWidth * frameHeight, 0xFF000000u);
    core->setVideoBuffer(core, reinterpret_cast<mColor*>(videoBuffer.data()), (size_t) frameWidth);

    // Diagnostics: confirm the video buffer actually gets written by the core.
    // A silent black screen is otherwise indistinguishable from "the core never
    // rendered", and this distinguishes the two in one log line.
    {
        unsigned fw = 0, fh = 0;
        core->currentVideoSize(core, &fw, &fh);
        unsigned w2 = 0, h2 = 0;
        core->currentVideoSize(core, &w2, &h2);
        LOGI("[pokemon-access-gb] video: platform=%s currentVideoSize=%ux%u frame=%dx%d bufPixels=%zu sizeof(mColor)=%zu",
             platform.c_str(), fw, fh, frameWidth, frameHeight,
             videoBuffer.size(), sizeof(mColor));
    }

    // The accessibility host.
    scriptPtr = std::make_unique<MGBAScript>();
    scriptPtr->setCore(core);
    scriptPtr->setDebugger(debuggerPtr, debuggerModulePtr);
    scriptPtr->refreshMemoryDomains();

    return true;
}

void MGBARunner::unloadRom()
{
    if (scriptPtr) scriptPtr->stop();
    scriptPtr.reset();

    teardownDebugger();

    corePtr.reset();

    romStorage.clear();
    platform.clear();
    platformEnum = -1;
    lastFrameCounter = 0;
}

void MGBARunner::runFrame()
{
    mCore* core = corePtr.get();
    if (!core) return;

    core->setKeys(core, pendingKeys);

    MGBAScript* script = scriptPtr.get();
    bool needsStepping = script && script->hasExecCallbacks() && debuggerPtr &&
                         debuggerPtr->platform && debuggerPtr->platform->checkBreakpoints;

    if (needsStepping)
    {
        // See MGBARunner.h: this is the only way registerexec callbacks fire.
        debuggerPtr->state = DEBUGGER_RUNNING;
        uint32_t startFrame = core->frameCounter(core);
        uint32_t guard = 0;
        do
        {
            core->step(core);
            debuggerPtr->platform->checkBreakpoints(debuggerPtr->platform);
        }
        while (core->frameCounter(core) == startFrame && ++guard < 1000000u);
    }
    else
    {
        core->runFrame(core);
    }

    lastFrameCounter = core->frameCounter(core);

    // One-shot PPM dump of the raw mGBA frame. `adb screencap` does NOT reliably
    // capture the emulator's EGL surface (it returns an all-black PNG even when
    // this buffer is full of pixels), so orientation/format questions must be
    // answered from the source data, not from a screenshot.
    // Dump the current frame on every 3000-frame mark, OVERWRITING. Pull the file
    // whenever the screen of interest is up. Used to answer orientation/format
    // questions from the source pixels, since `adb screencap` does not reliably
    // capture the emulator's EGL surface.
    if ((lastFrameCounter % 3000) == 0 && !videoBuffer.empty())
    {
        frameDumped = true;
        // Write into the app's own files area. Android 11+ scoped storage
        // refuses an app write to /sdcard root, which is why the first attempt
        // produced no file.
        std::string path = "/data/data/me.magnum.melonds.dev/cache/gbframe.ppm";
        FILE* fp = std::fopen(path.c_str(), "wb");
        if (fp)
        {
            std::fprintf(fp, "P6\n%d %d\n255\n", frameWidth, frameHeight);
            for (int y = 0; y < frameHeight; y++)
                for (int x = 0; x < frameWidth; x++)
                {
                    uint32_t p = videoBuffer[(size_t) (y * frameWidth + x)];
                    unsigned char rgb[3] = {
                        (unsigned char) (p & 0xFF),
                        (unsigned char) ((p >> 8) & 0xFF),
                        (unsigned char) ((p >> 16) & 0xFF),
                    };
                    std::fwrite(rgb, 1, 3, fp);
                }
            std::fclose(fp);
            LOGI("[pokemon-access-gb] dumped raw frame %u to %s (%dx%d)",
                 lastFrameCounter, path.c_str(), frameWidth, frameHeight);
        }
    }

    // Periodic diagnostic: is the core actually writing pixels? "All 0" means
    // the core never rendered into our buffer (a wiring problem); a non-zero
    // count means the frame is real and anything wrong is downstream (the
    // format/stride/GL path).
    if ((lastFrameCounter % 120) == 0)
    {
        size_t nonBlack = 0, nonZeroAlpha = 0;
        for (size_t i = 0; i < videoBuffer.size(); i++)
        {
            if ((videoBuffer[i] & 0x00FFFFFFu) != 0) nonBlack++;
            if ((videoBuffer[i] & 0xFF000000u) != 0) nonZeroAlpha++;
        }
        LOGI("[pokemon-access-gb] frame %u: buf=%zu pixels, nonBlack=%zu, nonZeroAlpha=%zu, sample=0x%08X",
             lastFrameCounter, videoBuffer.size(), nonBlack, nonZeroAlpha,
             videoBuffer.empty() ? 0u : videoBuffer[0]);
    }

    if (script)
        script->runFrame();
}

const uint32_t* MGBARunner::framebuffer(int* width, int* height, int* stride) const
{
    if (width) *width = frameWidth;
    if (height) *height = frameHeight;
    if (stride) *stride = frameWidth;
    return videoBuffer.data();
}

MGBAScript* StartMGBAccessibilityScript(MGBARunner* runner, std::string* errorOut)
{
    if (!runner || !runner->script())
    {
        if (errorOut) *errorOut = "no mGBA core is loaded";
        return nullptr;
    }
    if (!runner->script()->start())
    {
        if (errorOut) *errorOut = runner->script()->lastError();
        return nullptr;
    }
    return runner->script();
}

} // namespace MelonDSAndroid
