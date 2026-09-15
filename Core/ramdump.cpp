/*
 * ramdump.cpp — dump EMULATED Main RAM so the guest code can be disassembled.
 *
 * The ROM file cannot be disassembled: retail DS ROMs store the ARM9 code
 * ENCRYPTED and melonDS decrypts it into Main RAM at boot. Main RAM is where the
 * code the CPUs actually execute lives, so that is what gets dumped.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; printf("[SPEAK] %s\n", t); }

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    long frames = (argc > 2) ? atol(argv[2]) : 3000;
    const char* out = (argc > 3) ? argv[3] : "/tmp/mainram.bin";

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, rom, NULL)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }
    poke_set_script(core, "local a = 1\n");
    if (!poke_start(core)) { printf("start fail\n"); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    for (long f = 0; f < frames; f++) if (!poke_frame(core)) break;

    /* MainRAMMaxSize is 4MB for NDS; Main RAM maps at 0x02000000. */
    melonDS::u32 size = nds->MainRAMMaxSize;
    FILE* f = fopen(out, "wb");
    if (!f) { printf("cannot write %s\n", out); return 1; }
    fwrite(nds->MainRAM, 1, size, f);
    fclose(f);

    printf("dumped %u bytes of Main RAM to %s\n", size, out);
    printf("base virtual address: 0x02000000\n");
    printf("ARM9.PC=%08X  ARM7.PC=%08X\n", nds->ARM9.R[15], nds->ARM7.R[15]);
    printf("ScreensEnabled=%d  VRAMCNT_A=%02X\n",
           (int) nds->GPU.ScreensEnabled, nds->GPU.VRAMCNT[0]);
    /* is ANY of Main RAM nonzero (i.e. did the game load code)? */
    melonDS::u32 nz = 0;
    melonDS::u32 first = 0;
    for (melonDS::u32 i = 0; i < size; i++)
        if (nds->MainRAM[i]) { nz++; if (!first) first = i; }
    printf("nonzero bytes in Main RAM: %u  first at offset 0x%X\n", nz, first);
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
