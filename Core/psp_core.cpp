/* psp_core.cpp — PlayStation Portable core for iOS (PPSSPP embedding).
 *
 * Runs a PSP game with the IR interpreter (no JIT: sideloaded iOS has no
 * executable-memory pages) and the software GPU with no GL context, exactly
 * the shape PPSSPP's own headless target proves upstream. Video comes back
 * on the CPU through the GPU debug interface; game input goes in through
 * __CtrlUpdateButtons; adapters read PSP memory through psp_debug_read.
 *
 * No Lua reader exists for PSP: the native game adapters (Dissidia, ...)
 * poll through the Host in pokecore.cpp. Speech callbacks exist on this
 * core's ABI for symmetry with gba_core, but nothing calls them yet.
 */

#include "psp_core.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/Log/LogManager.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/CoreTiming.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Software/SoftGpu.h"
#include "Core/Screenshot.h"

// ---- software-only GPU factory (replaces GPU/GPU.cpp) -----------------------
// GPU.cpp's factory references every hardware backend (GLES/Vulkan/D3D11) by
// construction, so compiling it would drag the GL context code this embedding
// exists to avoid. The subset therefore excludes GPU/GPU.cpp and this file
// provides the five symbols the kept code actually uses: the globals, the
// lifecycle pair System.cpp drives during PSP_Init/PSP_Shutdown, and the
// channel-name helper the framebuffer manager logs through. SOFTWARE is the
// only backend this embedding can construct; anything else fails loudly.
GPUStatistics gpuStats{};
GPUCommon *gpu = nullptr;

bool GPU_Init(GPUCore gpuCore, GraphicsContext *ctx, Draw::DrawContext *draw)
{
    if (gpuCore != GPUCORE_SOFTWARE || gpu) return false;
    gpu = new SoftGPU(ctx, draw);
    return gpu != nullptr;
}

void GPU_Shutdown()
{
    delete gpu;
    gpu = nullptr;
}

// ---- debugger-protocol shims (replaces Core/Debugger/WebSocket/*) -------------
// The WebSocket debugger serves the desktop UI over HTTP; this embedding has
// its own reads (psp_debug_read) and no HTTP server. Upstream blesses exactly
// this shape for builds without it (see the __LIBRETRO__ branch of
// Core/Debugger/WebSocket.h): the tick/notify calls in the run loop resolve
// to harmless no-ops instead of dragging the whole subscriber tree in.
#include "Core/Debugger/WebSocket.h"
void WebSocketDebuggerTick() {}
bool WebSocketDebuggerHasClients() { return false; }
void WebSocketNotifyBreakpointHit(const BreakpointHit &) {}

const char *RasterChannelToString(RasterChannel channel)
{
    return channel == RASTER_COLOR ? "COLOR" : "DEPTH";
}

// The software GPU never touches GL, but a few shared CPU-side helpers
// (vertex decoding) read the global GL-capability struct. There is no GL
// here, so it stays zero-initialized: no extensions, no version.
#include "Common/GPU/OpenGL/GLFeatures.h"
GLExtensions gl_extensions{};
std::string g_all_gl_extensions;
std::string g_all_egl_extensions;

struct PspCore {
    bool created = false;
    bool booted = false;
    bool running = false;
    char error[512] = {0};
    char code[16] = {0};
    std::string romPath;
    std::string saveRoot;
    std::string assetDir;
    uint32_t buttonsDown = 0;   // PSP_CTRL_* bits currently held
    uint64_t frames = 0;
    std::vector<uint8_t> rgba;  // 480x272x4 staging, served to the app
    void *gfxCtx = nullptr;     // owned NullGraphicsContext (lives till stop)
    PspSpeechCallback sayCb = nullptr;
    void *sayUser = nullptr;
    PspLogCallback logCb = nullptr;
    void *logUser = nullptr;
};

