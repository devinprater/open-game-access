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
# PSP backend: the real Core/psp_core.cpp (IR interpreter + software GPU)
# satisfies the psp_* ABI pokecore.cpp calls. It rides PPSPP_GLUE with the
# ppspp lang, not plain cxx, because it needs the PPSSPP tree headers.
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

# ppspp_inc <ppsspp-src-root> — the include path every PPSSPP translation unit
# needs. ONE definition: build-core.sh, build-sim.sh and psp-host-proof.sh all
# call this, so the iOS and host-proof builds can never drift apart.
ppspp_inc() {
  echo "-I$1 -I$1/Common -I$1/ext -I$1/ext/snappy -I$1/ext/libpng17 -I$1/ext/zstd/lib -I$1/ext/cpu_features/include -I$1/ext/armips -I$1/ext/armips/ext/filesystem/include -I$1/ext/libchdr/include -I$1/ext/lua -I$1/ext/naett-lib -I$1/ext/libzip -I$1/ext/aemu_postoffice/client -I$1/ext/miniupnp/miniupnpc/include -I$1/ext/miniupnp/miniupnpc/src"
}

# ---- PPSSPP (PlayStation Portable) ----
#
# AUDITED SUBSET, host-proven September 2026: the real Core/psp_core.cpp (IR
# interpreter + software GPU, no GL, no native JIT) booted the Dissidia CSO
# (ULUS10437, 900/900 frames, live 480x272 framebuffer) linked against exactly
# these PPSSPP f293b10 translation units. scripts/psp-host-proof.sh reproduces
# that proof from these same lists; scripts/ppsspp-subset-test.sh guards them
# against upstream layout drift in CI.
#
# Excluded, and why:
#   native JIT backends (ARM/x86/RISC-V/...) — interpreter-only embedding.
#       Common/Thunk.cpp is JIT-only thunk emission too: nothing in the subset
#       references it (verified by object scan), and its x86 emitter calls
#       dangle on ARM64, which breaks the app link (the archive force-loads
#       every object). Dropped, not stubbed.
#   hardware GPU backends (GLES/Vulkan/D3D11) + GPU/GPU.cpp — its factory
#       references every hardware backend; psp_core.cpp provides the
#       software-only factory instead.
#   debugger WebSocket server, Reporting, RetroAchievements, AVIDump, UPnP,
#       VR, Lua console service calls — documented fail-closed shims in
#       psp_core.cpp (same shape upstream blesses for libretro builds).
#   CHD images — libchdr needs the LZMA *encoder*, which upstream does not
#       vendor (ext/lzma-sdk is decode-only); CHD opens fail cleanly.
#   HTTP (naett/libcurl), glslang shader translation, and every platform
#       frontend (SDL/Qt/Android/iOS UI). UPnP stays: the adhoc net stack
#       references it, and miniupnpc is small, portable C (its one generated
#       header, miniupnpcstrings.h, is produced at build time by miniupnp's
#       own updateminiupnpcstrings.sh, exactly like their Makefile does).
#   version reporting — upstream generates it from git; PPSSPP_GIT_VERSION is
#       pinned in psp_core.cpp instead.
PPSPP_CORE="
    /ext/disarm.cpp /ext/jpge/jpgd.cpp /ext/jpge/jpge.cpp /ext/loongarch-disasm.cpp
    /ext/riscv-disas.cpp Common/ABI.cpp Common/ArmCPUDetect.cpp Common/Buffer.cpp
    Common/CPUDetect.cpp Common/Crypto/md5.cpp Common/Crypto/sha1.cpp Common/Crypto/sha256.cpp
    Common/Data/Color/RGBAUtil.cpp Common/Data/Convert/ColorConv.cpp
    Common/Data/Convert/SmallDataConvert.cpp Common/Data/Encoding/Base64.cpp
    Common/Data/Encoding/Compression.cpp Common/Data/Encoding/Utf8.cpp
    Common/Data/Format/DDSLoad.cpp Common/Data/Format/IniFile.cpp Common/Data/Format/JSONReader.cpp
    Common/Data/Format/JSONWriter.cpp Common/Data/Format/PNGLoad.cpp Common/Data/Format/RIFF.cpp
    Common/Data/Format/ZIMLoad.cpp Common/Data/Format/ZIMSave.cpp Common/Data/Hash/Hash.cpp
    Common/Data/Text/Demangle.cpp Common/Data/Text/I18n.cpp Common/Data/Text/Parsers.cpp
    Common/Data/Text/WrapText.cpp Common/ExceptionHandlerSetup.cpp Common/FakeCPUDetect.cpp
    Common/File/DirListing.cpp Common/File/DiskFree.cpp Common/File/FileDescriptor.cpp
    Common/File/FileUtil.cpp Common/File/Path.cpp Common/File/PathBrowser.cpp
    Common/File/VFS/DirectoryReader.cpp Common/File/VFS/SevenZipFileReader.cpp
    Common/File/VFS/VFS.cpp Common/File/VFS/ZipFileReader.cpp Common/GPU/GPUBackendCommon.cpp
    Common/GPU/ShaderWriter.cpp Common/GhidraClient.cpp Common/Input/GestureDetector.cpp
    Common/Input/InputState.cpp Common/Log.cpp Common/Log/ConsoleListener.cpp
    Common/Log/LogManager.cpp Common/LoongArchCPUDetect.cpp Common/Math/Statistics.cpp
    Common/Math/curves.cpp Common/Math/expression_parser.cpp Common/Math/lin/matrix4x4.cpp
    Common/Math/lin/vec3.cpp Common/Math/math_util.cpp Common/MemArenaDarwin.cpp
    Common/MemArenaHorizon.cpp Common/MemArenaPosix.cpp Common/MemoryUtil.cpp
    Common/MemoryUtilHorizon.cpp Common/Net/HTTPClient.cpp Common/Net/HTTPHeaders.cpp
    Common/Net/HTTPNaettRequest.cpp Common/Net/HTTPRequest.cpp Common/Net/HTTPServer.cpp
    Common/Net/NetBuffer.cpp Common/Net/Resolve.cpp Common/Net/Sinks.cpp Common/Net/URL.cpp
    Common/Net/WebsocketServer.cpp Common/OSVersion.cpp Common/Profiler/Profiler.cpp
    Common/Render/AtlasGen.cpp Common/Render/DrawBuffer.cpp Common/Render/ManagedTexture.cpp
    Common/Render/Text/draw_text.cpp Common/Render/Text/draw_text_win.cpp
    Common/Render/TextureAtlas.cpp Common/RiscVCPUDetect.cpp Common/Serialize/Serializer.cpp
    Common/StringUtils.cpp Common/SysError.cpp Common/System/Display.cpp Common/System/OSD.cpp
    Common/System/Request.cpp Common/Thread/ParallelLoop.cpp Common/Thread/ThreadManager.cpp
    Common/Thread/ThreadUtil.cpp Common/TimeUtil.cpp
    Common/UI/AsyncImageFileView.cpp Common/UI/Context.cpp Common/UI/IconCache.cpp
    Common/UI/Notice.cpp Common/UI/PopupScreens.cpp Common/UI/Root.cpp Common/UI/Screen.cpp
    Common/UI/ScreenManager.cpp Common/UI/ScrollView.cpp Common/UI/TabHolder.cpp Common/UI/Tween.cpp
    Common/UI/UI.cpp Common/UI/UIScreen.cpp Common/UI/View.cpp Common/UI/ViewGroup.cpp
    Common/x64Analyzer.cpp Core/CmdLine.cpp Core/Compatibility.cpp Core/Config.cpp
    Core/ConfigSettings.cpp Core/ControlMapper.cpp Core/Core.cpp Core/CoreTiming.cpp
    Core/CwCheat.cpp Core/Debugger/Breakpoints.cpp Core/Debugger/DisassemblyManager.cpp
    Core/Debugger/LineInfo.cpp Core/Debugger/MemBlockInfo.cpp Core/Debugger/SymbolMap.cpp
    Core/Dialog/PSPDialog.cpp Core/Dialog/PSPGamedataInstallDialog.cpp Core/Dialog/PSPMsgDialog.cpp
    Core/Dialog/PSPNetconfDialog.cpp Core/Dialog/PSPNpSigninDialog.cpp
    Core/Dialog/PSPOskConstants.cpp Core/Dialog/PSPOskDialog.cpp
    Core/Dialog/PSPPlaceholderDialog.cpp Core/Dialog/PSPSaveDialog.cpp
    Core/Dialog/PSPScreenshotDialog.cpp Core/Dialog/SavedataParam.cpp Core/ELF/ElfReader.cpp
    Core/ELF/PBPReader.cpp Core/ELF/ParamSFO.cpp Core/ELF/PrxDecrypter.cpp Core/EmuThread.cpp
    Core/FileLoaders/CachingFileLoader.cpp Core/FileLoaders/DiskCachingFileLoader.cpp
    Core/FileLoaders/HTTPFileLoader.cpp Core/FileLoaders/LocalFileLoader.cpp
    Core/FileLoaders/RamCachingFileLoader.cpp Core/FileLoaders/RetryingFileLoader.cpp
    Core/FileLoaders/ZipFileLoader.cpp Core/FileSystems/BlobFileSystem.cpp
    Core/FileSystems/BlockDevices.cpp Core/FileSystems/DirectoryFileSystem.cpp
    Core/FileSystems/FileSystem.cpp Core/FileSystems/ISOFileSystem.cpp
    Core/FileSystems/MetaFileSystem.cpp Core/FileSystems/VirtualDiscFileSystem.cpp
    Core/FileSystems/tlzrc.cpp Core/Font/PGF.cpp Core/FrameTiming.cpp Core/HDRemaster.cpp
    Core/HLE/AtracCtx.cpp Core/HLE/AtracCtx2.cpp Core/HLE/HLE.cpp Core/HLE/HLEHelperThread.cpp
    Core/HLE/HLETables.cpp Core/HLE/KUBridge.cpp Core/HLE/NetAdhocCommon.cpp
    Core/HLE/NetInetConstants.cpp Core/HLE/Plugins.cpp Core/HLE/ReplaceTables.cpp
    Core/HLE/SocketManager.cpp Core/HLE/__sceAudio.cpp Core/HLE/proAdhoc.cpp
    Core/HLE/proAdhocServer.cpp Core/HLE/sceAac.cpp Core/HLE/sceAdler.cpp Core/HLE/sceAtrac.cpp
    Core/HLE/sceAudio.cpp Core/HLE/sceAudioRouting.cpp Core/HLE/sceAudiocodec.cpp
    Core/HLE/sceCcc.cpp Core/HLE/sceChkreg.cpp Core/HLE/sceChnnlsv.cpp Core/HLE/sceCtrl.cpp
    Core/HLE/sceDeflt.cpp Core/HLE/sceDisplay.cpp Core/HLE/sceDmac.cpp Core/HLE/sceFont.cpp
    Core/HLE/sceG729.cpp Core/HLE/sceGameUpdate.cpp Core/HLE/sceGe.cpp Core/HLE/sceHeap.cpp
    Core/HLE/sceHprm.cpp Core/HLE/sceHttp.cpp Core/HLE/sceImpose.cpp Core/HLE/sceIo.cpp
    Core/HLE/sceJpeg.cpp Core/HLE/sceKernel.cpp Core/HLE/sceKernelAlarm.cpp
    Core/HLE/sceKernelEventFlag.cpp Core/HLE/sceKernelHeap.cpp Core/HLE/sceKernelInterrupt.cpp
    Core/HLE/sceKernelMbx.cpp Core/HLE/sceKernelMemory.cpp Core/HLE/sceKernelModule.cpp
    Core/HLE/sceKernelMsgPipe.cpp Core/HLE/sceKernelMutex.cpp Core/HLE/sceKernelSemaphore.cpp
    Core/HLE/sceKernelThread.cpp Core/HLE/sceKernelTime.cpp Core/HLE/sceKernelVTimer.cpp
    Core/HLE/sceMd5.cpp Core/HLE/sceMp3.cpp Core/HLE/sceMp4.cpp Core/HLE/sceMpeg.cpp
    Core/HLE/sceMpegbase.cpp Core/HLE/sceMt19937.cpp Core/HLE/sceNet.cpp Core/HLE/sceNetAdhoc.cpp
    Core/HLE/sceNetAdhocMatching.cpp Core/HLE/sceNetApctl.cpp Core/HLE/sceNetInet.cpp
    Core/HLE/sceNetResolver.cpp Core/HLE/sceNet_lib.cpp Core/HLE/sceNp.cpp Core/HLE/sceNp2.cpp
    Core/HLE/sceOpenPSID.cpp Core/HLE/sceP3da.cpp Core/HLE/sceParseHttp.cpp Core/HLE/sceParseUri.cpp
    Core/HLE/scePauth.cpp Core/HLE/scePower.cpp Core/HLE/scePsmf.cpp Core/HLE/scePspNpDrm_user.cpp
    Core/HLE/sceReg.cpp Core/HLE/sceResmgr.cpp Core/HLE/sceRtc.cpp Core/HLE/sceSas.cpp
    Core/HLE/sceSfmt19937.cpp Core/HLE/sceSha256.cpp Core/HLE/sceSircs.cpp Core/HLE/sceSsl.cpp
    Core/HLE/sceUmd.cpp Core/HLE/sceUsb.cpp Core/HLE/sceUsbAcc.cpp Core/HLE/sceUsbCam.cpp
    Core/HLE/sceUsbGps.cpp Core/HLE/sceUsbMic.cpp Core/HLE/sceUtility.cpp Core/HLE/sceVaudio.cpp
    Core/HLE/sceVideocodec.cpp Core/HLE/sceVshBridge.cpp Core/HW/AsyncIOManager.cpp
    Core/HW/Atrac3Standalone.cpp Core/HW/AvcDecoder.cpp Core/HW/BufferQueue.cpp Core/HW/Camera.cpp
    Core/HW/Display.cpp Core/HW/GpioMMIO.cpp Core/HW/GranularMixer.cpp Core/HW/MediaEngine.cpp
    Core/HW/MemoryStick.cpp Core/HW/MpegDemux.cpp Core/HW/SasAudio.cpp Core/HW/SasReverb.cpp
    Core/HW/SimpleAudioDec.cpp Core/HW/StereoResampler.cpp Core/Instance.cpp Core/KeyMap.cpp
    Core/KeyMapDefaults.cpp Core/Loaders.cpp Core/MIPS/IR/IRAnalysis.cpp Core/MIPS/IR/IRFrontend.cpp
    Core/MIPS/IR/IRInst.cpp Core/MIPS/IR/IRInterpreter.cpp Core/MIPS/IR/IRPassSimplify.cpp
    Core/MIPS/Interpreter.cpp Core/MIPS/InterpreterDispatch.cpp Core/MIPS/InterpreterVFPU.cpp
    Core/MIPS/MIPS.cpp Core/MIPS/MIPSAnalyst.cpp Core/MIPS/MIPSCodeUtils.cpp
    Core/MIPS/MIPSDebugInterface.cpp Core/MIPS/MIPSDis.cpp Core/MIPS/MIPSDisVFPU.cpp
    Core/MIPS/MIPSStackWalk.cpp Core/MIPS/MIPSTables.cpp Core/MIPS/MIPSTracer.cpp
    Core/MIPS/MIPSVFPUFallbacks.cpp Core/MIPS/MIPSVFPUUtils.cpp Core/MIPS/fake/FakeJit.cpp
    Core/MemFault.cpp Core/MemMap.cpp Core/MemMapFunctions.cpp Core/PSPLoaders.cpp
    Core/SaveState.cpp Core/SaveStateRewind.cpp Core/Screenshot.cpp Core/System.cpp
    Core/TiltEventProcessor.cpp Core/Util/AtracTrack.cpp Core/Util/AudioFormat.cpp
    Core/Util/BlockAllocator.cpp Core/Util/DisArm64.cpp Core/Util/GameDB.cpp
    Core/Util/GameManager.cpp Core/Util/KL4E.cpp Core/Util/MemStick.cpp Core/Util/PPGeDraw.cpp
    Core/Util/PSARUnpack.cpp Core/Util/PathUtil.cpp Core/Util/PkgUnpack.cpp
    Core/Util/RecentFiles.cpp Core/Util/VideoPlayer.cpp Core/WaveFile.cpp
    GPU/Common/DepalettizeShaderCommon.cpp GPU/Common/DepthBufferCommon.cpp
    GPU/Common/DepthRaster.cpp GPU/Common/Draw2D.cpp GPU/Common/DrawEngineCommon.cpp
    GPU/Common/FragmentShaderGenerator.cpp GPU/Common/FramebufferManagerCommon.cpp
    GPU/Common/GPUDebugInterface.cpp GPU/Common/GPUStateUtils.cpp GPU/Common/IndexGenerator.cpp
    GPU/Common/PostShader.cpp GPU/Common/PresentationCommon.cpp
    GPU/Common/ReinterpretFramebuffer.cpp GPU/Common/ReplacedTexture.cpp GPU/Common/ShaderCommon.cpp
    GPU/Common/ShaderId.cpp GPU/Common/ShaderUniforms.cpp GPU/Common/SoftwareTransformCommon.cpp
    GPU/Common/SplineCommon.cpp GPU/Common/StencilCommon.cpp GPU/Common/TextureCacheCommon.cpp
    GPU/Common/TextureDecoder.cpp GPU/Common/TextureReplacer.cpp GPU/Common/TextureScalerCommon.cpp
    GPU/Common/TextureShaderCommon.cpp GPU/Common/TransformCommon.cpp
    GPU/Common/VertexDecoderArm.cpp GPU/Common/VertexDecoderArm64.cpp
    GPU/Common/VertexDecoderCommon.cpp GPU/Common/VertexDecoderHandwritten.cpp
    GPU/Common/VertexDecoderLoongArch64.cpp GPU/Common/VertexDecoderRiscV.cpp
    GPU/Common/VertexDecoderX86.cpp GPU/Common/VertexShaderGenerator.cpp
    GPU/Debugger/Breakpoints.cpp GPU/Debugger/Debugger.cpp GPU/Debugger/GECommandTable.cpp
    GPU/Debugger/Playback.cpp GPU/Debugger/Record.cpp GPU/Debugger/State.cpp
    GPU/Debugger/Stepping.cpp GPU/GPUCommon.cpp GPU/GPUState.cpp GPU/GeConstants.cpp
    GPU/GeDisasm.cpp GPU/Math3D.cpp GPU/Software/BinManager.cpp GPU/Software/Clipper.cpp
    GPU/Software/DrawPixel.cpp GPU/Software/DrawPixelX86.cpp GPU/Software/FuncId.cpp
    GPU/Software/Lighting.cpp GPU/Software/Rasterizer.cpp GPU/Software/RasterizerRectangle.cpp
    GPU/Software/RasterizerRegCache.cpp GPU/Software/Sampler.cpp GPU/Software/SamplerX86.cpp
    GPU/Software/SoftGpu.cpp GPU/Software/TransformUnit.cpp Core/MIPS/IR/IRCompALU.cpp
    Core/MIPS/IR/IRCompBranch.cpp Core/MIPS/IR/IRCompFPU.cpp Core/MIPS/IR/IRCompLoadStore.cpp
    Core/MIPS/IR/IRCompVFPU.cpp Core/MIPS/IR/IRRegCache.cpp Core/MIPS/IR/IRJit.cpp
    Core/MIPS/JitCommon/JitBlockCache.cpp Common/GPU/thin3d.cpp Common/File/AndroidContentURI.cpp
    Core/Replay.cpp Common/ArmEmitter.cpp Common/Arm64Emitter.cpp Common/x64Emitter.cpp
    Core/LuaContext.cpp Core/MIPS/JitCommon/JitState.cpp
