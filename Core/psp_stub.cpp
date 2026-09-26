/* psp_stub.cpp -- link-time gate for the PSP backend (PPSSPP not vendored).
 *
 * WHY THIS EXISTS. pokecore.cpp calls the psp_* ABI unconditionally, so every
 * app link needs those symbols. The real implementation (Core/psp_core.cpp)
 * needs the full PPSSPP tree (Common/, Core/, GPU/) which bootstrap-deps.sh
 * does not fetch and core-sources.sh does not compile -- so the simulator link
 * failed with undefined symbol _psp_* from pokecore.o (run 36214860816).
 *
 * WHAT THIS IS. A no-op implementation of the exact psp_core.h ABI. It links,
 * loads no ROM, and reports "PSP core not included" on every path that needs
 * the emulator. The NDS and GBA paths are unaffected; a PSP ROM load fails
 * loudly at runtime instead of at link time.
 *
 * WHAT THIS IS NOT. Not the emulator. Dissidia and any future PSP adapter
 * cannot run against this stub -- their Host reads return 0 ("not there yet").
 * Promoting the real core means: pin a PPSSPP revision in bootstrap-deps.sh,
 * add the iOS-compatible IR-interpreter + software-GPU TU subset to
 * core-sources.sh (mirroring the audited mGBA subset pattern), compile
 * Core/psp_core.cpp instead of this stub, and re-prove with a headless boot.
 * Until then this stub is the intentional gate, not a silent fallback.
 */

#include "psp_core.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

struct PspCore {
    char error[128] = {0};
};

static const char kNotIncluded[] = "PSP core not included in this build (PPSSPP not vendored).";

PspCore *psp_create(void)
{
    PspCore *core = new PspCore();
    strncpy(core->error, kNotIncluded, sizeof(core->error) - 1);
    return core;
}

void psp_destroy(PspCore *core)
{
    delete core;
}

void psp_set_speech_callback(PspCore *, PspSpeechCallback, void *)
{
}

void psp_set_log_callback(PspCore *, PspLogCallback, void *)
{
}

bool psp_load_rom(PspCore *core, const char *, const char *, char code_out[16])
{
    if (code_out) code_out[0] = 0;
    if (core) strncpy(core->error, kNotIncluded, sizeof(core->error) - 1);
    return false;
}

bool psp_start(PspCore *core)
{
    if (core) strncpy(core->error, kNotIncluded, sizeof(core->error) - 1);
    return false;
}

void psp_stop(PspCore *)
{
}

bool psp_running(PspCore *)
{
    return false;
}

bool psp_frame(PspCore *)
{
    return false;
}

bool psp_framebuffer(PspCore *core, int *width, int *height)
{
    if (width) *width = PSP_FB_W;
    if (height) *height = PSP_FB_H;
    (void)core;
    return false;
}

const uint8_t *psp_framebuffer_ptr(PspCore *)
{
    return nullptr;
}

void psp_set_button(PspCore *, int, bool)
{
}

bool psp_save_state(PspCore *core, const char *)
{
    if (core) strncpy(core->error, kNotIncluded, sizeof(core->error) - 1);
    return false;
}

bool psp_load_state(PspCore *core, const char *)
{
    if (core) strncpy(core->error, kNotIncluded, sizeof(core->error) - 1);
    return false;
}

unsigned long long psp_frames_completed(PspCore *)
{
    return 0;
}

const char *psp_last_error(PspCore *core)
{
    if (core && core->error[0]) return core->error;
    return kNotIncluded;
}

uint32_t psp_debug_read(PspCore *, uint32_t, int)
{
    return 0;
}

void psp_debug_write(PspCore *, uint32_t, uint32_t, int)
{
}
