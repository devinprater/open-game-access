/* snes_core.cpp — the Super Nintendo console, MesenCE underneath.
 *
 * Tasks 1-2 of docs/plans/alttp-snes-console.md: load and identify, then
 * frames, video, input and a clean stop.
 *
 * ⛔ EVERY FAILURE PATH SETS A SENTENCE. Mesen catches its own exceptions and
 * turns them into a bare `false`, so the caller must be told something real;
 * "Mesen refused the ROM" with no reason cost the NES core a session.
 */
#include "snes_core.h"

#include "pch.h"

#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/Interfaces/IConsole.h"
#include "Shared/BaseControlManager.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/SettingTypes.h"
#include "Shared/Video/BaseVideoFilter.h"
#include "Shared/RenderedFrame.h"
#include "Shared/MemoryType.h"
#include "Shared/SaveStateManager.h"
#include "SNES/SnesConsole.h"
#include "SNES/SnesCpu.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

class SnesInputProvider;

struct SnesCore {
    std::unique_ptr<Emulator> emu;
    std::shared_ptr<IConsole> console;

    bool loaded = false;
    bool running = false;
    std::thread runThread;
    std::atomic<bool> stopRequested{false};

    SnesInputProvider *inputProvider = nullptr;
    uint32_t framesSeenOnConsole = 0;
    unsigned long long frames = 0;

    std::string error;
    uint32_t crc = 0;       /* CRC32 of the ROM PAYLOAD, copier header skipped */
    long header_bytes = 0;  /* 512 for a headered .smc, 0 otherwise */

    uint8_t buttons[SNES_BTN_COUNT] = {0};  /* 1 = pressed, in SNES_BTN_* order */
    /* What the host last handed Mesen's controller, for the input proof. */
    unsigned providerBits = 0;
    int providerCalls = 0;
    int lastDeviceType = -1;
    std::vector<uint8_t> frameRGBA;         /* our own copy: Mesen's buffer dies with the frame */
    int fbW = 0, fbH = 0;
    /* ⛔ ONE FILTER, REUSED. SnesConsole::GetVideoFilter RETURNS A NEW OBJECT on
     * every call, so asking per frame leaks one filter per frame and the output
     * buffer changes identity underneath us. */
    BaseVideoFilter *videoFilter = nullptr;
};

/* Mesen runs this provider for EVERY device, including the SystemActionManager
 * whose buttons are Reset/Power. Claiming only a real SNES controller is what
 * stops a direction press from power-cycling the machine — the failure the NES
 * provider records at length. */
class SnesInputProvider : public IInputProvider {
public:
    explicit SnesInputProvider(SnesCore *core) : _core(core) {}

    bool SetInput(BaseControlDevice *device) override
    {
        if (!_core || !device) return false;
        _core->providerCalls++;
        _core->lastDeviceType = (int) device->GetControllerType();
        if (device->GetControllerType() != ControllerType::SnesController) return false;
        unsigned bits = 0;
        for (int i = 0; i < SNES_BTN_COUNT; i++) {
            bool down = _core->buttons[i] != 0;
            if (down) bits |= (1u << i);
            device->SetBitValue((uint8_t) i, down);
        }
        _core->providerBits = bits;
        return true;
    }

private:
    SnesCore *_core;
};

namespace {

/* ⛔ NEVER CACHE THE CONSOLE. Mesen's `_console` is a weak pointer and the
 * emulator replaces the console object on a power cycle or reset. Call this at
 * each use so a replacement is picked up for free. */
std::shared_ptr<IConsole> LiveConsole(SnesCore *c)
{
    return (c && c->emu) ? c->emu->GetConsole() : std::shared_ptr<IConsole>();
}

void SetError(SnesCore *c, const char *msg) { if (c) c->error = msg ? msg : "unknown error"; }

/* Why CaptureFrame produced no frame; printed by the probe when video is empty. */
static const char *g_captureResult = "ok";

/* ---- identity is the PAYLOAD, not the file ---------------------------------
 * Mesen's Emulator::GetCrc32 hashes the FILE. A .smc carrying the 512-byte
 * copier header that old dumpers wrote is the same cartridge data and hashes
 * differently — measured here: the same ROM with a 512-byte prefix gave
 * f59f60d1 instead of 777aac2f. Mesen strips the header for the cartridge
 * itself (Core/SNES/BaseCartridge.cpp), so the game runs either way; only the
 * id would be wrong, and the id is what selects a reader.
 *
 * A headered SNES dump is the only ambiguous case on this console and its
 * signature is size % 1024 == 512. The payload is what gets hashed.
 */
const std::array<uint32_t, 256> &CrcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

uint32_t Crc32Payload(const uint8_t *p, size_t n)
{
    const std::array<uint32_t, 256> &t = CrcTable();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool PayloadCrc(const char *path, uint32_t *out_crc, long *out_skip)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return false; }
    long skip = (size > 512 && (size % 1024) == 512) ? 512 : 0;
    if (fseek(f, skip, SEEK_SET) != 0) { fclose(f); return false; }

