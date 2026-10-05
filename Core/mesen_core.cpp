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
#include "Shared/BaseControlDevice.h"
#include "Shared/MemoryType.h"
#include "NES/NesConsole.h"
#include "NES/NesTypes.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"

#include <cctype>
#include <cstdio>
#include <cstring>
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
    unsigned long long frames = 0;
    std::string error;
    std::string gameCode;
    uint8_t buttons[NES_BTN_COUNT] = {0};  /* 1 = pressed, in NES_BTN_* order */
    std::vector<uint8_t> frameRGBA;        /* our own copy: Mesen's pointer dies with the frame */
    int fbW = 0, fbH = 0;
};

namespace {

void SetError(NesCore* c, const char* msg) { if (c) c->error = msg ? msg : "unknown error"; }

/* Our header's NES_BTN_* order -> the controller's own bit order. One table, so the mapping is
 * visible rather than implied by arithmetic. */
const uint8_t kBtnToBit[NES_BTN_COUNT] = {
    NES_BIT_A, NES_BIT_B, NES_BIT_SELECT, NES_BIT_START,
    NES_BIT_UP, NES_BIT_DOWN, NES_BIT_LEFT, NES_BIT_RIGHT,
};

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
    if (!c || !c->emu) return;
    if (c->running) {
        c->emu->Stop(false, true, true);     /* sets _stopFlag, so Run() returns */
        if (c->runThread.joinable()) c->runThread.join();
        c->running = false;
    }
}

bool nes_running(NesCore* c) { return c && c->running; }

bool nes_frame(NesCore* c) {
    if (!c || !c->loaded || !c->emu) return false;

    /* Press the pad through the console's OWN controller device, so Mesen sees real input from a
     * real device rather than a state poke. The adapter contract is read-only plus real buttons. */
    if (c->console) {
        BaseControlManager* cm = c->console->GetControlManager();
        if (cm) {
            std::shared_ptr<BaseControlDevice> pad = cm->GetControlDevice(0);
            if (pad) {
                for (int i = 0; i < NES_BTN_COUNT; i++)
                    pad->SetBitValue(kBtnToBit[i], c->buttons[i] != 0);
                pad->SetStateFromInput();
            }
        }
    }

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

uint32_t nes_ram_base(NesCore* c) {
    (void) c;
    return NES_RAM_BASE;
}

} // extern "C"
