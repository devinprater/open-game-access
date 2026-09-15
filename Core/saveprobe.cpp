/*
 * saveprobe.cpp — is the cartridge's save memory actually allocated?
 *
 * Android always passes a real SRAM buffer into NDSCartArgs and requires the .sav
 * file to exist; this core passes std::nullopt. If SaveMemoryLength is 0 the cart
 * has NO save chip backing, and a Pokémon game that probes its save during boot
 * would spin forever without ever reaching graphics setup.
 */
#include "pokecore.h"
#include "NDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

melonDS::NDS* poke_debug_nds(PokeCore* core);
static void on_log(const char* t, void* u) { (void)t; (void)u; }
static void on_speech(const char* t, bool i, void* u) { (void)i;(void)u; }

int main(int argc, char** argv)
{
    const char* rom = argv[1];
    const char* sav = (argc > 2 && argv[2][0]) ? argv[2] : NULL;

    PokeCore* core = poke_create();
    poke_set_speech_callback(core, on_speech, NULL);
    poke_set_log_callback(core, on_log, NULL);
    if (!poke_load_rom(core, rom, sav)) { printf("load fail: %s\n", poke_last_error(core)); return 1; }

    melonDS::NDS* nds = poke_debug_nds(core);
    melonDS::u8*  mem = nds->GetNDSSave();
    melonDS::u32  len = nds->GetNDSSaveLength();
    printf("AFTER LOAD: GetNDSSave=%p  GetNDSSaveLength=%u (0x%X)\n",
           (void*) mem, len, len);
    printf("            -> save chip is %s\n", len ? "BACKED" : "EMPTY (no save memory!)");

    poke_set_script(core, "local a=1\n");
    if (!poke_start(core)) { printf("start fail\n"); return 1; }

    for (long f = 0; f < 4000; f++) if (!poke_frame(core)) break;

    mem = nds->GetNDSSave();
    len = nds->GetNDSSaveLength();
    printf("AFTER %d frames: len=%u  VRAMCNT_A=%02X  ARM9.PC=%08X\n",
           4000, len, nds->GPU.VRAMCNT[0], nds->ARM9.R[15]);
    if (mem && len)
    {
        melonDS::u32 nz = 0;
        for (melonDS::u32 i = 0; i < len; i++) if (mem[i]) nz++;
        printf("  save bytes written by the game: %u of %u\n", nz, len);
    }
    fflush(stdout);
    poke_destroy(core);
    return 0;
}