    std::vector<uint8_t> buf((size_t) (size - skip));
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got != buf.size()) return false;

    if (out_crc) *out_crc = Crc32Payload(buf.data(), buf.size());
    if (out_skip) *out_skip = skip;
    return true;
}

/* ⛔ THE SNES PPU DOES NOT HAND OUT RGBA. Its frame buffer is 16-bit palette
 * indices; the console's own video filter turns those into 0xAARRGGBB words.
 * So the filter must run, and the words must be byte-swapped into the RGBA
 * order every other OGA backend delivers. */
void CaptureFrame(SnesCore *c)
{
    std::shared_ptr<IConsole> console = LiveConsole(c);
    if (!console) { g_captureResult = "no console"; return; }

    if (!c->videoFilter) c->videoFilter = console->GetVideoFilter(true);
    BaseVideoFilter *filter = c->videoFilter;
    if (!filter) { g_captureResult = "no video filter"; return; }

    /* ⛔ THE PPU BUFFER IS LIVE. Without the emulator lock the emulation thread
     * may be drawing the next frame while this copies the last one, so the same
     * input produces different pixels run to run — measured: three identical
     * --run 600 processes gave hashes d1eab5e6, 58aea79e, 7589182b. The lock is
     * Mesen's own mechanism for exactly this (Emulator::AcquireLock). */
    EmulatorLock frameLock = c->emu->AcquireLock(false);

    PpuFrameInfo ppu = console->GetPpuFrame();
    if (!ppu.FrameBuffer || !ppu.Width || !ppu.Height) { g_captureResult = "empty ppu frame"; return; }

    /* ⛔ THE FILTER DOES NOT KNOW THE FRAME SIZE. It is told by whoever owns the
     * render loop (VideoDecoder / LuaApi both call these two before SendFrame).
     * Without them _baseFrameInfo stays 0x0, the output buffer is allocated at
     * size zero, and every frame is silently empty. */
    FrameInfo base;
    base.Width = ppu.Width;
    base.Height = ppu.Height;
    filter->SetBaseFrameInfo(base);
    filter->SetOverscan(OverscanDimensions{});

    FrameInfo info = filter->SendFrame((uint16_t *) ppu.FrameBuffer, ppu.FrameCount, 0,
                                       nullptr, false /* no overscan: the player sees the full frame */);
    uint32_t *src = filter->GetOutputBuffer();
    if (!src) { g_captureResult = "no output buffer"; return; }
    if (!info.Width || !info.Height) { g_captureResult = "filter returned no size"; return; }

    c->fbW = (int) info.Width;
    c->fbH = (int) info.Height;
    size_t n = (size_t) info.Width * (size_t) info.Height;
    c->frameRGBA.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        uint32_t v = src[i];
        c->frameRGBA[i * 4 + 0] = (uint8_t) ((v >> 16) & 0xFF);  /* R */
        c->frameRGBA[i * 4 + 1] = (uint8_t) ((v >> 8) & 0xFF);   /* G */
        c->frameRGBA[i * 4 + 2] = (uint8_t) (v & 0xFF);          /* B */
        c->frameRGBA[i * 4 + 3] = (uint8_t) ((v >> 24) & 0xFF);  /* A */
    }
}

} // namespace