static void SetError(PspCore *core, const char *fmt, ...)
{
    if (!core) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(core->error, sizeof(core->error), fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// PPSSPP platform hooks. The core calls into Native* and System_* helpers
// that each frontend normally provides; this embedding answers them with
// the smallest true thing (no window, no audio, no dialogs).
// ---------------------------------------------------------------------------

void NativeFrame(GraphicsContext *) {}
void NativeResized() {}
bool NativeSaveSecret(std::string_view, std::string_view) { return false; }
std::string NativeLoadSecret(std::string_view) { return std::string(); }

void System_Toast(std::string_view) {}
void System_Notify(SystemNotification) {}
// No audio backend on this embedding yet (host proof or iOS without audio):
// the mixer runs so sound-gated game logic advances, but output goes nowhere.
void System_AudioGetDebugStats(char *, size_t) {}
void System_AudioClear() {}
void System_AudioPushSamples(const int32_t *, int, float) {}
void System_PostUIMessage(UIMessage, std::string_view) {}
void System_RunCallbackInWndProc(void (*)()) {}
bool System_MakeRequest(SystemRequestType, RequesterToken, std::string_view, std::string_view, std::string_view, int64_t, bool) { return false; }
bool System_MakeRequest(SystemRequestType, int, const std::string &, const std::string &, int64_t, int64_t) { return false; }
std::vector<std::string> System_GetCameraDeviceList() { return {}; }
int64_t System_GetPropertyInt(SystemProperty prop)
{
    // A 60Hz progressive display; the software GPU renders 480x272.
    if (prop == SYSPROP_DISPLAY_REFRESH_RATE) return 60000;
    if (prop == SYSPROP_DISPLAY_XRES) return 480;
    if (prop == SYSPROP_DISPLAY_YRES) return 272;
    return 0;
}
float System_GetPropertyFloat(SystemProperty) { return 0.0f; }
bool System_GetPropertyBool(SystemProperty prop)
{
    // No back button, no keyboard, no JIT: report the plain truth.
    if (prop == SYSPROP_HAS_BACK_BUTTON) return false;
    if (prop == SYSPROP_HAS_KEYBOARD) return false;
    if (prop == SYSPROP_CAN_JIT) return false;
    return false;
}
std::string System_GetProperty(SystemProperty) { return std::string(); }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty) { return {}; }
void System_SendMessage(std::string_view, std::string_view) {}
void System_AskForPermission(SystemPermission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission) { return PERMISSION_STATUS_GRANTED; }
bool System_AudioRecordingIsAvailable() { return false; }
bool System_AudioRecordingState() { return false; }

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

PspCore *psp_create(void)
{
    PspCore *core = new PspCore();
    core->created = true;
    core->rgba.resize((size_t)PSP_FB_W * PSP_FB_H * 4, 0);
    return core;
}

void psp_destroy(PspCore *core)
{
    if (!core) return;
    if (core->booted) PSP_Shutdown(true);
    delete (GraphicsContext *)core->gfxCtx;
    delete core;
}

void psp_set_speech_callback(PspCore *core, PspSpeechCallback cb, void *userdata)
{
    if (!core) return;
    core->sayCb = cb;
    core->sayUser = userdata;
}

void psp_set_log_callback(PspCore *core, PspLogCallback cb, void *userdata)
{
    if (!core) return;
    core->logCb = cb;
    core->logUser = userdata;
}

static std::string BaseName(const std::string &p)
{
    size_t i = p.find_last_of("/\\");
    return (i == std::string::npos) ? p : p.substr(i + 1);
}

bool psp_load_rom(PspCore *core, const char *rom_path, const char *save_path, char code_out[16])
{
    if (!core || !rom_path || !*rom_path || !code_out)
    {
        if (core) SetError(core, "No game file was given.");
        return false;
    }
    if (core->booted) PSP_Shutdown(true);
    core->booted = false;
    core->running = false;
    core->frames = 0;
    core->buttonsDown = 0;
    core->romPath = rom_path;
    core->saveRoot = (save_path && *save_path) ? save_path : ".";
    core->code[0] = 0;

    // The hardware has no BIOS: every boot file (fonts, syscalls tables the
    // HLE kernel consults) comes from PPSSPP's assets tree, bundled with the
    // app on iOS and pointed at the repo checkout for host proofs.
    if (core->assetDir.empty())
    {
        const char *env = getenv("PPSSPP_ASSETS");
        core->assetDir = (env && *env) ? env : "ppsspp-assets";
    }
    g_VFS.Clear();
    g_VFS.Register("", new DirectoryReader(Path(core->assetDir)));

    // Config FIRST: Load() resets everything to defaults (including the
    // logging toggle the log manager holds a pointer to), so logging init
    // must come after or it gets silently switched back off.
    static bool configInit = false;
    if (!configInit)
    {
        g_Config.Load("");
        configInit = true;
    }

    // PPSSPP's loader/kernel log to stderr (host proofs and Xcode console).
    // Initialized once; the app never shows this to the player.
    static bool logInit = false;
    if (!logInit)
    {
        g_Config.bEnableLogging = true;
        g_logManager.Init(&g_Config.bEnableLogging, false);
        for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++)
        {
            Log type = (Log)i;
            g_logManager.SetEnabled(type, true);
            g_logManager.SetLogLevel(type, LogLevel::LDEBUG);
        }
        g_logManager.EnableOutput(LogOutput::Printf);
        logInit = true;
    }

    // Stock defaults (sound mixing on: see below), software rendering,
    // memstick under the app's save dir.
    // Sound MIXING stays on even though the app has no audible output path yet:
    // games routinely gate menu input on sound-effect completion, and with
    // mixing off that ack never comes (title cursor froze after one move).
    g_Config.bEnableSound = true;
    g_Config.bSoftwareRendering = true;
    g_Config.memStickDirectory = Path(core->saveRoot) / "ppsspp-memstick";
    File::CreateDir(g_Config.memStickDirectory, true);
    CreateSysDirectories();
    g_Config.nandRootDirectory = GetSysDirectory(DIRECTORY_NAND);

    // Worker threads for the software rasterizer's binning. Without Init the
    // looper count is 0 and the rasterizer divides by it (SIGFPE on first
    // overlapping draw) — exactly what upstream's headless runner inits.
    // Once: the manager is a process-global singleton.
    static bool threadsInit = false;
    if (!threadsInit)
    {
        g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);
        threadsInit = true;
    }

    CoreParameter param;
    param.cpuCore = CPUCore::IR_INTERPRETER;   // no JIT pages on iOS
    param.gpuCore = GPUCORE_SOFTWARE;          // no GL context
    // The core (soft GPU) may touch this until shutdown, so it lives on the
    // core struct — deleting it after boot is a use-after-free.
    delete (GraphicsContext *)core->gfxCtx;
    core->gfxCtx = new NullGraphicsContext();
    param.graphicsContext = (GraphicsContext *)core->gfxCtx;
    param.enableSound = false;
    param.fileToStart = Path(core->romPath);
    param.startBreak = false;
    param.headLess = true;
    param.loadGameConfigs = false;
    param.renderScaleFactor = 1;
    param.renderWidth = PSP_FB_W;
    param.renderHeight = PSP_FB_H;
    param.pixelWidth = PSP_FB_W;
    param.pixelHeight = PSP_FB_H;
    param.fastForward = false;

    if (!PSP_InitStart(param))
    {
        SetError(core, "The PSP core refused to start this file.");
        delete param.graphicsContext;
        return false;
    }
    std::string err;
    while (PSP_InitUpdate(&err) == BootState::Booting)
    {
    }
    if (!PSP_IsInited())
    {
        SetError(core, "%s", err.empty() ? "The game could not be booted." : err.c_str());
        PSP_Shutdown(true);
        return false;
    }

    // Game ID from PARAM.SFO (ULUS10437 for Dissidia): the registry match
    // in pokecore.cpp depends on exactly this string.
    std::string id = g_paramSFO.GetValueString("DISC_ID");
    if (id.empty())
    {
        // Homebrew / unpacked executables carry no disc ID; fall back to
        // the file name so the load still identifies itself in logs.
        id = BaseName(core->romPath);
    }
    strncpy(core->code, id.c_str(), sizeof(core->code) - 1);
    strncpy(code_out, core->code, 16);

    core->booted = true;
    return true;
}