"
PPSPP_EXT_CPP="
    ext/gason/gason.cpp ext/basis_universal/basisu_transcoder.cpp ext/armips/Core/Types.cpp
    ext/aemu_postoffice/client/mutex_impl_cpp.cpp ext/aemu_postoffice/client/delay_impl_cpp.cpp
    ext/aemu_postoffice/client/log_impl_ppsspp.cpp ext/at3_standalone/atrac.cpp
    ext/at3_standalone/atrac3.cpp ext/at3_standalone/atrac3plus.cpp
    ext/at3_standalone/atrac3plusdec.cpp ext/at3_standalone/atrac3plusdsp.cpp
    ext/at3_standalone/fft.cpp ext/at3_standalone/get_bits.cpp ext/at3_standalone/mem.cpp
    ext/at3_standalone/compat.cpp ext/minimp3/minimp3.cpp ext/xbrz/xbrz.cpp ext/snappy/snappy-c.cpp
    ext/snappy/snappy.cpp ext/snappy/snappy-sinksource.cpp ext/snappy/snappy-stubs-internal.cpp
    ext/cityhash/city.cpp
"
PPSPP_EXT_C="
    ext/xxhash.c ext/sfmt19937/SFMT.c ext/libpng17/png.c ext/libpng17/pngerror.c
    ext/libpng17/pngget.c ext/libpng17/pngmem.c ext/libpng17/pngpread.c ext/libpng17/pngread.c
    ext/libpng17/pngrio.c ext/libpng17/pngrtran.c ext/libpng17/pngrutil.c ext/libpng17/pngset.c
    ext/libpng17/pngtrans.c ext/libpng17/pngwio.c ext/libpng17/pngwrite.c ext/libpng17/pngwtran.c
    ext/libpng17/pngwutil.c ext/zstd/lib/common/debug.c ext/zstd/lib/common/entropy_common.c
    ext/zstd/lib/common/error_private.c ext/zstd/lib/common/fse_decompress.c
    ext/zstd/lib/common/pool.c ext/zstd/lib/common/threading.c ext/zstd/lib/common/xxhash.c
    ext/zstd/lib/common/zstd_common.c ext/zstd/lib/compress/fse_compress.c
    ext/zstd/lib/compress/hist.c ext/zstd/lib/compress/huf_compress.c
    ext/zstd/lib/compress/zstd_compress.c ext/zstd/lib/compress/zstd_compress_literals.c
    ext/zstd/lib/compress/zstd_compress_sequences.c ext/zstd/lib/compress/zstd_compress_superblock.c
    ext/zstd/lib/compress/zstd_double_fast.c ext/zstd/lib/compress/zstd_fast.c
    ext/zstd/lib/compress/zstd_lazy.c ext/zstd/lib/compress/zstd_ldm.c
    ext/zstd/lib/compress/zstd_opt.c ext/zstd/lib/compress/zstd_preSplit.c
    ext/zstd/lib/compress/zstdmt_compress.c ext/zstd/lib/decompress/huf_decompress.c
    ext/zstd/lib/decompress/zstd_ddict.c ext/zstd/lib/decompress/zstd_decompress.c
    ext/zstd/lib/decompress/zstd_decompress_block.c ext/libzip/zip_add.c ext/libzip/zip_add_dir.c
    ext/libzip/zip_add_entry.c ext/libzip/zip_algorithm_deflate.c ext/libzip/zip_buffer.c
    ext/libzip/zip_close.c ext/libzip/zip_delete.c ext/libzip/zip_dir_add.c ext/libzip/zip_dirent.c
    ext/libzip/zip_discard.c ext/libzip/zip_entry.c ext/libzip/zip_err_str.c ext/libzip/zip_error.c
    ext/libzip/zip_error_clear.c ext/libzip/zip_error_get.c ext/libzip/zip_error_get_sys_type.c
    ext/libzip/zip_error_strerror.c ext/libzip/zip_error_to_str.c ext/libzip/zip_extra_field.c
    ext/libzip/zip_extra_field_api.c ext/libzip/zip_fclose.c ext/libzip/zip_fdopen.c
    ext/libzip/zip_file_add.c ext/libzip/zip_file_error_clear.c ext/libzip/zip_file_error_get.c
    ext/libzip/zip_file_get_comment.c ext/libzip/zip_file_get_external_attributes.c
    ext/libzip/zip_file_get_offset.c ext/libzip/zip_file_rename.c ext/libzip/zip_file_replace.c
    ext/libzip/zip_file_set_comment.c ext/libzip/zip_file_set_encryption.c
    ext/libzip/zip_file_set_external_attributes.c ext/libzip/zip_file_set_mtime.c
    ext/libzip/zip_file_strerror.c ext/libzip/zip_fopen.c ext/libzip/zip_fopen_encrypted.c
    ext/libzip/zip_fopen_index.c ext/libzip/zip_fopen_index_encrypted.c ext/libzip/zip_fread.c
    ext/libzip/zip_fseek.c ext/libzip/zip_ftell.c ext/libzip/zip_get_archive_comment.c
    ext/libzip/zip_get_archive_flag.c ext/libzip/zip_get_encryption_implementation.c
    ext/libzip/zip_get_file_comment.c ext/libzip/zip_get_name.c ext/libzip/zip_get_num_entries.c
    ext/libzip/zip_get_num_files.c ext/libzip/zip_hash.c ext/libzip/zip_io_util.c
    ext/libzip/zip_libzip_version.c ext/libzip/zip_memdup.c ext/libzip/zip_mkstempm.c
    ext/libzip/zip_name_locate.c ext/libzip/zip_new.c ext/libzip/zip_open.c ext/libzip/zip_pkware.c
    ext/libzip/zip_progress.c ext/libzip/zip_random_unix.c ext/libzip/zip_rename.c
    ext/libzip/zip_replace.c ext/libzip/zip_set_archive_comment.c ext/libzip/zip_set_archive_flag.c
    ext/libzip/zip_set_default_password.c ext/libzip/zip_set_file_comment.c
    ext/libzip/zip_set_file_compression.c ext/libzip/zip_set_name.c
    ext/libzip/zip_source_accept_empty.c ext/libzip/zip_source_begin_write.c
    ext/libzip/zip_source_begin_write_cloning.c ext/libzip/zip_source_buffer.c
    ext/libzip/zip_source_call.c ext/libzip/zip_source_close.c ext/libzip/zip_source_commit_write.c
    ext/libzip/zip_source_compress.c ext/libzip/zip_source_crc.c ext/libzip/zip_source_error.c
    ext/libzip/zip_source_file_common.c ext/libzip/zip_source_file_stdio.c
    ext/libzip/zip_source_file_stdio_named.c ext/libzip/zip_source_free.c
    ext/libzip/zip_source_function.c ext/libzip/zip_source_get_file_attributes.c
    ext/libzip/zip_source_is_deleted.c ext/libzip/zip_source_layered.c ext/libzip/zip_source_open.c
    ext/libzip/zip_source_pkware_decode.c ext/libzip/zip_source_pkware_encode.c
    ext/libzip/zip_source_read.c ext/libzip/zip_source_remove.c
    ext/libzip/zip_source_rollback_write.c ext/libzip/zip_source_seek.c
    ext/libzip/zip_source_seek_write.c ext/libzip/zip_source_stat.c ext/libzip/zip_source_supports.c
    ext/libzip/zip_source_tell.c ext/libzip/zip_source_tell_write.c ext/libzip/zip_source_window.c
    ext/libzip/zip_source_write.c ext/libzip/zip_source_zip.c ext/libzip/zip_source_zip_new.c
    ext/libzip/zip_stat.c ext/libzip/zip_stat_index.c ext/libzip/zip_stat_init.c
    ext/libzip/zip_strerror.c ext/libzip/zip_string.c ext/libzip/zip_unchange.c
    ext/libzip/zip_unchange_all.c ext/libzip/zip_unchange_archive.c ext/libzip/zip_unchange_data.c
    ext/libzip/zip_utf-8.c ext/libchdr/src/libchdr_bitstream.c ext/libchdr/src/libchdr_cdrom.c
    ext/libchdr/src/libchdr_flac.c ext/libchdr/src/libchdr_huffman.c ext/lzma-sdk/7zArcIn.c
    ext/lzma-sdk/7zBuf.c ext/lzma-sdk/7zCrc.c ext/lzma-sdk/7zCrcOpt.c ext/lzma-sdk/7zDec.c
    ext/lzma-sdk/7zFile.c ext/lzma-sdk/7zStream.c ext/lzma-sdk/Bcj2.c ext/lzma-sdk/Bra.c
    ext/lzma-sdk/Bra86.c ext/lzma-sdk/CpuArch.c ext/lzma-sdk/Delta.c ext/lzma-sdk/Lzma2Dec.c
    ext/lzma-sdk/LzmaDec.c ext/aemu_postoffice/client/postoffice.c
    ext/aemu_postoffice/client/postoffice_mem_stdc.c ext/aemu_postoffice/client/sock_impl_linux.c
    ext/libkirk/AES.c
    ext/libkirk/SHA1.c
    ext/libkirk/amctrl.c
    ext/libkirk/bn.c
    ext/libkirk/ec.c
    ext/libkirk/kirk_engine.c
    ext/miniupnp/miniupnpc/src/igd_desc_parse.c
    ext/miniupnp/miniupnpc/src/miniupnpc.c
    ext/miniupnp/miniupnpc/src/minixml.c
    ext/miniupnp/miniupnpc/src/minisoap.c
    ext/miniupnp/miniupnpc/src/minissdpc.c
    ext/miniupnp/miniupnpc/src/miniwget.c
    ext/miniupnp/miniupnpc/src/upnpcommands.c
    ext/miniupnp/miniupnpc/src/upnpdev.c
    ext/miniupnp/miniupnpc/src/upnpreplyparse.c
    ext/miniupnp/miniupnpc/src/upnperrors.c
    ext/miniupnp/miniupnpc/src/connecthostport.c
    ext/miniupnp/miniupnpc/src/portlistingparse.c
    ext/miniupnp/miniupnpc/src/receivedata.c
    ext/miniupnp/miniupnpc/src/addr_is_reserved.c
