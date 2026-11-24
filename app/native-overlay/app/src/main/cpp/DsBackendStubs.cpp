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
// Every console pokecore can route to is now really compiled on Android:
//   - GB/GBC/GBA: Core/gba_core.cpp rides the DS glue (overlay section 9)
//     since the Game Boy session moved onto the shared Core, so its stubs
//     are GONE — defining them would duplicate the real symbols in one .so.
//   - PSP: psp_* is really compiled, in the oga-ppsspp-glue objects.
//   - NES/SNES: the overlay's section 11 compiles the real MesenCE glue
//     (Core/mesen_core.cpp and Core/snes_core.cpp, the oga-mesen
//     subproject) into the same frontend library.
//   - Genesis and PlayStation: their own subprojects (sections 10, 10b).
//
// The file stays as the one place a backend pokecore references, but this
// build does not carry, gets its refusal. Today that set is the Nintendo 64.
//
// MEASURED, NOT GUESSED, when a stub is added: inventory the undefined
// symbols of the Core translation units the DS glue compiles (nm on the
// host), and stub every function the missing backend's header declares,
// so the set stays complete if a future Core TU reaches for one more.
//
// SEMANTICS for any stub: create() returns null, and pokecore's load path
// checks for exactly that and refuses the ROM with its own error — so a
// file handed to the wrong session fails cleanly, in words, instead of
// crashing or half-starting.
//
// THE NINTENDO 64 IS REFUSED HERE, for now. Core/oga_core.cpp and
// pokecore.cpp call the n64_* ABI (Core/n64_core.h) since Mupen64Plus-Next
// joined the iOS build, and this Android build does not yet carry that core.
// Every function n64_core.h declares is stubbed below: n64_create() returns
// null and pokecore refuses the cartridge in words. When the Android N64
// slice lands, these stubs go, as the GBA and NES/SNES ones did.

#include <cstdint>

#include "n64_core.h"

namespace {

const char *kNotInBuild = "that console is not in this Android build";

}  // namespace

// ---------------------------------------------------------- Nintendo 64 ---

extern "C" {

N64Core *n64_create(void) { return nullptr; }
void n64_destroy(N64Core *) {}
bool n64_load_rom(N64Core *, const char *, const char *, char[16]) { return false; }
bool n64_start(N64Core *) { return false; }
void n64_stop(N64Core *) {}
bool n64_running(N64Core *) { return false; }
bool n64_frame(N64Core *) { return false; }
bool n64_framebuffer(N64Core *, int *width, int *height) {
  if (width) *width = 0;
  if (height) *height = 0;
  return false;
}
const uint8_t *n64_framebuffer_ptr(N64Core *) { return nullptr; }
void n64_set_button(N64Core *, int, bool) {}
void n64_set_analog(N64Core *, float, float) {}
int n64_read_audio(N64Core *, int16_t *, int) { return 0; }
bool n64_save_state(N64Core *, const char *) { return false; }
bool n64_load_state(N64Core *, const char *) { return false; }
unsigned long long n64_frames_completed(N64Core *) { return 0; }
const char *n64_last_error(N64Core *) { return kNotInBuild; }
bool n64_read(N64Core *, uint32_t, int, uint32_t *) { return false; }
const char *n64_core_version(void) { return kNotInBuild; }

}  // extern "C"