bool psp_start(PspCore *core)
{
    if (!core || !core->booted)
    {
        if (core) SetError(core, "Load a game first.");
        return false;
    }
    core->running = true;
    // The frontend owns the run state: a fresh boot leaves it down, and only
    // the first start lifts it (exactly like upstream's headless runner).
    coreState = CORE_RUNNING_CPU;
    if (gpu) gpu->BeginHostFrame(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape));
    return true;
}

void psp_stop(PspCore *core)
{
    if (!core) return;
    core->running = false;
    if (core->booted)
    {
        if (gpu) gpu->EndHostFrame();
        PSP_Shutdown(true);
        core->booted = false;
    }
}

bool psp_running(PspCore *core)
{
    return core && core->running && core->booted;
}

// One emulated frame: latch the pad, run to the next vblank, pump savestate
// completions. Mirrors the headless runner's stepping (CORE_NEXTFRAME flip),
// so a frame is a frame even with no display vsync anywhere.
bool psp_frame(PspCore *core)
{
    if (!core || !core->running || !core->booted) return false;

    __CtrlUpdateButtons(core->buttonsDown, ~core->buttonsDown);

    const int kChunk = (int)usToCycles(1000000 / 60);
    for (int i = 0; i < 40; i++)
    {
        PSP_RunLoopFor(kChunk);
        if (coreState == CORE_NEXTFRAME)
        {
            coreState = CORE_RUNNING_CPU;
            if (gpu)
            {
                gpu->EndHostFrame();
                gpu->BeginHostFrame(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape));
            }
            break;
        }
        if (coreState != CORE_RUNNING_CPU && coreState != CORE_NEXTFRAME)
            break;   // error / stepping / quit — report below
    }
    SaveState::Process();

    if (coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN)
    {
        SetError(core, "The game stopped unexpectedly (core state %d).", (int)coreState);
        core->running = false;
        return false;
    }
    core->frames++;
    return true;
}