extern "C" {

static std::atomic<int> g_liveInstances{0};

SnesCore *snes_create(void)
{
    SnesCore *c = new (std::nothrow) SnesCore();
    if (c) g_liveInstances.fetch_add(1);
    return c;
}

void snes_destroy(SnesCore *c)
{
    if (!c) return;
    if (c->running) snes_stop(c);
    if (c->videoFilter) { delete c->videoFilter; c->videoFilter = nullptr; }
    c->console.reset();
    c->emu.reset();
    delete c;
    g_liveInstances.fetch_sub(1);
}

bool snes_load_rom(SnesCore *c, const char *rom_path, const char *save_path,
                   char code_out[16])
{
    if (!c || !rom_path) return false;
    c->error.clear();
    c->crc = 0;
    c->header_bytes = 0;
    c->loaded = false;

    /* Identity first, so a failure here names the file rather than Mesen. */
    if (!PayloadCrc(rom_path, &c->crc, &c->header_bytes)) {
        c->error = "The ROM could not be read.";
        return false;
    }

    /* Never leave the home folder unset: Mesen throws from inside LoadRom and
     * the exception is swallowed by its own catch. */
    {
        std::string home = (save_path && *save_path) ? std::string(save_path) : std::string(".");
        FolderUtilities::SetHomeFolder(home);
    }

    c->emu.reset(new (std::nothrow) Emulator());
    if (!c->emu) { c->error = "Could not create the emulator."; return false; }

    /* Settings, memory registrations and the console factory are wired here.
     * Loading on an uninitialized Emulator fails in ways that look like a bad ROM. */
    c->emu->Initialize();

    /* ⛔ MESEN DOES NOT CONNECT A PAD BY ITSELF. Its front end does that from the
     * settings file; a library use starts with Port1.Type == None, so the console
     * builds NO controller, our IInputProvider is offered only "None" devices and
     * every button press is dropped. Measured: provider-calls=15, last-type=0
     * (ControllerType::None), pad bits all zero. Connect the standard pad before
     * the ROM is loaded so the console creates it. */
    if (EmuSettings *settings = c->emu->GetSettings()) {
        SnesConfig &cfg = settings->GetSnesConfig();
        if (cfg.Port1.Type == ControllerType::None)
            cfg.Port1.Type = ControllerType::SnesController;
    }

    VirtualFile rom((std::string(rom_path)));
    if (!rom.IsValid()) { c->error = "The ROM could not be opened."; return false; }

    bool loaded = false;
    try {
        loaded = c->emu->LoadRom(rom, VirtualFile(), false);
    } catch (const std::exception &ex) {
        c->error = std::string("Mesen threw while loading: ") + ex.what();
        return false;
    } catch (...) {
        c->error = "Mesen threw a non-std exception while loading.";
        return false;
    }
    if (!loaded) {
        c->error = "Mesen refused the ROM (LoadRom returned false).";
        return false;
    }

    c->console = c->emu->GetConsole();
    if (!c->console) { c->error = "The ROM loaded but no console was created."; return false; }

    /* ⛔ THE ID IS THE IMAGE MESEN RUNS, NOT A SECOND GUESS AT THE FILE.
     * Mesen decides for itself whether a dump carries the 512-byte copier header
     * (BaseCartridge.cpp scores eight candidate bases). The file-size heuristic
     * above can disagree with it, and then the id would describe bytes the
     * console is not executing. SnesPrgRom is the cartridge's ROM AFTER Mesen's
     * own decision, so hashing that makes the id and the running game the same
     * thing by construction. The file CRC stays only as a fallback for a build
     * where the block is not exposed. */
    {
        ConsoleMemoryInfo rom = c->emu->GetMemory(MemoryType::SnesPrgRom);
        if (rom.Memory && rom.Size > 0) {
            c->crc = Crc32Payload((const uint8_t *) rom.Memory, (size_t) rom.Size);
            c->header_bytes = 0;   /* Mesen's decision, not ours */
        }
    }
    if (code_out) std::snprintf(code_out, 16, "crc:%08x", c->crc);
    c->loaded = true;
    return true;
}

uint32_t snes_rom_crc32(SnesCore *c)
{
    return c ? c->crc : 0;
}

bool snes_start(SnesCore *c)
{
    if (!c || !c->loaded || !c->emu) { SetError(c, "Load a game first."); return false; }
    if (c->running) return true;

    /* Register the input provider BEFORE the loop starts, so the very first
     * frame already sees real input rather than a cleared pad. */
    if (c->console) {
        BaseControlManager *cm = c->console->GetControlManager();
        if (cm) {
            if (!c->inputProvider) c->inputProvider = new SnesInputProvider(c);
            cm->RegisterInputProvider(c->inputProvider);
        }
    }

    c->stopRequested = false;
    c->runThread = std::thread([c]() {
        try {
            c->emu->Run();
        } catch (...) {
            /* An exception escaping the emulation thread would terminate the process. */
            c->stopRequested = true;
        }
    });
    c->running = true;
    return true;
}

void snes_stop(SnesCore *c)
{
    if (!c) return;
    if (c->inputProvider) {
        if (c->console) {
            BaseControlManager *cm = c->console->GetControlManager();
            if (cm) cm->UnregisterInputProvider(c->inputProvider);
        }
        delete c->inputProvider;
        c->inputProvider = nullptr;
    }
    if (!c->emu) return;
    if (c->running) {
        c->emu->Stop(false, true, true);   /* sets the stop flag, so Run() returns */
        if (c->runThread.joinable()) c->runThread.join();
        c->running = false;
    }
    /* Battery RAM belongs to the cartridge that just stopped. */
    std::shared_ptr<IConsole> console = LiveConsole(c);
    if (console) console->SaveBattery();

    if (c->videoFilter) { delete c->videoFilter; c->videoFilter = nullptr; }
}

bool snes_running(SnesCore *c) { return c && c->running; }

void snes_reset(SnesCore *c)
{
    /* Emulator::Stop clears its console, and Reset dereferences it. A reset
     * of a stopped console is a no-op, not a crash. */
    if (!c || !c->emu || !c->running) return;
    c->emu->Reset();
}

/* Set at every exit of snes_frame, so a false return names its own cause. */
static const char *g_frameResult = "ok";

const char *snes_frame_result(SnesCore *c) { (void) c; return g_frameResult; }

bool snes_frame(SnesCore *c)
{
    if (!c || !c->loaded || !c->emu) { g_frameResult = "core not loaded"; return false; }

    std::shared_ptr<IConsole> console = LiveConsole(c);
    if (!console) {
        /* A console swap has a window with no console at all (weak pointer).
         * Wait, bounded, so a genuine absence still becomes an error. */
        const auto swapDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!console && std::chrono::steady_clock::now() < swapDeadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            console = LiveConsole(c);
        }
        if (!console) { SetError(c, "The SNES console went away and did not come back."); return false; }
    }

    /* First call after a load, or a replaced console: re-baseline on THIS
     * object's counter and then wait for it to move. Counting the baseline
     * itself as a frame would hand the caller a frame that never ran. */
    if (console.get() != c->console.get()) {
        c->console = console;
        c->framesSeenOnConsole = console->GetFrameCount();
    }

    uint32_t before = console->GetFrameCount();
    if (c->framesSeenOnConsole > 0 && before < c->framesSeenOnConsole)
        c->framesSeenOnConsole = before;   /* a reset took the counter backwards */

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (console->GetFrameCount() == before) {
        if (c->stopRequested) {
            g_frameResult = "stop requested";
            SetError(c, "The SNES core stopped unexpectedly.");
            return false;
        }
        /* Re-check for a swap INSIDE the wait: the old object's counter never
         * advances once Mesen has replaced it. */
        std::shared_ptr<IConsole> now = LiveConsole(c);
        if (now && now.get() != console.get()) {
            g_frameResult = "ok (console replaced mid-wait)";
            c->console = now;
            c->framesSeenOnConsole = now->GetFrameCount();
            c->frames++;
            CaptureFrame(c);
            return true;
        }
        if (now && now->GetFrameCount() != before) break;

        if (std::chrono::steady_clock::now() > deadline) {
            g_frameResult = "frame counter never advanced";
            SetError(c, "The SNES core did not produce a frame within 5 seconds.");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    g_frameResult = "ok";
    c->console = LiveConsole(c);
    c->framesSeenOnConsole = c->console ? c->console->GetFrameCount() : 0;
    c->frames++;
    CaptureFrame(c);
    return true;
}

bool snes_framebuffer(SnesCore *c, int *width, int *height)
{
    if (!c || c->fbW <= 0 || c->fbH <= 0) return false;
    if (width) *width = c->fbW;
    if (height) *height = c->fbH;
    return true;
}

const uint8_t *snes_framebuffer_ptr(SnesCore *c)
{
    return (c && !c->frameRGBA.empty()) ? c->frameRGBA.data() : nullptr;
}

const char *snes_video_result(SnesCore *c) { (void) c; return g_captureResult; }

void snes_set_button(SnesCore *c, int snes_button, bool down)
{
    if (!c || snes_button < 0 || snes_button >= SNES_BTN_COUNT) return;
    c->buttons[snes_button] = down ? 1 : 0;
}

bool snes_read(SnesCore *c, uint32_t addr, int width, uint32_t *out)
{
    if (!out) return false;
    /* A refusal must never look like a value: zero the caller's slot before
     * any check, so a caller that ignores the bool cannot read stale data. */
    *out = 0;
    /* ⛔ NO CONSOLE, NO ANSWER. After snes_stop the emulator has cleared its
     * console, and GetMemory would still hand back the dead core's cached
     * bytes -- a plausible answer from a console that is not running. */
    if (!c || !c->emu || !c->running) return false;
    if (width != 1 && width != 2 && width != 4) return false;

    /* Two blocks, both read directly rather than through the CPU bus (which has
     * side effects on MMIO): work RAM at 0x7E0000, and the cartridge's save RAM
     * at the LoROM window 0x700000. */
    uint32_t base = SNES_RAM_BASE;
    MemoryType type = MemoryType::SnesWorkRam;
    if (addr >= SNES_SRAM_BASE && addr < SNES_SRAM_BASE + SNES_SRAM_SIZE) {
        base = SNES_SRAM_BASE;
        type = MemoryType::SnesSaveRam;
    }

    ConsoleMemoryInfo mem = c->emu->GetMemory(type);
    if (!mem.Memory || mem.Size == 0) return false;
    if (addr < base) return false;

    uint32_t off = addr - base;
    if (off >= mem.Size) return false;
    /* A read that would cross the end is REFUSED, never wrapped: a wrapped
     * 16-bit read is a silently wrong answer the adapter cannot detect. */
    if ((uint64_t) off + (uint64_t) width > (uint64_t) mem.Size) return false;

    const uint8_t *p = (const uint8_t *) mem.Memory;
    uint32_t v = 0;
    for (int i = 0; i < width; i++)
        v |= (uint32_t) p[off + (uint32_t) i] << (8 * i);
    *out = v;
    return true;
}

uint32_t snes_ram_base(void) { return SNES_RAM_BASE; }
uint32_t snes_ram_size(void) { return SNES_RAM_SIZE; }

int snes_cpu_pc(SnesCore *c)
{
    std::shared_ptr<IConsole> console = LiveConsole(c);
    if (!console) return -1;
    SnesConsole *snes = dynamic_cast<SnesConsole *>(console.get());
    if (!snes || !snes->GetCpu()) return -1;
    return (int) snes->GetCpu()->GetState().PC;
}

unsigned snes_provider_bits(SnesCore *c) { return c ? c->providerBits : 0; }
int snes_provider_calls(SnesCore *c) { return c ? c->providerCalls : 0; }
int snes_provider_last_type(SnesCore *c) { return c ? c->lastDeviceType : -1; }

unsigned snes_controller_bits(SnesCore *c)
{
    std::shared_ptr<IConsole> console = LiveConsole(c);
    if (!console) return 0xFFFFFFFFu;
    BaseControlManager *cm = console->GetControlManager();
    if (!cm) return 0xFFFFFFFFu;
    for (const ControllerData &d : cm->GetPortStates()) {
        if (d.Port != 0 || d.Type != ControllerType::SnesController) continue;
        unsigned v = 0;
        for (size_t i = 0; i < d.State.State.size() && i < 4; i++)
            v |= (unsigned) d.State.State[i] << (8 * i);
        return v;
    }
    return 0xFFFFFFFFu;
}

void snes_set_uncapped(SnesCore *c, bool uncapped)
{
    if (!c || !c->emu) return;
    EmuSettings *settings = c->emu->GetSettings();
    if (!settings) return;
    settings->SetFlagState(EmulationFlags::MaximumSpeed, uncapped);
}

unsigned long long snes_frames_completed(SnesCore *c) { return c ? c->frames : 0; }

unsigned snes_console_frame_count(SnesCore *c)
{
    std::shared_ptr<IConsole> console = LiveConsole(c);
    return console ? console->GetFrameCount() : 0u;
}

double snes_fps(SnesCore *c) { return (c && c->emu) ? c->emu->GetFps() : -1.0; }

bool snes_save_state(SnesCore *c, const char *path)
{
    if (!c || !c->emu || !path || !*path) { SetError(c, "No save-state path given."); return false; }
    SaveStateManager *mgr = c->emu->GetSaveStateManager();
    if (!mgr) { SetError(c, "The emulator has no save-state manager."); return false; }
    /* Mesen's manager takes its own emulator lock, so this is safe while Run()
     * owns the emulation thread; it pauses, serialises and resumes. */
    if (!mgr->SaveState(std::string(path))) {
        SetError(c, "Mesen refused to write the save state.");
        return false;
    }
    return true;
}

bool snes_load_state(SnesCore *c, const char *path)
{
    if (!c || !c->emu || !path || !*path) { SetError(c, "No save-state path given."); return false; }
    SaveStateManager *mgr = c->emu->GetSaveStateManager();
    if (!mgr) { SetError(c, "The emulator has no save-state manager."); return false; }
    if (!mgr->LoadState(std::string(path))) {
        SetError(c, "Mesen refused to load the save state.");
        return false;
    }
    /* The console object may have been replaced by the load; follow it. */
    c->console = LiveConsole(c);
    c->framesSeenOnConsole = c->console ? c->console->GetFrameCount() : 0;
    return true;
}

int snes_live_instances(void) { return g_liveInstances.load(); }

const char *snes_last_error(SnesCore *c)
{
    return (c && !c->error.empty()) ? c->error.c_str() : "";
}

} // extern "C"
