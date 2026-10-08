// gba-debug-spike.c — mGBA REAL exec hooks: do they fire without blocking, and at what cost?
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/debugger/debugger.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>
#include <mgba/gba/core.h>
#include <mgba/core/log.h>

// Silence mGBA's own logger: it floods stdout and hides the measurement.
static void quiet_log(struct mLogger* l, int cat, enum mLogLevel lvl, const char* fmt, va_list a) {
    (void)l;(void)cat;(void)lvl;(void)fmt;(void)a;
}
static struct mLogger QUIET = { .log = quiet_log };
#include <mgba/gb/core.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_hits = 0;
static uint32_t g_first_pc = 0, g_second_pc = 0;
static struct mCore* g_core = NULL;
static int32_t g_r1_at_first = -1;

static void onEntered(struct mDebuggerModule* module, enum mDebuggerEntryReason reason,
                      struct mDebuggerEntryInfo* info) {
    if (reason != DEBUGGER_ENTER_BREAKPOINT) return;
    if (g_hits == 0) {
        g_first_pc = info ? info->address : 0;
        if (g_core && g_core->readRegister) g_core->readRegister(g_core, "r1", &g_r1_at_first);
    } else if (g_hits == 1) {
        g_second_pc = info ? info->address : 0;
    }
    g_hits++;
    // ⛔ CLEAR isPaused OR THE EMULATOR BLOCKS. mDebuggerEnter sets module->isPaused = true
    // before calling this, and mDebuggerUpdatePaused then puts the whole debugger into
    // DEBUGGER_PAUSED -- after which mDebuggerRunTimeout waits on a timeout instead of running.
    // mGBA's own scripting layer does exactly this at the end of _scriptDebuggerEntered; a
    // callback that forgets it hangs the emulator at the first breakpoint.
    module->isPaused = false;
}
static void onUpdate(struct mDebuggerModule* m) { (void) m; }
static void onCustom(struct mDebuggerModule* m) { (void) m; }

static double run_dbg(struct mDebugger* d, long frames) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long f = 0; f < frames; f++) mDebuggerRunFrame(d);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1e6;
}

int main(int argc, char** argv) {
    const char* rom = (argc > 1) ? argv[1] : "emerald.gba";
    uint32_t bp  = (argc > 2) ? (uint32_t) strtoul(argv[2], NULL, 16) : 0x08000000;
    long frames  = (argc > 3) ? atol(argv[3]) : 90;

    mLogSetDefaultLogger(&QUIET);
    struct mCore* core = mCoreFind(rom);
    if (!core) { printf("no core\n"); return 1; }
    core->init(core);
    g_core = core;
    static uint32_t fb[240*160];
    core->setVideoBuffer(core, fb, 240);
    mCoreConfigInit(&core->config, "oga");
    struct mCoreOptions opts; memset(&opts, 0, sizeof(opts));
    mCoreConfigLoadDefaults(&core->config, &opts);
    mCoreConfigSetIntValue(&core->config, "sgb.borders", 0);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, fb, 240);

    struct VFile* vf = VFileOpen(rom, O_RDONLY);
    if (!vf || !core->loadROM(core, vf)) { printf("load failed\n"); return 1; }
    core->reset(core);

    // baseline timing
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long f = 0; f < frames; f++) core->runFrame(core);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double base = (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1e6;

    struct mCore* core2 = mCoreFind(rom);   // fresh instance, so the baseline did not advance it
    core2->init(core2); g_core = core2;
    core2->setVideoBuffer(core2, fb, 240);
    mCoreConfigInit(&core2->config, "oga2");
    mCoreConfigLoadDefaults(&core2->config, &opts);
    mCoreConfigSetIntValue(&core2->config, "sgb.borders", 0);
    mCoreLoadConfig(core2);
    core2->setVideoBuffer(core2, fb, 240);
    struct VFile* vf2 = VFileOpen(rom, O_RDONLY);
    core2->loadROM(core2, vf2);
    core2->reset(core2);

    struct mDebugger debugger; mDebuggerInit(&debugger);
    mDebuggerAttach(&debugger, core2);           // sets platform itself
    struct mDebuggerModule module; memset(&module, 0, sizeof(module));
    module.type = DEBUGGER_CUSTOM;
    module.entered = onEntered; module.update = onUpdate; module.custom = onCustom;
    module.isPaused = false;
    module.needsCallback = true;                 // <-- the non-blocking mode
    mDebuggerAttachModule(&debugger, &module);
    mDebuggerModuleSetNeedsCallback(&module);

    struct mBreakpoint b = { .address = bp, .segment = -1, .type = BREAKPOINT_HARDWARE };
    ssize_t id = debugger.platform->setBreakpoint(debugger.platform, &module, &b);

    double dbg = run_dbg(&debugger, frames);

    printf("ROM=%s  bp=0x%08X id=%zd\n", rom, bp, id);
    printf("baseline runFrame         : %.2f ms / %ld = %.4f ms/frame\n", base, frames, base/frames);
    printf("debugger+breakpoint       : %.2f ms / %ld = %.4f ms/frame\n", dbg, frames, dbg/frames);
    printf("HITS=%d  firstPC=0x%08X (r1=0x%08X)  secondPC=0x%08X\n",
           g_hits, g_first_pc, (unsigned) g_r1_at_first, g_second_pc);
    return g_hits > 0 ? 0 : 3;
}
