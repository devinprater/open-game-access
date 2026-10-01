/*
 * host_harness_stub.cpp — link-time only. Host NDS harnesses (fedump,
 * fe_access, simrun, simboot) link Vendor/hostobj/*.o, which includes
 * pokecore.o referencing two backends the host build does not compile:
 *
 *   * psp_* — the real Core/psp_core.cpp needs the PPSSPP tree (ppspp lang
 *     in core-sources.sh); the NDS harnesses never boot a PSP game.
 *   * 7z archive symbols (SzArEx_Init and friends) — mGBA's vfs-lzma references
 *     ROM archives; the app links PPSSPP's identical SDK copy instead (see
 *     core-sources.sh: mGBA's third-party/lzma is deliberately absent). The
 *     harnesses only load plain .nds files, never archives.
 *
 * Every stub below ABORTS with a message when called. A silent no-op would
 * turn a real backend need into mysteriously dead behaviour; an abort says
 * exactly what is missing. If a harness ever legitimately needs one of these
 * paths, build the real backend (psp-host-proof.sh / the app link) instead of
 * softening this file.
 *
 * ⛔ NEVER ADD THIS FILE TO core-sources.sh. It exists so host-only tools link;
 * the app builds satisfy these symbols with the real implementations, and a
 * second definition would break the app link the way lua/xxhash did.
 */
#ifndef POKE_HOST
#error "host_harness_stub.cpp is host-harness-only (POKE_HOST). Do not compile it into app builds."
#endif

#include <stdio.h>
#include <stdlib.h>

namespace {

[[noreturn]] void harness_missing(const char* what)
{
    fprintf(stderr, "!! host harness: %s is not available in NDS harness builds\n", what);
    abort();
}

} // namespace

// ---- PSP backend (real: Core/psp_core.cpp) ---------------------------------
struct PspCore;
extern "C" {

PspCore* psp_create(void) { harness_missing("psp_create"); }
void psp_destroy(PspCore*) { harness_missing("psp_destroy"); }
void psp_set_speech_callback(PspCore*, void (*)(const char*, bool, void*), void*) { harness_missing("psp_set_speech_callback"); }
void psp_set_log_callback(PspCore*, void (*)(const char*, void*), void*) { harness_missing("psp_set_log_callback"); }
void psp_set_asset_dir(PspCore*, const char*) { harness_missing("psp_set_asset_dir"); }
bool psp_load_rom(PspCore*, const char*, const char*, char*) { harness_missing("psp_load_rom"); }
int psp_read_audio(PspCore*, short*, int) { harness_missing("psp_read_audio"); }
bool psp_start(PspCore*) { harness_missing("psp_start"); }
void psp_stop(PspCore*) { harness_missing("psp_stop"); }
bool psp_running(PspCore*) { harness_missing("psp_running"); }
bool psp_frame(PspCore*) { harness_missing("psp_frame"); }
bool psp_framebuffer(PspCore*, int*, int*) { harness_missing("psp_framebuffer"); }
const unsigned char* psp_framebuffer_ptr(PspCore*) { harness_missing("psp_framebuffer_ptr"); }
void psp_set_button(PspCore*, int, bool) { harness_missing("psp_set_button"); }
void psp_set_analog(PspCore*, float, float) { harness_missing("psp_set_analog"); }
bool psp_save_state(PspCore*, const char*) { harness_missing("psp_save_state"); }
bool psp_load_state(PspCore*, const char*) { harness_missing("psp_load_state"); }
unsigned long long psp_frames_completed(PspCore*) { harness_missing("psp_frames_completed"); }
const char* psp_last_error(PspCore*) { harness_missing("psp_last_error"); }
unsigned psp_debug_read(PspCore*, unsigned, int) { harness_missing("psp_debug_read"); }
void psp_debug_write(PspCore*, unsigned, unsigned, int) { harness_missing("psp_debug_write"); }

} // extern "C"

// ---- 7z archive symbols (real: PPSSPP ext/lzma-sdk in app builds) ------------
//
// Signatures are shape-compatible (pointer/int widths), not the SDK headers:
// the bodies abort before touching arguments, so nothing reads through them.
// If the harness ever needs real archive support, compile the SDK sources
// instead of extending this list.
extern "C" {

void SzArEx_Init(void*) { harness_missing("SzArEx_Init"); }
void SzArEx_Free(void*, void*) { harness_missing("SzArEx_Free"); }
unsigned long SzArEx_GetFileNameUtf16(const void*, unsigned long, void*) { harness_missing("SzArEx_GetFileNameUtf16"); }
int SzArEx_Extract(const void*, void*, unsigned*, unsigned*, void*, unsigned*, void*, void*, void*, void*) { harness_missing("SzArEx_Extract"); }
int SzArEx_Open(void*, void*, void*, void*, void*) { harness_missing("SzArEx_Open"); }
void LookToRead2_CreateVTable(void*, int) { harness_missing("LookToRead2_CreateVTable"); }
void CrcGenerateTable(void) { harness_missing("CrcGenerateTable"); }
int InFile_Open(void*, const char*) { harness_missing("InFile_Open"); }
int File_Close(void*) { harness_missing("File_Close"); }
void FileInStream_CreateVTable(void*) { harness_missing("FileInStream_CreateVTable"); }

} // extern "C"
