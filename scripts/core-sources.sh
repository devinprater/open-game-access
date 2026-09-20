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
gba_core.cpp mgba_version_stub.cpp
dbz_adapter.cpp
dissidia_adapter.cpp
adapters.cpp
"

# ---- mGBA (Game Boy Advance) ----
#
# ⛔ THIS IS A FIXED SUBSET, NOT THE WHOLE TREE, AND EVERY EXCLUSION IS LOAD-
# BEARING. It mirrors the translation units the host proof (September 2026)
# compiled and booted Emerald against; the full frontend brings Qt/SDL/OpenGL
# dependencies iOS cannot satisfy.
#
# Included: ARM core + GBA core + debugger backend + scripting HOST (context,
# console, socket...) + VFS + LZMA + inih + POSIX memory.
# Excluded:
#   version.c — CMake generates it from git; Core/mgba_version_stub.cpp
#       provides the same symbols with the pinned revision baked in.
#   src/script/storage.c and test/ — not in the proven set (storage needs a
#       frontend storage backend; nothing in the subset references it).
#   GB core (src/gb/* except audio) — M_CORE_GB is OFF until the GB core is
#       host-proven the way the GBA core was. .gb/.gbc ROMs fail loudly at
#       load, they do not silently run the wrong core.
MGBA="
src/arm/arm.c src/arm/debugger/cli-debugger.c src/arm/debugger/debugger.c
src/arm/debugger/memory-debugger.c src/arm/decoder-arm.c src/arm/decoder-thumb.c
src/arm/decoder.c src/arm/isa-arm.c src/arm/isa-thumb.c
src/core/bitmap-cache.c src/core/cache-set.c src/core/cheats.c src/core/config.c
src/core/core.c src/core/directories.c src/core/input.c src/core/interface.c
src/core/library.c src/core/lockstep.c src/core/log.c src/core/map-cache.c
src/core/mem-search.c src/core/rewind.c src/core/scripting.c src/core/serialize.c
src/core/sync.c src/core/thread.c src/core/tile-cache.c src/core/timing.c
src/debugger/access-logger.c src/debugger/cli-debugger-scripting.c
src/debugger/cli-debugger.c src/debugger/debugger.c src/debugger/parser.c
src/debugger/stack-trace.c src/debugger/symbols.c
src/feature/commandline.c src/feature/proxy-backend.c src/feature/thread-proxy.c
src/feature/updater.c src/feature/video-backend.c src/feature/video-logger.c
src/gb/audio.c
src/gba/audio.c src/gba/bios.c src/gba/cart/ereader.c src/gba/cart/gpio.c
src/gba/cart/matrix.c src/gba/cart/unlicensed.c src/gba/cart/vfame.c
src/gba/cheats.c src/gba/cheats/codebreaker.c src/gba/cheats/gameshark.c
src/gba/cheats/parv3.c src/gba/core.c src/gba/debugger/cli.c src/gba/dma.c
src/gba/extra/battlechip.c src/gba/extra/proxy.c src/gba/gba.c src/gba/hle-bios.c
src/gba/input.c src/gba/io.c src/gba/memory.c src/gba/overrides.c
src/gba/renderers/cache-set.c src/gba/renderers/common.c src/gba/renderers/gl.c
src/gba/renderers/software-bg.c src/gba/renderers/software-mode0.c
src/gba/renderers/software-obj.c src/gba/renderers/video-software.c
src/gba/savedata.c src/gba/serialize.c src/gba/sharkport.c src/gba/sio.c
src/gba/sio/dolphin.c src/gba/sio/gbp.c src/gba/sio/lockstep.c src/gba/timer.c
src/gba/video.c
src/platform/posix/memory.c
src/script/canvas.c src/script/console.c src/script/context.c src/script/input.c
src/script/image.c src/script/socket.c src/script/stdlib.c src/script/types.c
src/script/engines/lua.c
src/third-party/inih/ini.c
src/third-party/lzma/7zAlloc.c src/third-party/lzma/7zArcIn.c
src/third-party/lzma/7zBuf.c src/third-party/lzma/7zBuf2.c src/third-party/lzma/7zCrc.c
src/third-party/lzma/7zCrcOpt.c src/third-party/lzma/7zDec.c src/third-party/lzma/7zFile.c
src/third-party/lzma/7zStream.c src/third-party/lzma/Bcj2.c src/third-party/lzma/Bra.c
src/third-party/lzma/Bra86.c src/third-party/lzma/BraIA64.c src/third-party/lzma/CpuArch.c
src/third-party/lzma/Delta.c src/third-party/lzma/Lzma2Dec.c src/third-party/lzma/LzmaDec.c
src/third-party/lzma/Ppmd7.c src/third-party/lzma/Ppmd7Dec.c
src/util/audio-buffer.c src/util/audio-resampler.c src/util/circle-buffer.c
src/util/configuration.c src/util/convolve.c src/util/crc32.c src/util/elf-read.c
src/util/formatting.c src/util/gbk-table.c src/util/geometry.c src/util/hash.c
src/util/image.c src/util/image/export.c src/util/image/font.c src/util/image/png-io.c
src/util/interpolator.c src/util/md5.c src/util/patch.c src/util/patch-fast.c
src/util/patch-ips.c src/util/patch-ups.c src/util/ring-fifo.c src/util/sfo.c
src/util/sha1.c src/util/string.c src/util/table.c src/util/text-codec.c
src/util/vector.c src/util/vfs.c src/util/vfs/vfs-dirent.c src/util/vfs/vfs-fd.c
src/util/vfs/vfs-fifo.c src/util/vfs/vfs-lzma.c src/util/vfs/vfs-mem.c
"

# ⛔ AUDITED, NOT GENERATED. These are the C_DEFINES the host proof's CMake run
# produced, MINUS the six function checks that false-positive on Swift-Darwin
# clang (HAVE_POPCOUNT32/HAVE_CRC32/HAVE_SNPRINTF_L/HAVE_STRTOF_L/HAVE_FUTIMENS/
# HAVE_FUTIMES — implicit declarations that "succeed" because the compiler only
# warns). The host proof forced HAVE_POPCOUNT32=OFF etc. via mgba-configure.sh;
# these -D lines ARE that forcing, baked in. Do not "refresh" them from a
# CMake run on this machine without re-auditing.
MGBA_DEFS="-DBUILD_STATIC -DENABLE_DEBUGGERS -DENABLE_DIRECTORIES -DENABLE_SCRIPTING -DENABLE_VFS -DENABLE_VFS_FD -DHAVE_FREELOCALE -DHAVE_LOCALE -DHAVE_LOCALTIME_R -DHAVE_NEWLOCALE -DHAVE_PTHREAD_CREATE -DHAVE_PTHREAD_SETNAME_NP -DHAVE_PTHREAD_SET_NAME_NP -DHAVE_REALPATH -DHAVE_SETLOCALE -DHAVE_STRDUP -DHAVE_STRLCPY -DHAVE_STRNDUP -DHAVE_USELOCALE -DHAVE_VASPRINTF -DHAVE_XLOCALE -DLUA_VERSION_ONLY='\"5.4\"' -DM_CORE_GBA -DUSE_LUA -DUSE_LZMA -DUSE_PTHREADS -D_DARWIN_C_SOURCE"