// ---------------------------------------------------------------------------
// video: software GPU -> debug interface -> RGBA8888 staging
// ---------------------------------------------------------------------------

bool psp_framebuffer(PspCore *core, int *width, int *height)
{
    if (width) *width = PSP_FB_W;
    if (height) *height = PSP_FB_H;
    if (!core || !core->booted || !gpu) return false;

    GPUDebugBuffer buf;
    if (!gpu->GetOutputFramebuffer(buf)) { fprintf(stderr, "PSPDBG: GetOutputFramebuffer false\n"); return false; }
    // w/h are in/out: preset from the buffer or the convert yields an empty shot.
    u32 w = buf.GetStride(), h = buf.GetHeight();
    u8 *tmp = nullptr;
    const u8 *rgb = ConvertBufferToScreenshot(buf, false, tmp, w, h);
    if (!rgb || w == 0 || h == 0)
    {
        delete[] tmp;
        return false;
    }
    // ConvertBufferToScreenshot yields packed RGB24; expand to RGBA8888.
    // (w,h) follow the emulated display, expected 480x272; if a game ever
    // changes mode mid-frame the staging buffer simply follows it.
    size_t n = (size_t)w * h;
    if (core->rgba.size() != n * 4) core->rgba.resize(n * 4);
    uint8_t *dst = core->rgba.data();
    for (size_t i = 0; i < n; i++)
    {
        dst[i * 4 + 0] = rgb[i * 3 + 0];
        dst[i * 4 + 1] = rgb[i * 3 + 1];
        dst[i * 4 + 2] = rgb[i * 3 + 2];
        dst[i * 4 + 3] = 0xFF;
    }
    delete[] tmp;
    *width = (int)w;
    *height = (int)h;
    return true;
}

const uint8_t *psp_framebuffer_ptr(PspCore *core)
{
    if (!core || !core->booted) return nullptr;
    return core->rgba.data();
}

// ---------------------------------------------------------------------------
// input: app pad indices -> PSP_CTRL_* bits, latched on the next frame
// ---------------------------------------------------------------------------

static const uint32_t kPspButtonBits[PSP_BTN_COUNT] = {
    CTRL_SELECT,    // 0
    CTRL_START,     // 1
    CTRL_UP,        // 2
    CTRL_RIGHT,     // 3
    CTRL_DOWN,      // 4
    CTRL_LEFT,      // 5
    CTRL_TRIANGLE,  // 6
    CTRL_CIRCLE,    // 7
    CTRL_CROSS,     // 8
    CTRL_SQUARE,    // 9
    CTRL_LTRIGGER,  // 10
    CTRL_RTRIGGER,  // 11
};

