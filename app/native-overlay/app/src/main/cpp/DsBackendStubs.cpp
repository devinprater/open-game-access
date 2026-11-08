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
//   - NES/SNES have no Android backend at all (a separate decision).
//
// MEASURED, NOT GUESSED: the undefined-symbol set of the fifteen Core
// translation units the DS glue compiles (pokecore, oga_core, systems,
// adapters and the DS-reachable adapter TUs) was inventoried with nm on
// the host before this file was written. Beyond the melonDS core, Lua,
// the PSP glue and libc, the ONLY missing symbols were the gba_*/nes_*/
// snes_* functions below — every function those three headers declare is
// stubbed here, so the set stays complete if a future Core TU reaches
// for one more of them.
//
// SEMANTICS: create() returns null, and pokecore's load path checks for
// exactly that and refuses the ROM with its own error — so a .gba/.nes/
// .smc file handed to the DS session by mistake fails cleanly, in words,
// instead of crashing or half-starting. Nothing here can be reached in
// normal play: DsGameSession only ever loads .nds files, and the
// launcher routes every other extension to its own console's session.

#include <cstdint>

#include "gba_core.h"
#include "mesen_core.h"
#include "snes_core.h"

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

// ---------------------------------------------------------------- NES ---

NesCore *nes_create(void) { return nullptr; }
void nes_destroy(NesCore *) {}

void nes_set_speech_callback(NesCore *, NesSpeechCallback, void *) {}
void nes_set_log_callback(NesCore *, NesLogCallback, void *) {}

bool nes_load_rom(NesCore *, const char *, const char *, char[16]) {
  return false;
}
int nes_read_audio(NesCore *, int16_t *, int) { return 0; }

bool nes_start(NesCore *) { return false; }
void nes_set_uncapped(NesCore *, bool) {}
void nes_stop(NesCore *) {}
bool nes_running(NesCore *) { return false; }
bool nes_frame(NesCore *) { return false; }

bool nes_framebuffer(NesCore *, int *width, int *height) {
  if (width) *width = 0;
  if (height) *height = 0;
  return false;
}
const uint8_t *nes_framebuffer_ptr(NesCore *) { return nullptr; }

void nes_set_button(NesCore *, int, bool) {}

bool nes_save_state(NesCore *, const char *) { return false; }
bool nes_load_state(NesCore *, const char *) { return false; }

unsigned long long nes_frames_completed(NesCore *) { return 0; }
int nes_is_paused(NesCore *) { return 0; }
unsigned nes_console_frame_count(NesCore *) { return 0; }
int nes_cpu_pc(NesCore *) { return 0; }
long long nes_cpu_cycles(NesCore *) { return 0; }
int nes_ppu_scanline(NesCore *) { return 0; }
double nes_fps(NesCore *) { return 0.0; }
int nes_emulation_speed(NesCore *) { return 0; }
void *nes_emulator_ptr(NesCore *) { return nullptr; }
unsigned nes_emu_frame_count(NesCore *) { return 0; }
const char *nes_frame_result(NesCore *) { return ""; }
int nes_console_is_current(NesCore *) { return 0; }
const char *nes_last_error(NesCore *) { return kNotInBuild; }

bool nes_read(NesCore *, uint32_t, int, uint32_t *) { return false; }
uint32_t nes_ram_base(NesCore *) { return 0; }
uint8_t nes_read_nametable(NesCore *, uint32_t) { return 0; }
uint8_t nes_read_prg_rom(NesCore *, uint32_t) { return 0; }

void nes_set_script_dir(NesCore *, const char *) {}
bool nes_script_loaded(NesCore *) { return false; }
const char *nes_reader_set(NesCore *) { return ""; }
uint32_t nes_rom_crc32(NesCore *) { return 0; }

// --------------------------------------------------------------- SNES ---

int snes_live_instances(void) { return 0; }
SnesCore *snes_create(void) { return nullptr; }
void snes_destroy(SnesCore *) {}

bool snes_load_rom(SnesCore *, const char *, const char *, char[16]) {
  return false;
}
uint32_t snes_rom_crc32(SnesCore *) { return 0; }

bool snes_start(SnesCore *) { return false; }
void snes_stop(SnesCore *) {}
bool snes_running(SnesCore *) { return false; }
bool snes_frame(SnesCore *) { return false; }
const char *snes_frame_result(SnesCore *) { return ""; }
void snes_reset(SnesCore *) {}

bool snes_framebuffer(SnesCore *, int *width, int *height) {
  if (width) *width = 0;
  if (height) *height = 0;
  return false;
}
const uint8_t *snes_framebuffer_ptr(SnesCore *) { return nullptr; }
const char *snes_video_result(SnesCore *) { return ""; }

void snes_set_button(SnesCore *, int, bool) {}
unsigned snes_controller_bits(SnesCore *) { return 0; }
unsigned snes_provider_bits(SnesCore *) { return 0; }
int snes_provider_calls(SnesCore *) { return 0; }
int snes_provider_last_type(SnesCore *) { return 0; }

bool snes_read(SnesCore *, uint32_t, int, uint32_t *) { return false; }
bool snes_write(SnesCore *, uint32_t, int, uint32_t) { return false; }
uint32_t snes_ram_base(void) { return 0; }
uint32_t snes_ram_size(void) { return 0; }
int snes_cpu_pc(SnesCore *) { return 0; }

void snes_set_uncapped(SnesCore *, bool) {}
unsigned long long snes_frames_completed(SnesCore *) { return 0; }
unsigned snes_console_frame_count(SnesCore *) { return 0; }
double snes_fps(SnesCore *) { return 0.0; }

bool snes_save_state(SnesCore *, const char *) { return false; }
bool snes_load_state(SnesCore *, const char *) { return false; }
const char *snes_last_error(SnesCore *) { return kNotInBuild; }

}  // extern "C"
