/* snes_core.h — C ABI for the Super Nintendo console (MesenCE).
 *
 * Mirrors mesen_core.h deliberately: the app and the front ends already know
 * how to drive a core with this contract, so the SNES speaks the same language.
 *
 * ⛔ IDENTITY IS THE ROM'S CONTENT. A Link to the Past is a 1991 cartridge with
 * no usable four-letter game code (0x7FB0 holds 01 8D 24 01), so snes_load_rom
 * writes "crc:<8 lowercase hex>" into code_out. That string is the adapter
 * registry's key and is never spoken.
 */
#ifndef MESEN_SNES_CORE_H
#define MESEN_SNES_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SnesCore SnesCore;

SnesCore *snes_create(void);
void snes_destroy(SnesCore *core);

/* Load a .sfc/.smc/.fig/.swc ROM. save_path becomes Mesen's home folder, the
 * same contract as nes_load_rom: without it Mesen throws "Home folder not
 * specified" from inside LoadRom and the caller sees only `false`.
 */
bool snes_load_rom(SnesCore *core, const char *rom_path, const char *save_path,
                   char code_out[16]);

/* The dump's CRC32, as Mesen computed it. 0 before a successful load. */
uint32_t snes_rom_crc32(SnesCore *core);

/* The last failure, in words. Empty string when nothing failed. */
const char *snes_last_error(SnesCore *core);

#ifdef __cplusplus
}
#endif

#endif /* MESEN_SNES_CORE_H */