"
PPSPP_LUA="
    lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldblib.c ldebug.c ldo.c ldump.c lfunc.c
    lgc.c linit.c liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c lopcodes.c loslib.c
    lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c lundump.c lutf8lib.c lvm.c
    lzio.c
"
# ARM-only: libpng's NEON init + intrinsics. Upstream builds these on 64-bit
# ARM (their CMake picks filter_neon_intrinsics.c for 64-bit, the .S only for
# 32-bit); iOS is always 64-bit ARM. x86_64 needs nothing extra — libpng's
# SSE path is opt-in, while its NEON path is default-on for ARM — and these
# files do not compile on x86, so iOS builds gate them by target below while
# the Linux host proof skips them the same way it skips the x86 helpers.
PPSPP_ARM="
    ext/libpng17/arm/arm_init.c ext/libpng17/arm/filter_neon_intrinsics.c
"
# x86_64-only: cpuid helpers. ARM64 (every iOS target) never references
# them (USE_CPU_FEATURES is x86-only upstream), so iOS builds skip these.
PPSPP_X86="
    ext/cpu_features/src/filesystem.c ext/cpu_features/src/stack_line_reader.c
    ext/cpu_features/src/string_view.c ext/cpu_features/src/impl_x86_linux_or_android.c
"
# x86_64-only: zstd's AMD64 Huffman asm. ARM64 uses the C fallback and
# cannot assemble this file, so iOS builds skip it.
PPSPP_X86_ASM="
    ext/zstd/lib/decompress/huf_decompress_amd64.S