void psp_set_button(PspCore *core, int psp_button, bool down)
{
    if (!core || psp_button < 0 || psp_button >= PSP_BTN_COUNT) return;
    if (down) core->buttonsDown |= kPspButtonBits[psp_button];
    else core->buttonsDown &= ~kPspButtonBits[psp_button];
}

// ---------------------------------------------------------------------------
// states + reads
// ---------------------------------------------------------------------------

bool psp_save_state(PspCore *core, const char *path)
{
    if (!core || !core->booted || !path || !*path) return false;
    bool done = false;
    SaveState::Save(Path(path), -1, [&](SaveState::Status status, std::string_view, std::string_view) {
        done = (status == SaveState::Status::SUCCESS);
    });
    for (int i = 0; i < 600 && !done; i++) SaveState::Process();
    if (!done) SetError(core, "Could not save the game state.");
    return done;
}

bool psp_load_state(PspCore *core, const char *path)
{
    if (!core || !core->booted || !path || !*path) return false;
    bool done = false;
    SaveState::Load(Path(path), -1, [&](SaveState::Status status, std::string_view, std::string_view) {
        done = (status == SaveState::Status::SUCCESS);
    });
    for (int i = 0; i < 600 && !done; i++) SaveState::Process();
    if (!done) SetError(core, "The saved state could not be loaded.");
    return done;
}

unsigned long long psp_frames_completed(PspCore *core)
{
    return core ? core->frames : 0;
}

const char *psp_last_error(PspCore *core)
{
    return (core && core->error[0]) ? core->error : "";
}

// Raw PSP memory for the adapter Host path. Validity is checked: unmapped
// reads yield 0 rather than crashing (the adapters range-guard themselves,
// e.g. Dissidia's InRam, and treat 0 as "not there yet").
uint32_t psp_debug_read(PspCore *core, uint32_t addr, int width)
{
    if (!core || !core->booted) return 0;
    if (width != 1 && width != 2 && width != 4) return 0;
    if (!Memory::IsValidAddress(addr)) return 0;
    const uint8_t *p = Memory::GetPointerOrNull(addr);
    if (!p) return 0;
    if (width == 1) return p[0];
    if (!Memory::IsValidAddress(addr + (uint32_t)width - 1)) return 0;
    if (width == 2) return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void psp_debug_write(PspCore *core, uint32_t addr, uint32_t value, int width)
{
    if (!core || !core->booted) return;
    if (width != 1 && width != 2 && width != 4) return;
    if (!Memory::IsValidAddress(addr)) return;
    if (!Memory::IsValidAddress(addr + (uint32_t)width - 1)) return;
    try {
        uint8_t *p = Memory::GetPointerWriteRangeOrException(addr, (uint32_t)width);
        p[0] = (uint8_t)value;
        if (width >= 2) p[1] = (uint8_t)(value >> 8);
        if (width == 4) { p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24); }
    } catch (...) { /* treat as no-op, like a failed read yielding 0 */ }
}

// ---- embedding-disabled service shims -------------------------------------
// The subset below excludes PPSSPP's desktop/service layers (telemetry,
// achievements, AVI capture, UPnP, VR, hardware-GPU probes, Lua console,
// native JIT selection). The kept code still references a few of their
// entry points, so this file provides them with embedding-honest behavior:
// features the frontend cannot use fail closed (false/empty/no-op), while
// anything affecting emulation state or save layout keeps upstream semantics.
// Each group notes why the real implementation is absent.

#include "Core/Reporting.h"
#include "Core/RetroAchievements.h"
#include "Core/AVIDump.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Notice.h"
#include "Common/UI/Context.h"
#include "naett.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/GPU/ShaderTranslation.h"

// Crash-report server: the embedding has no network and never opts in.
namespace Reporting {
void Init() {}
void Shutdown() {}
void DoState(PointerWrap &) {}
void NotifyDebugger() {}
void NotifyExecModule(const char *, int, uint32_t) {}
void ReportMessage(const char *, ...) {}
void ReportMessageFormatted(const char *, const char *) {}
bool ShouldLogNTimes(const char *, int) { return false; }
}

