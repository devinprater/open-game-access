// DsBackendStubs.cpp — honest refusals for the backends this Android build
// does not carry, so the shared DS host can link.
//
// WHY THIS FILE EXISTS. The Nintendo DS host is Core/pokecore.cpp — the
// same translation unit iOS drives — and it is a MULTI-CONSOLE host: its
// ROM resolver can route a file to the DS, GBA, NES or SNES backend
// (Core/oga_core.cpp's wrappers), and the shared adapter registry
// (Core/adapters.cpp) names adapters for all of them. Linking pokecore
// into the Android frontend therefore references the gba_*/nes_*/snes_*
// C ABIs even though the DS session is the only caller this stage adds.
//
// The other consoles are NOT pokecore's business on Android:
//   - GB/GBA already has its own Android slice (GbGameSession over the
//     mGBA runner), which never goes through pokecore.
//   - PSP already has its own slice (psp_* is really compiled, in the
//     oga-ppsspp-glue objects — which is why there are no psp_* stubs).
//   - NES/SNES were stubbed here until the Mesen stage: the overlay's
//     section 10 now compiles the real MesenCE glue (Core/mesen_core.cpp
//     and Core/snes_core.cpp, the oga-mesen subproject) into the same
//     frontend library, so their stubs are GONE — defining them would
//     duplicate the real symbols in one .so. Only the GBA ABI remains
//     refused, because the GB session deliberately does not ride
//     pokecore on Android.
//
// MEASURED, NOT GUESSED: the undefined-symbol set of the fifteen Core
// translation units the DS glue compiles (pokecore, oga_core, systems,
// adapters and the DS-reachable adapter TUs) was inventoried with nm on
// the host before this file was written. Beyond the melonDS core, Lua,
// the PSP glue, the Mesen glue and libc, the ONLY missing symbols were
// the gba_* functions below — every function gba_core.h declares is
// stubbed here, so the set stays complete if a future Core TU reaches
// for one more of them.
//
// SEMANTICS: create() returns null, and pokecore's load path checks for
// exactly that and refuses the ROM with its own error — so a .gba file
// handed to the DS session by mistake fails cleanly, in words, instead
// of crashing or half-starting. Nothing here can be reached in normal
// play: DsGameSession only ever loads .nds files, and the launcher
// routes every other extension to its own console's session.

#include <cstdint>

#include "gba_core.h"

namespace {

const char *kNotInBuild = "that console is not in this Android build";

}  // namespace

// ---------------------------------------------------------------- GBA ---

extern "C" {

GbaCore *gba_create(void) { return nullptr; }
void gba_destroy(GbaCore *) {}

void gba_set_speech_callback(GbaCore *, GbaSpeechCallback, void *) {}
void gba_set_log_callback(GbaCore *, GbaLogCallback, void *) {}
void gba_set_sound_callback(GbaCore *, GbaSoundCallback, void *) {}
void gba_set_script_dir(GbaCore *, const char *) {}

bool gba_load_rom(GbaCore *, const char *, const char *, char[16], int *) {
  return false;
}
bool gba_set_exec_hook(GbaCore *, uint32_t, int) { return false; }
bool gba_clear_exec_hook(GbaCore *, uint32_t) { return false; }
bool gba_hooks_active(GbaCore *) { return false; }

bool gba_start(GbaCore *) { return false; }
void gba_stop(GbaCore *) {}
bool gba_running(GbaCore *) { return false; }
bool gba_frame(GbaCore *) { return false; }

void gba_set_audio_enabled(GbaCore *, bool) {}

bool gba_framebuffer(GbaCore *, int *width, int *height) {
  if (width) *width = 0;
  if (height) *height = 0;
  return false;
}
const uint8_t *gba_framebuffer_ptr(GbaCore *) { return nullptr; }

void gba_set_button(GbaCore *, int, bool) {}
void gba_set_hotkey(GbaCore *, const char *, bool) {}

bool gba_save_state(GbaCore *, const char *) { return false; }
bool gba_load_state(GbaCore *, const char *) { return false; }

unsigned long long gba_frames_completed(GbaCore *) { return 0; }
const char *gba_last_error(GbaCore *) { return kNotInBuild; }

int gba_read_audio(GbaCore *, int16_t *, int) { return 0; }
uint32_t gba_debug_read(GbaCore *, uint32_t, int) { return 0; }

}  // extern "C"