"
# psp_core.cpp is OGA glue but needs the PPSSPP tree, so it has its own
# list and lang (ppspp) rather than riding OGA_GLUE's plain cxx.
# Runtime assets the boot path actually opens (strace-proven, Sep 2026):
# compat.ini + langregion.ini (config tables), ppge_atlas (debug-draw font
# the soft GPU keeps mapped), vfpu/*.dat (VFPU sin LUTs the IR interpreter
# consults). Total ~5.5 MB, all free files from PPSSPP's own assets tree.
# NOT bundled and NOT needed: shaders (GL only), lang/*.ini (no system UI),
# debugger/ (stubbed), flash0 (Sony IP — PPSSPP auto-installs it from the
# game disc's own updater partition on first boot; verified with Dissidia).
PPSPP_ASSETS="
    compat.ini langregion.ini ppge_atlas.meta ppge_atlas.zim vfpu/vfpu_sin_lut8192.dat vfpu/vfpu_sin_lut_delta.dat vfpu/vfpu_sin_lut_exceptions.dat vfpu/vfpu_sin_lut_interval_delta.dat
"
# Apple-only ObjC++: the Cocoa text drawer and the Darwin sandbox-bookmark
# helpers. Referenced by kept code only under __APPLE__ (which is why the
# Linux host proof links without them); the iOS build scripts compile them
# with ARC (they use __bridge_transfer). The Linux host proof skips them.
# NOTE: Core/Util/DarwinFileSystemServices.mm is deliberately NOT here. It
# implements the document-picker UI, which needs kUTType* and a
# sharedViewController the app target cannot provide. The only two methods the
# kept code references (reauthorizeBookmarkByPath, stopAccessingPath) are
# stubbed in Core/psp_core.cpp with behavior identical to the real ones for
# this embedding (no picker means no bookmarks and no scoped-access tokens).
PPSPP_MM="
    Common/Render/Text/draw_text_cocoa.mm
"
PPSPP_GLUE="
    psp_core.cpp
"