// RetroAchievements: the frontend has no login UI, so achievements can never
// activate; hardcore mode (which would disable savestates) stays off.
namespace Achievements {
void ChangeUMD(const Path &, FileLoader *) {}
void DoState(PointerWrap &) {}
bool HardcoreModeActive() { return false; }
bool WarnUserIfHardcoreModeActive(bool, std::string_view) { return false; }
}

// Upstream declares class AVIDump only when MOBILE_DEVICE is off
// (Core/AVIDump.h) — the shims follow the same guard.
#ifndef MOBILE_DEVICE
// AVI capture: SaveState only calls these when bDumpFrames is set, which the
// embedding never sets (there is no recorder UI).
bool AVIDump::Start(int, int) { return false; }
void AVIDump::Stop() {}
#endif
// UPnP port mapping: the embedding never maps ports, so adhoc stays
// local-only even though miniupnpc itself is compiled in.
void UPnP_Add(const char *, unsigned short, unsigned short) {}
void UPnP_Remove(const char *, unsigned short) {}
void UPnP_Notify() {}

// VR: never present in this embedding.
bool IsVREnabled() { return false; }
bool IsGameVRScene() { return false; }
bool IsFlatVRGame() { return false; }
bool IsImmersiveVRMode() { return false; }
bool IsBigScreenVRMode() { return false; }

// Hardware-GPU probes: the software-only factory above is the sole backend,
// so no hardware backend is ever available and shaders never need
// translation (the software renderer consumes them directly).
bool VulkanMayBeAvailable() { return false; }
bool TranslateShader(std::string *, ShaderLanguage, const ShaderLanguageDesc &,
                     TranslatedShaderMetadata *, std::string, ShaderLanguage,
                     ShaderStage, std::string *) { return false; }

// Single-threaded embedding: callers expect this queued on the UI thread,
// but the emulator and frontend share one thread, so run it inline.
void System_RunOnMainThread(std::function<void()> f) { f(); }

// Debugger web server port policy: there is no web server in the embedding
// (CmdLine parses the flag, nothing listens).
void WebServerSetRequireExactPort(bool) {}

// Native JIT selection: this embedding only ever runs
// CPUCore::IR_INTERPRETER, so MIPS.cpp's JIT branches are never taken. The
// null jit pointer is exactly what MemFault's guards expect, and
// DoDummyJitState keeps the verbatim upstream section layout so savestates
// stay well-formed (saves are embedding-local: same writer, same reader).
namespace MIPSComp {
JitInterface *jit = nullptr;
JitInterface *CreateNativeJit(MIPSState *, bool) { return nullptr; }
void DoDummyJitState(PointerWrap &p) {
    auto sec = p.Section("Jit", 1, 2);
    if (!sec)
        return;
    bool dummy = false;
    Do(p, dummy);
    if (sec >= 2) {
        dummy = true;
        Do(p, dummy);
    }
}
}

// x86 disassembler: only MemFault's crash-dump path and the debugger's JIT
// listing call this. libudis86 is excluded from the subset, so crash logs
// note the absence instead of printing disassembly.

// IR branch analysis (verbatim upstream bodies from JitCommon.cpp, which is
// excluded only because it also houses the native-JIT factory): the
// interpreter's block cache resolves not-taken targets through these.
namespace MIPSComp {
BranchInfo::BranchInfo(u32 pc, MIPSOpcode o, MIPSOpcode delayO, bool al, bool l)
		: compilerPC(pc), op(o), delaySlotOp(delayO), likely(l), andLink(al) {
		delaySlotInfo = MIPSGetInfo(delaySlotOp).value;
		delaySlotIsBranch = (delaySlotInfo & (IS_JUMP | IS_CONDBRANCH)) != 0;
	}

