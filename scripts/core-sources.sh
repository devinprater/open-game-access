#!/usr/bin/env bash
# core-sources.sh — the single source list for the melonDS+Lua core, shared by
# the device build and the simulator build so the two cannot drift apart.
# (Two copies of this list is how a platform ends up missing a translation unit
# and failing at the FINAL link with an undefined symbol.)

CORE_CPP="
ARCodeFile.cpp ARDatabaseDAT.cpp AREngine.cpp ARM.cpp ARMInterpreter.cpp
ARMInterpreter_ALU.cpp ARMInterpreter_Branch.cpp ARMInterpreter_LoadStore.cpp
CP15.cpp CRC32.cpp DMA.cpp DMA_Timings.cpp
DSi.cpp DSi_AES.cpp DSi_Camera.cpp DSi_DSP.cpp DSi_I2C.cpp DSi_I2S.cpp
DSi_NAND.cpp DSi_NDMA.cpp DSi_NWifi.cpp DSi_SD.cpp DSi_SPI_TSC.cpp
FATIO.cpp FATStorage.cpp GBACart.cpp GBACartMotionPak.cpp
GPU.cpp GPU_Soft.cpp GPU2D.cpp GPU2D_Soft.cpp
GPU3D.cpp GPU3D_Soft.cpp GPU3D_Texcache.cpp
Mic.cpp NDS.cpp NDSCart.cpp ROMList.cpp FreeBIOS.cpp
RTC.cpp Savestate.cpp SPI.cpp SPI_Firmware.cpp SPU.cpp Utils.cpp
Wifi.cpp WifiAP.cpp
DSP_HLE/UcodeBase.cpp DSP_HLE/AACUcode.cpp DSP_HLE/G711Ucode.cpp DSP_HLE/GraphicsUcode.cpp
NDSCart/CartCommon.cpp NDSCart/CartRetail.cpp NDSCart/CartRetailNAND.cpp
NDSCart/CartRetailIR.cpp NDSCart/CartRetailBT.cpp NDSCart/CartSD.cpp
NDSCart/CartHomebrew.cpp NDSCart/CartR4.cpp
"

CORE_C="
fatfs/ff.c fatfs/ffsystem.c fatfs/ffunicode.c
sha1/sha1.c tiny-AES-c/aes.c xxhash/xxhash.c blip-buf/blip_buf.c
"

# teakra: the DSi DSP emulator, a hard dependency of DSi_DSP.cpp. Its C wrapper
# (teakra_c.cpp) is deliberately absent: the fork's copy references a method that
# no longer exists, and nothing in melonDS calls it.
TEAKRA="
teakra/src/teakra.cpp
teakra/src/ahbm.cpp teakra/src/apbp.cpp teakra/src/btdmp.cpp
teakra/src/disassembler.cpp teakra/src/disassembler_c.cpp
teakra/src/dma.cpp teakra/src/memory_interface.cpp
teakra/src/mmio.cpp teakra/src/parser.cpp teakra/src/processor.cpp
teakra/src/timer.cpp teakra/src/test_generator.cpp
"

# ---- Open Game Access glue ---------------------------------------------------
# ⛔ THIS LIST MUST LIVE HERE TOO. build-core.sh owned a private GLUE= line while
# build-sim.sh compiled ONLY poke_platform.cpp and pokecore.cpp — so the simulator
# archive contained NO adapters at all, while the device archive contained all of
# them. Nothing failed at compile time; the simulator link just reported
# `undefined symbol: oga::find_by_game_code`, which reads as a missing function
# rather than a build script that never compiled the file.
#
# Keep the adapters here, not in either build script: this is the one list the two
# platforms share, and the reason it exists is exactly this class of drift.
#
# Order is irrelevant to the linker (these are objects, not a library), but the
# adapters are grouped last because they are the feature layer sitting on top of
# pokecore.cpp's registry.
OGA_GLUE="
poke_platform.cpp pokecore.cpp
fe_access.cpp fe_adapter.cpp
gba_adapter.cpp
dbz_adapter.cpp
adapters.cpp
"