	u32 ResolveNotTakenTarget(const BranchInfo &branchInfo) {
		u32 notTakenTarget = branchInfo.compilerPC + 8;
		if ((branchInfo.delaySlotInfo & (IS_JUMP | IS_CONDBRANCH)) != 0) {
			// If a branch has a j/jr/jal/jalr as a delay slot, that is run if the branch is not taken.
			// TODO: Technically, in the likely case, we should somehow suppress andLink on this exit.
			bool isJump = (branchInfo.delaySlotInfo & IS_JUMP) != 0;
			// If the delay slot is a branch, likely skips it.
			if (isJump || !branchInfo.likely)
				notTakenTarget -= 4;

			// For a branch (not a jump), it actually should try the delay slot and take its target potentially.
			// This is similar to the VFPU case and has not been seen, so just report it.
			if (!isJump && SignExtend16ToU32(branchInfo.delaySlotOp) != SignExtend16ToU32(branchInfo.op) - 1)
				ERROR_LOG_REPORT(Log::JIT, "Branch in branch delay slot at %08x with different target", branchInfo.compilerPC);
			if (isJump && branchInfo.likely && (branchInfo.delaySlotInfo & (OUT_RA | OUT_RD)) != 0)
				ERROR_LOG_REPORT(Log::JIT, "Jump in likely branch delay slot with link at %08x", branchInfo.compilerPC);
	}
		return notTakenTarget;
}
}

std::vector<std::string> DisassembleX86(const u8 *, int) { return {}; }

// Version string: upstream generates this from git; pin the validated tree.

// CHD (MAME compressed disk) images: libchdr's CHD unit needs the LZMA
// *encoder*, which upstream does not vendor (ext/lzma-sdk is decode-only),
// so CHD cannot be opened in this embedding. The frontend only opens
// CSO/ISO; a CHD passed in fails cleanly at open time.
#include "libchdr/chd.h"
chd_error chd_open_core_file(core_file *, int, chd_file *, chd_file **) {
    return CHDERR_INVALID_PARAMETER;
}
void chd_close(chd_file *) {}
const char *chd_error_string(chd_error) { return "CHD unsupported in this build"; }
const chd_header *chd_get_header(chd_file *) { return nullptr; }
chd_error chd_read(chd_file *, uint32_t, void *) { return CHDERR_INVALID_PARAMETER; }


// On-screen notice widget: its measure/draw bodies live in UI/OnScreenDisplay.cpp
// (dropped with the desktop UI), so provide the verbatim wrappers over the
// kept MeasureNotice/RenderNotice helpers. Nothing in the embedding shows
// notices, but PopupScreens constructs one.
void NoticeView::GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const {
    float layoutWidth = layoutParams_->width;
    if (layoutWidth < 0) {
        layoutWidth = horiz.size;
    }
    ApplyBoundBySpec(layoutWidth, horiz);
    const int align = wrapText_ ? FLAG_WRAP_TEXT : 0;
    MeasureNotice(dc, level_, text_, detailsText_, iconName_, align, layoutWidth, &w, &h, &height1_);
}

void NoticeView::Draw(UIContext &dc) {
    dc.PushScissor(bounds_);
    const int align = wrapText_ ? FLAG_WRAP_TEXT : 0;
    RenderNotice(dc, bounds_, height1_, level_, text_, detailsText_, iconName_, align, 1.0f, OSDMessageFlags::None, 0.0f);
    dc.PopScissor();
}

// HTTP client (naett): needs libcurl, which has no place in an offline
// embedding, so every entry point fails closed. The frontend only opens
// local files; remote URLs and resolve requests never succeed.
void naettInit(naettInitData) {}
naettOption *naettMethod(const char *) { return nullptr; }
naettOption *naettHeader(const char *, const char *) { return nullptr; }
naettOption *naettBody(const char *, int) { return nullptr; }
naettOption *naettBodyWriter(naettWriteFunc, void *) { return nullptr; }
naettOption *naettTimeout(int) { return nullptr; }
naettOption *naettUserAgent(const char *) { return nullptr; }
naettReq *naettRequestWithOptions(const char *, int, const naettOption **) { return nullptr; }
naettRes *naettMake(naettReq *) { return nullptr; }
void naettFree(naettReq *) {}
int naettComplete(const naettRes *) { return 0; }
int naettGetStatus(const naettRes *) { return 0; }
int naettGetTotalBytesRead(naettRes *, int *) { return 0; }
void naettClose(naettRes *) {}
extern "C" int naettCurlLoad(void) { return 0; }

const char *PPSSPP_GIT_VERSION = "f293b10-oga";
