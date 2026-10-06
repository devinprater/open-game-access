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
sha1/sha1.c tiny-AES-c/aes.c blip-buf/blip_buf.c
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
# ⛔ NO COMMENTS INSIDE THIS VARIABLE. OGA_GLUE is an unquoted shell string that
# build scripts WORD-SPLIT into a source list, and word-splitting does not respect
# '#' inside a variable: a comment here becomes filenames ("Core/branch",
# "Core/in", "Core/table,", "Core/pokecore.cpp."). That silently broke the device
# archive — 859 "of 908 TUs processed", exit 0, archive never rebuilt, and the app
# link then failed on undefined oga_system_* symbols. Prose belongs OUT here.
#
# oga_core.cpp is the one backend interface every console is reached through
# (Core/oga_core.h): the Game Boy and PSP ops tables plus the extension->backend
# resolver. systems.cpp is the console registry, which was in NO build list at all
# until it was added here.
OGA_GLUE="
poke_platform.cpp pokecore.cpp oga_core.cpp systems.cpp
fe_access.cpp fe_adapter.cpp
gba_adapter.cpp
nes_adapter.cpp
n64_adapter.cpp
gba_core.cpp mgba_version_stub.cpp
mesen_core.cpp
dbz_adapter.cpp
dissidia_adapter.cpp
dq9_adapter.cpp
osk_echo.cpp
adapters.cpp
announce.cpp
"

# ---- Mesen: SEVEN console cores, one tree ----
#
# ⛔ PLANNED, NOT INTEGRATED — ALL SEVEN OF THESE.
# ⛔ REPOSITORY: nesdev-org/MesenCE (the maintained successor), NOT
# SourMesen/Mesen2 (archived 2026-06-04). See the note in bootstrap-deps.sh.
# The counts below are the MesenCE measurement. Each list below is a MEASURED
# build set: every entry compiled clean for aarch64-linux-android26 with the NDK's
# own clang (scripts/mesen-feasibility.sh, re-runnable). They are here so the next
# step — the host glue — has lists to compile against instead of re-deriving them.
#
# ⛔ NONE OF THEM IS IN OGA_GLUE YET, AND THAT IS THE POINT. Adding one there
# would claim that backend exists, and nothing implements any of them: there is no
# Core/mesen_core.cpp. When it lands, these join OGA_GLUE with their own lang case
# (they need Mesen's include roots, exactly as gba_core.cpp needs mGBA's).
#
# ⛔ WHAT A PASS HERE MEANS, AND WHAT IT DOES NOT. It means the core COMPILES for
# this target. There is no ROM boot, no host glue, no frame loop, and no evidence
# at all about performance. mGBA was admitted to this repo the same way and then
# spent months unproven — do not read a list as "the NES works".
#
# Excluded on purpose: Core/UI, Windows/, Sdl/, MacOS/, Linux/, InteropDLL/ and
# SevenZip/ — the frontends and the archive reader. Every console core below links
# against Core/Shared and nothing else from Mesen: no Qt, no SDL, which is the
# property that made mGBA portable here and Dolphin not.
# ---- Mesen's remaining vendored deps (both already in the pinned tree) ----
# ⛔ THESE ARE C, NOT C++, so they need the `cc` language and none of the Mesen pch/cxx flags.
# spng is a single C file; the 7-Zip SDK is what SZReader calls into. Both were invisible until the
# link got far enough to reference them -- four separate link errors, one subsystem at a time.
MESEN_C="
Utilities/spng.c
"
# ---- Mesen's 7-Zip SDK, MINUS the files mGBA's copy already provides ----
# ⛔ THE SHARED-SDK PROBLEM, SOLVED BY DIFFERENCE RATHER THAN BY PREFERENCE. mgba/src/third-party/lzma
# is in the link already (MGBA_LZMA) and is the SAME 7-Zip SDK as Mesen's SevenZip/, but a SUBSET:
# 8 files against Mesen's 20. So:
#
#   * link ALL of Mesen's  -> 8 duplicate definitions (LookInStream_LookRead, ...)
#   * link NONE of Mesen's -> SzAlloc/SzFree/MemBufferInit undefined (mGBA's copy has no 7zAlloc.c)
#   * link the DIFFERENCE  -> one SDK, complete. That is this list.
#
# Computed from the two lists, not hardcoded, so it stays correct if mGBA's set changes. The files
# below are exactly those with no basename counterpart in MGBA_LZMA:
#   7zAlloc.c, 7zArcIn.c, 7zCrc.c, 7zDec.c, 7zMemBuffer.c, 7zStream.c, Bra.c, BraIA64.c, Delta.c, Ppmd7.c, Ppmd7Dec.c, Precomp.c
# ⛔ MINIMAL AND EMPIRICAL, NOT A DIFF -- and the prose lives HERE, not inside the variable.
# mGBA's vendored lzma (MGBA_LZMA) is the same 7-Zip SDK and is already in the link, so most of
# Mesen's SevenZip/ would be duplicate definitions. Two attempts got this wrong in opposite
# directions: all of Mesen's -> "multiple definition of LookInStream_LookRead"; none of Mesen's ->
# SzAlloc/SzFree/SzTempFree/MemBufferInit undefined, because mGBA's copy is a subset without
# 7zAlloc.c. So start from the files that DEFINE the symbols the linker named, and let the linker
# ask for anything else.
# ⛔ 7zStream.c IS THE ONLY SOURCE of LookToRead_CreateVTable / LookToRead_Init, which SZReader
# needs, and they cannot be lifted out (LookToRead_CreateVTable points at file-static helpers in the
# same file). It overlaps mGBA's copy in several symbols, and its object is post-processed by
# scripts/mesen-dedupe-7z.sh to keep the shared ones local. Do NOT remove this entry to "fix" the
# duplicates -- that deletes the two symbols SZReader actually needs.
MESEN_SEVENZIP_C="
SevenZip/7zAlloc.c
SevenZip/7zMemBuffer.c
SevenZip/7zStream.c
"
MESEN_SEVENZIP_CPP=""


# ---- THE WHOLE Mesen Core, from Mesen's own Core.vcxproj ----
# ⛔ NOT A SUBSET, AND THE SUBSET WAS THE BUG. Core/Shared/Emulator.cpp constructs EVERY console
# Mesen supports (its factory has no per-console guard) and owns a Debugger unconditionally; there
# is no #ifdef in the tree to trim either. So a NES-only list cannot LINK, however cleanly it
# compiles -- and scripts/mesen-feasibility.sh ran -fsyntax-only, which is why "84/84 TUs" was true
# and said nothing about linking. Mesen's own Core.vcxproj is the authority: this is that list.
#
# Keep the measured subsets above; they document what the admission test actually measured.
MESEN_CORE_ALL="
Core/Debugger/Base6502Assembler.cpp
Core/Debugger/BaseEventManager.cpp
Core/Debugger/Breakpoint.cpp
Core/Debugger/BreakpointManager.cpp
Core/Debugger/CallstackManager.cpp
Core/Debugger/CdlManager.cpp
Core/Debugger/CodeDataLogger.cpp
Core/Debugger/Debugger.cpp
Core/Debugger/Disassembler.cpp
Core/Debugger/DisassemblyInfo.cpp
Core/Debugger/DisassemblySearch.cpp
Core/Debugger/ExpressionEvaluator.Cx4.cpp
Core/Debugger/ExpressionEvaluator.Gameboy.cpp
Core/Debugger/ExpressionEvaluator.Gba.cpp
Core/Debugger/ExpressionEvaluator.Gsu.cpp
Core/Debugger/ExpressionEvaluator.NecDsp.cpp
Core/Debugger/ExpressionEvaluator.Nes.cpp
Core/Debugger/ExpressionEvaluator.Pce.cpp
Core/Debugger/ExpressionEvaluator.Sms.cpp
Core/Debugger/ExpressionEvaluator.Snes.cpp
Core/Debugger/ExpressionEvaluator.Spc.cpp
Core/Debugger/ExpressionEvaluator.St018.cpp
Core/Debugger/ExpressionEvaluator.Ws.cpp
Core/Debugger/ExpressionEvaluator.cpp
Core/Debugger/LabelManager.cpp
Core/Debugger/LuaApi.cpp
Core/Debugger/LuaCallHelper.cpp
Core/Debugger/MemoryAccessCounter.cpp
Core/Debugger/MemoryDumper.cpp
Core/Debugger/PpuTools.cpp
Core/Debugger/Profiler.cpp
Core/Debugger/ScriptHost.cpp
Core/Debugger/ScriptManager.cpp
Core/Debugger/ScriptingContext.cpp
Core/Debugger/StepBackManager.cpp
Core/GBA/APU/GbaApu.cpp
Core/GBA/APU/GbaNoiseChannel.cpp
Core/GBA/APU/GbaSquareChannel.cpp
Core/GBA/APU/GbaWaveChannel.cpp
Core/GBA/Cart/GbaCart.cpp
Core/GBA/Cart/GbaGpio.cpp
Core/GBA/Cart/GbaRtc.cpp
Core/GBA/Debugger/DummyGbaCpu.cpp
Core/GBA/Debugger/GbaDebugger.cpp
Core/GBA/Debugger/GbaDisUtils.cpp
Core/GBA/Debugger/GbaEventManager.cpp
Core/GBA/Debugger/GbaPpuTools.cpp
Core/GBA/Debugger/GbaTraceLogger.cpp
Core/GBA/GbaConsole.cpp
Core/GBA/GbaControlManager.cpp
Core/GBA/GbaCpu.Arm.cpp
Core/GBA/GbaCpu.Thumb.cpp
Core/GBA/GbaCpu.cpp
Core/GBA/GbaDefaultVideoFilter.cpp
Core/GBA/GbaDmaController.cpp
Core/GBA/GbaMemoryManager.cpp
Core/GBA/GbaPpu.cpp
Core/GBA/GbaTimer.cpp
Core/Gameboy/APU/GbApu.cpp
Core/Gameboy/APU/GbNoiseChannel.cpp
Core/Gameboy/APU/GbSquareChannel.cpp
Core/Gameboy/APU/GbWaveChannel.cpp
Core/Gameboy/Debugger/DummyGbCpu.cpp
Core/Gameboy/Debugger/GameboyDisUtils.cpp
Core/Gameboy/Debugger/GbAssembler.cpp
Core/Gameboy/Debugger/GbDebugger.cpp
Core/Gameboy/Debugger/GbEventManager.cpp
Core/Gameboy/Debugger/GbPpuTools.cpp
Core/Gameboy/Debugger/GbTraceLogger.cpp
Core/Gameboy/Gameboy.cpp
Core/Gameboy/GbControlManager.cpp
Core/Gameboy/GbCpu.cpp
Core/Gameboy/GbDefaultVideoFilter.cpp
Core/Gameboy/GbDmaController.cpp
Core/Gameboy/GbMemoryManager.cpp
Core/Gameboy/GbPpu.cpp
Core/Gameboy/GbTimer.cpp
Core/NES/APU/DeltaModulationChannel.cpp
Core/NES/APU/NesApu.cpp
Core/NES/BaseMapper.cpp
Core/NES/BaseNesPpu.cpp
Core/NES/BisqwitNtscFilter.cpp
Core/NES/Debugger/DummyNesCpu.cpp
Core/NES/Debugger/NesAssembler.cpp
Core/NES/Debugger/NesDebugger.cpp
Core/NES/Debugger/NesDisUtils.cpp
Core/NES/Debugger/NesEventManager.cpp
Core/NES/Debugger/NesPpuTools.cpp
Core/NES/Debugger/NesTraceLogger.cpp
Core/NES/Epsm.cpp
Core/NES/GameDatabase.cpp
Core/NES/HdPacks/HdAudioDevice.cpp
Core/NES/HdPacks/HdNesPack.cpp
Core/NES/HdPacks/HdNesPpu.cpp
Core/NES/HdPacks/HdPackBuilder.cpp
Core/NES/HdPacks/HdPackLoader.cpp
Core/NES/HdPacks/HdVideoFilter.cpp
Core/NES/HdPacks/OggMixer.cpp
Core/NES/HdPacks/OggReader.cpp
Core/NES/Loaders/FdsLoader.cpp
Core/NES/Loaders/NsfLoader.cpp
Core/NES/Loaders/RomLoader.cpp
Core/NES/Loaders/StudyBoxLoader.cpp
Core/NES/Loaders/UnifLoader.cpp
Core/NES/Loaders/iNesLoader.cpp
Core/NES/MapperFactory.cpp
Core/NES/Mappers/FDS/Fds.cpp
Core/NES/Mappers/FDS/FdsAudio.cpp
Core/NES/Mappers/FDS/FdsInputButtons.cpp
Core/NES/Mappers/Homebrew/Rainbow.cpp
Core/NES/Mappers/NSF/NsfMapper.cpp
Core/NES/Mappers/VsSystem/VsControlManager.cpp
Core/NES/NesConsole.cpp
Core/NES/NesControlManager.cpp
Core/NES/NesCpu.cpp
Core/NES/NesDefaultVideoFilter.cpp
Core/NES/NesHeader.cpp
Core/NES/NesMemoryManager.cpp
Core/NES/NesNtscFilter.cpp
Core/NES/NesPpu.cpp
Core/NES/NesSoundMixer.cpp
Core/Netplay/GameClient.cpp
Core/Netplay/GameClientConnection.cpp
Core/Netplay/GameConnection.cpp
Core/Netplay/GameServer.cpp
Core/Netplay/GameServerConnection.cpp
Core/PCE/CdRom/PceAdpcm.cpp
Core/PCE/CdRom/PceArcadeCard.cpp
Core/PCE/CdRom/PceAudioFader.cpp
Core/PCE/CdRom/PceCdAudioPlayer.cpp
Core/PCE/CdRom/PceCdRom.cpp
Core/PCE/CdRom/PceScsiBus.cpp
Core/PCE/Debugger/DummyPceCpu.cpp
Core/PCE/Debugger/PceAssembler.cpp
Core/PCE/Debugger/PceDebugger.cpp
Core/PCE/Debugger/PceDisUtils.cpp
Core/PCE/Debugger/PceEventManager.cpp
Core/PCE/Debugger/PceTraceLogger.cpp
Core/PCE/Debugger/PceVdcTools.cpp
Core/PCE/Input/PceTurboTap.cpp
Core/PCE/PceConsole.cpp
Core/PCE/PceControlManager.cpp
Core/PCE/PceCpu.Instructions.cpp
Core/PCE/PceCpu.cpp
Core/PCE/PceMemoryManager.cpp
Core/PCE/PceNtscFilter.cpp
Core/PCE/PcePsg.cpp
Core/PCE/PcePsgChannel.cpp
Core/PCE/PceSf2RomMapper.cpp
Core/PCE/PceTimer.cpp
Core/PCE/PceVce.cpp
Core/PCE/PceVdc.cpp
Core/PCE/PceVpc.cpp
Core/SMS/Carts/SmsCart.cpp
Core/SMS/Debugger/DummySmsCpu.cpp
Core/SMS/Debugger/SmsAssembler.cpp
Core/SMS/Debugger/SmsDebugger.cpp
Core/SMS/Debugger/SmsDisUtils.cpp
Core/SMS/Debugger/SmsEventManager.cpp
Core/SMS/Debugger/SmsTraceLogger.cpp
Core/SMS/Debugger/SmsVdpTools.cpp
Core/SMS/SmsBiosMapper.cpp
Core/SMS/SmsConsole.cpp
Core/SMS/SmsControlManager.cpp
Core/SMS/SmsCpu.cpp
Core/SMS/SmsFmAudio.cpp
Core/SMS/SmsMemoryManager.cpp
Core/SMS/SmsNtscFilter.cpp
Core/SMS/SmsPsg.cpp
Core/SMS/SmsVdp.cpp
Core/SNES/AluMulDiv.cpp
Core/SNES/BaseCartridge.cpp
Core/SNES/Coprocessors/BSX/BsxCart.cpp
Core/SNES/Coprocessors/BSX/BsxMemoryPack.cpp
Core/SNES/Coprocessors/BSX/BsxSatellaview.cpp
Core/SNES/Coprocessors/BSX/BsxStream.cpp
Core/SNES/Coprocessors/CX4/Cx4.Instructions.cpp
Core/SNES/Coprocessors/CX4/Cx4.cpp
Core/SNES/Coprocessors/DSP/NecDsp.cpp
Core/SNES/Coprocessors/GSU/Gsu.Instructions.cpp
Core/SNES/Coprocessors/GSU/Gsu.cpp
Core/SNES/Coprocessors/MSU1/Msu1.cpp
Core/SNES/Coprocessors/OBC1/Obc1.cpp
Core/SNES/Coprocessors/SA1/Sa1.cpp
Core/SNES/Coprocessors/SA1/Sa1Cpu.cpp
Core/SNES/Coprocessors/SDD1/Sdd1.cpp
Core/SNES/Coprocessors/SDD1/Sdd1Decomp.cpp
Core/SNES/Coprocessors/SDD1/Sdd1Mmc.cpp
Core/SNES/Coprocessors/SGB/SuperGameboy.cpp
Core/SNES/Coprocessors/SPC7110/Rtc4513.cpp
Core/SNES/Coprocessors/SPC7110/Spc7110.cpp
Core/SNES/Coprocessors/SPC7110/Spc7110Decomp.cpp
Core/SNES/Coprocessors/ST018/ArmV3Cpu.cpp
Core/SNES/Coprocessors/ST018/St018.cpp
Core/SNES/DSP/Dsp.cpp
Core/SNES/DSP/DspVoice.cpp
Core/SNES/Debugger/Cx4Debugger.cpp
Core/SNES/Debugger/Cx4DisUtils.cpp
Core/SNES/Debugger/DummyArmV3Cpu.cpp
Core/SNES/Debugger/DummySnesCpu.cpp
Core/SNES/Debugger/DummySpc.cpp
Core/SNES/Debugger/GsuDebugger.cpp
Core/SNES/Debugger/GsuDisUtils.cpp
Core/SNES/Debugger/NecDspDebugger.cpp
Core/SNES/Debugger/NecDspDisUtils.cpp
Core/SNES/Debugger/SnesAssembler.cpp
Core/SNES/Debugger/SnesDebugger.cpp
Core/SNES/Debugger/SnesDisUtils.cpp
Core/SNES/Debugger/SnesEventManager.cpp
Core/SNES/Debugger/SnesPpuTools.cpp
Core/SNES/Debugger/SpcDebugger.cpp
Core/SNES/Debugger/SpcDisUtils.cpp
Core/SNES/Debugger/St018Debugger.cpp
Core/SNES/Debugger/St018DisUtils.cpp
Core/SNES/Debugger/TraceLogger/Cx4TraceLogger.cpp
Core/SNES/Debugger/TraceLogger/GsuTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/NecDspTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/SnesCpuTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/SpcTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/St018TraceLogger.cpp
Core/SNES/Input/Multitap.cpp
Core/SNES/Input/SnesBlueRetroController.cpp
Core/SNES/Input/SnesController.cpp
Core/SNES/Input/SnesNttDataKeypad.cpp
Core/SNES/Input/SnesRumbleController.cpp
Core/SNES/InternalRegisters.cpp
Core/SNES/MemoryMappings.cpp
Core/SNES/RegisterHandlerB.cpp
Core/SNES/SnesConsole.cpp
Core/SNES/SnesControlManager.cpp
Core/SNES/SnesCpu.cpp
Core/SNES/SnesDefaultVideoFilter.cpp
Core/SNES/SnesDmaController.cpp
Core/SNES/SnesMemoryManager.cpp
Core/SNES/SnesNtscFilter.cpp
Core/SNES/SnesPpu.cpp
Core/SNES/Spc.Instructions.cpp
Core/SNES/Spc.cpp
Core/Shared/Audio/AudioPlayerHud.cpp
Core/Shared/Audio/BaseSoundManager.cpp
Core/Shared/Audio/PcmReader.cpp
Core/Shared/Audio/SoundMixer.cpp
Core/Shared/Audio/SoundResampler.cpp
Core/Shared/Audio/WaveRecorder.cpp
Core/Shared/BaseControlDevice.cpp
Core/Shared/BaseControlManager.cpp
Core/Shared/BatteryManager.cpp
Core/Shared/CdReader.cpp
Core/Shared/CheatManager.cpp
Core/Shared/DebuggerRequest.cpp
Core/Shared/EmuSettings.cpp
Core/Shared/Emulator.cpp
Core/Shared/EmulatorLock.cpp
Core/Shared/HistoryViewer.cpp
Core/Shared/InputHud.cpp
Core/Shared/KeyManager.cpp
Core/Shared/MessageManager.cpp
Core/Shared/Movies/BizHawkMovie.cpp
Core/Shared/Movies/MesenMovie.cpp
Core/Shared/Movies/MovieManager.cpp
Core/Shared/Movies/MovieRecorder.cpp
Core/Shared/NotificationManager.cpp
Core/Shared/RecordedRomTest.cpp
Core/Shared/RewindData.cpp
Core/Shared/RewindManager.cpp
Core/Shared/SaveStateManager.cpp
Core/Shared/ShortcutKeyHandler.cpp
Core/Shared/Utilities/S3511ARtc.cpp
Core/Shared/Utilities/emu2413.cpp
Core/Shared/Video/BaseVideoFilter.cpp
Core/Shared/Video/DebugHud.cpp
Core/Shared/Video/DebugStats.cpp
Core/Shared/Video/DrawStringCommand.cpp
Core/Shared/Video/RotateFilter.cpp
Core/Shared/Video/ScaleFilter.cpp
Core/Shared/Video/SoftwareRenderer.cpp
Core/Shared/Video/SystemHud.cpp
Core/Shared/Video/VideoDecoder.cpp
Core/Shared/Video/VideoRenderer.cpp
Core/WS/APU/WsApu.cpp
Core/WS/Carts/WsCart.cpp
Core/WS/Carts/WsCartBandai2001.cpp
Core/WS/Carts/WsCartBandai2003.cpp
Core/WS/Carts/WsCartWonderWitch.cpp
Core/WS/Carts/WsRtc.cpp
Core/WS/Debugger/DummyWsCpu.cpp
Core/WS/Debugger/WsDebugger.cpp
Core/WS/Debugger/WsDisUtils.cpp
Core/WS/Debugger/WsEventManager.cpp
Core/WS/Debugger/WsPpuTools.cpp
Core/WS/Debugger/WsTraceLogger.cpp
Core/WS/WsConsole.cpp
Core/WS/WsControlManager.cpp
Core/WS/WsCpu.cpp
Core/WS/WsCpuPrefetch.cpp
Core/WS/WsDefaultVideoFilter.cpp
Core/WS/WsDmaController.cpp
Core/WS/WsEeprom.cpp
Core/WS/WsMemoryManager.cpp
Core/WS/WsPpu.cpp
Core/WS/WsSerial.cpp
Core/WS/WsTimer.cpp
"

MESEN_NES="
Core/NES/APU/DeltaModulationChannel.cpp
Core/NES/APU/NesApu.cpp
Core/NES/BaseMapper.cpp
Core/NES/BaseNesPpu.cpp
Core/NES/BisqwitNtscFilter.cpp
Core/NES/Debugger/DummyNesCpu.cpp
Core/NES/Debugger/NesAssembler.cpp
Core/NES/Debugger/NesDebugger.cpp
Core/NES/Debugger/NesDisUtils.cpp
Core/NES/Debugger/NesEventManager.cpp
Core/NES/Debugger/NesPpuTools.cpp
Core/NES/Debugger/NesTraceLogger.cpp
Core/NES/Epsm.cpp
Core/NES/GameDatabase.cpp
Core/NES/HdPacks/HdAudioDevice.cpp
Core/NES/HdPacks/HdNesPack.cpp
Core/NES/HdPacks/HdNesPpu.cpp
Core/NES/HdPacks/HdPackBuilder.cpp
Core/NES/HdPacks/HdPackLoader.cpp
Core/NES/HdPacks/HdVideoFilter.cpp
Core/NES/HdPacks/OggMixer.cpp
Core/NES/HdPacks/OggReader.cpp
Core/NES/Loaders/FdsLoader.cpp
Core/NES/Loaders/NsfLoader.cpp
Core/NES/Loaders/RomLoader.cpp
Core/NES/Loaders/StudyBoxLoader.cpp
Core/NES/Loaders/UnifLoader.cpp
Core/NES/Loaders/iNesLoader.cpp
Core/NES/MapperFactory.cpp
Core/NES/Mappers/FDS/Fds.cpp
Core/NES/Mappers/FDS/FdsAudio.cpp
Core/NES/Mappers/FDS/FdsInputButtons.cpp
Core/NES/Mappers/Homebrew/Rainbow.cpp
Core/NES/Mappers/NSF/NsfMapper.cpp
Core/NES/Mappers/VsSystem/VsControlManager.cpp
Core/NES/NesConsole.cpp
Core/NES/NesControlManager.cpp
Core/NES/NesCpu.cpp
Core/NES/NesDefaultVideoFilter.cpp
Core/NES/NesHeader.cpp
Core/NES/NesMemoryManager.cpp
Core/NES/NesNtscFilter.cpp
Core/NES/NesPpu.cpp
Core/NES/NesSoundMixer.cpp
"
# ⛔ GENERATED BY scripts/mesen-feasibility.sh, NOT HAND-WRITTEN. That script
# compiles every one of these for aarch64-linux-android26 and fails if any of them
# does not, so the lists cannot drift from the tree they were measured against:
# move the Mesen pin, re-run the script, and paste its output here. The script also
# PRINTS these lists in exactly this form, so refreshing them is copy-paste.


# ⛔ Core/Shared IS COMPILED ONCE AND SHARED BY EVERY CONSOLE BELOW. Mesen is
# one tree with seven console cores; folding Shared into each console's list
# would compile the same TUs seven times and make every count a lie.
# ---- Mesen Utilities: the third set no list carried ----
# ⛔ THESE ARE NOT OPTIONAL AND WERE NEVER IN A LIST. Core/Shared calls into Utilities/ for locks,
# serialization, UTF8 and VirtualFile. scripts/mesen-feasibility.sh compiled every Core TU with
# -fsyntax-only and linked nothing, so a MISSING TRANSLATION UNIT was invisible to the admission
# test by construction -- the "84/84" number was true and said nothing about linking. The first NES
# link failed on SimpleLock::SimpleLock(), which is what surfaced it.
#
# pch.cpp is deliberately absent: it IS the precompiled header Mesen force-includes, not a TU.
# ⛔ EVERY SUBDIRECTORY, FROM Mesen's OWN Utilities.vcxproj. The first version of this list was
# taken from the TOP LEVEL of Utilities/ only, so Audio/, HQX/, KreedSaiEagle/, NTSC/, Patches/,
# Scale2x/, Video/ and xBRZ/ were all absent -- and each one announced itself as a separate link
# error (ReverbFilter, hqxInit, IpsPatcher, AviRecorder, ...) rather than as "the list is wrong".
# Read the project file, not the directory listing; the project file is what Mesen builds.
MESEN_UTILS="
Utilities/ArchiveReader.cpp
Utilities/Audio/BiquadCascadeFilter.cpp
Utilities/Audio/BiquadFilter.cpp
Utilities/Audio/CrossFeedFilter.cpp
Utilities/Audio/Equalizer.cpp
Utilities/Audio/HermiteResampler.cpp
Utilities/Audio/ReverbFilter.cpp
Utilities/Audio/StereoCombFilter.cpp
Utilities/Audio/StereoDelayFilter.cpp
Utilities/Audio/StereoPanningFilter.cpp
Utilities/Audio/WavReader.cpp
Utilities/Audio/blip_buf.cpp
Utilities/Audio/stb_vorbis.cpp
Utilities/Audio/ymfm/ymfm_adpcm.cpp
Utilities/Audio/ymfm/ymfm_misc.cpp
Utilities/Audio/ymfm/ymfm_opn.cpp
Utilities/Audio/ymfm/ymfm_ssg.cpp
Utilities/AutoResetEvent.cpp
Utilities/CRC32.cpp
Utilities/FolderUtilities.cpp
Utilities/HQX/hq2x.cpp
Utilities/HQX/hq3x.cpp
Utilities/HQX/hq4x.cpp
Utilities/HQX/init.cpp
Utilities/HexUtilities.cpp
Utilities/KreedSaiEagle/2xSai.cpp
Utilities/KreedSaiEagle/Super2xSai.cpp
Utilities/KreedSaiEagle/SuperEagle.cpp
Utilities/NTSC/nes_ntsc.cpp
Utilities/NTSC/sms_ntsc.cpp
Utilities/NTSC/snes_ntsc.cpp
Utilities/PNGHelper.cpp
Utilities/Patches/BpsPatcher.cpp
Utilities/Patches/IpsPatcher.cpp
Utilities/Patches/UpsPatcher.cpp
Utilities/PlatformUtilities.cpp
Utilities/SZReader.cpp
Utilities/Scale2x/scale2x.cpp
Utilities/Scale2x/scale3x.cpp
Utilities/Scale2x/scalebit.cpp
Utilities/Serializer.cpp
Utilities/SimpleLock.cpp
Utilities/Socket.cpp
Utilities/Timer.cpp
Utilities/UPnPPortMapper.cpp
Utilities/UTF8Util.cpp
Utilities/Video/AviRecorder.cpp
Utilities/Video/AviWriter.cpp
Utilities/Video/CamstudioCodec.cpp
Utilities/Video/GifRecorder.cpp
Utilities/Video/ZmbvCodec.cpp
Utilities/VirtualFile.cpp
Utilities/ZipReader.cpp
Utilities/ZipWriter.cpp
Utilities/miniz.cpp
Utilities/sha1.cpp
Utilities/xBRZ/xbrz.cpp
"


MESEN_SHARED="
Core/Shared/Audio/AudioPlayerHud.cpp
Core/Shared/Audio/BaseSoundManager.cpp
Core/Shared/Audio/PcmReader.cpp
Core/Shared/Audio/SoundMixer.cpp
Core/Shared/Audio/SoundResampler.cpp
Core/Shared/Audio/WaveRecorder.cpp
Core/Shared/BaseControlDevice.cpp
Core/Shared/BaseControlManager.cpp
Core/Shared/BatteryManager.cpp
Core/Shared/CdReader.cpp
Core/Shared/CheatManager.cpp
Core/Shared/DebuggerRequest.cpp
Core/Shared/EmuSettings.cpp
Core/Shared/Emulator.cpp
Core/Shared/EmulatorLock.cpp
Core/Shared/HistoryViewer.cpp
Core/Shared/InputHud.cpp
Core/Shared/KeyManager.cpp
Core/Shared/MessageManager.cpp
Core/Shared/Movies/BizHawkMovie.cpp
Core/Shared/Movies/MesenMovie.cpp
Core/Shared/Movies/MovieManager.cpp
Core/Shared/Movies/MovieRecorder.cpp
Core/Shared/NotificationManager.cpp
Core/Shared/RecordedRomTest.cpp
Core/Shared/RewindData.cpp
Core/Shared/RewindManager.cpp
Core/Shared/SaveStateManager.cpp
Core/Shared/ShortcutKeyHandler.cpp
Core/Shared/Utilities/S3511ARtc.cpp
Core/Shared/Utilities/emu2413.cpp
Core/Shared/Video/BaseVideoFilter.cpp
Core/Shared/Video/DebugHud.cpp
Core/Shared/Video/DebugStats.cpp
Core/Shared/Video/DrawStringCommand.cpp
Core/Shared/Video/RotateFilter.cpp
Core/Shared/Video/ScaleFilter.cpp
Core/Shared/Video/SoftwareRenderer.cpp
Core/Shared/Video/SystemHud.cpp
Core/Shared/Video/VideoDecoder.cpp
Core/Shared/Video/VideoRenderer.cpp
"
# Super Nintendo
MESEN_SNES="
Core/SNES/AluMulDiv.cpp
Core/SNES/BaseCartridge.cpp
Core/SNES/Coprocessors/BSX/BsxCart.cpp
Core/SNES/Coprocessors/BSX/BsxMemoryPack.cpp
Core/SNES/Coprocessors/BSX/BsxSatellaview.cpp
Core/SNES/Coprocessors/BSX/BsxStream.cpp
Core/SNES/Coprocessors/CX4/Cx4.Instructions.cpp
Core/SNES/Coprocessors/CX4/Cx4.cpp
Core/SNES/Coprocessors/DSP/NecDsp.cpp
Core/SNES/Coprocessors/GSU/Gsu.Instructions.cpp
Core/SNES/Coprocessors/GSU/Gsu.cpp
Core/SNES/Coprocessors/MSU1/Msu1.cpp
Core/SNES/Coprocessors/OBC1/Obc1.cpp
Core/SNES/Coprocessors/SA1/Sa1.cpp
Core/SNES/Coprocessors/SA1/Sa1Cpu.cpp
Core/SNES/Coprocessors/SDD1/Sdd1.cpp
Core/SNES/Coprocessors/SDD1/Sdd1Decomp.cpp
Core/SNES/Coprocessors/SDD1/Sdd1Mmc.cpp
Core/SNES/Coprocessors/SGB/SuperGameboy.cpp
Core/SNES/Coprocessors/SPC7110/Rtc4513.cpp
Core/SNES/Coprocessors/SPC7110/Spc7110.cpp
Core/SNES/Coprocessors/SPC7110/Spc7110Decomp.cpp
Core/SNES/Coprocessors/ST018/ArmV3Cpu.cpp
Core/SNES/Coprocessors/ST018/St018.cpp
Core/SNES/DSP/Dsp.cpp
Core/SNES/DSP/DspVoice.cpp
Core/SNES/Debugger/Cx4Debugger.cpp
Core/SNES/Debugger/Cx4DisUtils.cpp
Core/SNES/Debugger/DummyArmV3Cpu.cpp
Core/SNES/Debugger/DummySnesCpu.cpp
Core/SNES/Debugger/DummySpc.cpp
Core/SNES/Debugger/GsuDebugger.cpp
Core/SNES/Debugger/GsuDisUtils.cpp
Core/SNES/Debugger/NecDspDebugger.cpp
Core/SNES/Debugger/NecDspDisUtils.cpp
Core/SNES/Debugger/SnesAssembler.cpp
Core/SNES/Debugger/SnesDebugger.cpp
Core/SNES/Debugger/SnesDisUtils.cpp
Core/SNES/Debugger/SnesEventManager.cpp
Core/SNES/Debugger/SnesPpuTools.cpp
Core/SNES/Debugger/SpcDebugger.cpp
Core/SNES/Debugger/SpcDisUtils.cpp
Core/SNES/Debugger/St018Debugger.cpp
Core/SNES/Debugger/St018DisUtils.cpp
Core/SNES/Debugger/TraceLogger/Cx4TraceLogger.cpp
Core/SNES/Debugger/TraceLogger/GsuTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/NecDspTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/SnesCpuTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/SpcTraceLogger.cpp
Core/SNES/Debugger/TraceLogger/St018TraceLogger.cpp
Core/SNES/Input/Multitap.cpp
Core/SNES/Input/SnesBlueRetroController.cpp
Core/SNES/Input/SnesController.cpp
Core/SNES/Input/SnesNttDataKeypad.cpp
Core/SNES/Input/SnesRumbleController.cpp
Core/SNES/InternalRegisters.cpp
Core/SNES/MemoryMappings.cpp
Core/SNES/RegisterHandlerB.cpp
Core/SNES/SnesConsole.cpp
Core/SNES/SnesControlManager.cpp
Core/SNES/SnesCpu.cpp
Core/SNES/SnesDefaultVideoFilter.cpp
Core/SNES/SnesDmaController.cpp
Core/SNES/SnesMemoryManager.cpp
Core/SNES/SnesNtscFilter.cpp
Core/SNES/SnesPpu.cpp
Core/SNES/Spc.Instructions.cpp
Core/SNES/Spc.cpp
"
# Game Boy / Game Boy Color -- a SECOND Game Boy path; mGBA already boots GB,
# so adopting this one is a choice, not a gap
MESEN_GAMEBOY="
Core/Gameboy/APU/GbApu.cpp
Core/Gameboy/APU/GbNoiseChannel.cpp
Core/Gameboy/APU/GbSquareChannel.cpp
Core/Gameboy/APU/GbWaveChannel.cpp
Core/Gameboy/Debugger/DummyGbCpu.cpp
Core/Gameboy/Debugger/GameboyDisUtils.cpp
Core/Gameboy/Debugger/GbAssembler.cpp
Core/Gameboy/Debugger/GbDebugger.cpp
Core/Gameboy/Debugger/GbEventManager.cpp
Core/Gameboy/Debugger/GbPpuTools.cpp
Core/Gameboy/Debugger/GbTraceLogger.cpp
Core/Gameboy/Gameboy.cpp
Core/Gameboy/GbControlManager.cpp
Core/Gameboy/GbCpu.cpp
Core/Gameboy/GbDefaultVideoFilter.cpp
Core/Gameboy/GbDmaController.cpp
Core/Gameboy/GbMemoryManager.cpp
Core/Gameboy/GbPpu.cpp
Core/Gameboy/GbTimer.cpp"
# PC Engine / TurboGrafx-16 / SuperGrafx
MESEN_PCE="
Core/PCE/CdRom/PceAdpcm.cpp
Core/PCE/CdRom/PceArcadeCard.cpp
Core/PCE/CdRom/PceAudioFader.cpp
Core/PCE/CdRom/PceCdAudioPlayer.cpp
Core/PCE/CdRom/PceCdRom.cpp
Core/PCE/CdRom/PceScsiBus.cpp
Core/PCE/Debugger/DummyPceCpu.cpp
Core/PCE/Debugger/PceAssembler.cpp
Core/PCE/Debugger/PceDebugger.cpp
Core/PCE/Debugger/PceDisUtils.cpp
Core/PCE/Debugger/PceEventManager.cpp
Core/PCE/Debugger/PceTraceLogger.cpp
Core/PCE/Debugger/PceVdcTools.cpp
Core/PCE/Input/PceTurboTap.cpp
Core/PCE/PceConsole.cpp
Core/PCE/PceControlManager.cpp
Core/PCE/PceCpu.Instructions.cpp
Core/PCE/PceCpu.cpp
Core/PCE/PceMemoryManager.cpp
Core/PCE/PceNtscFilter.cpp
Core/PCE/PcePsg.cpp
Core/PCE/PcePsgChannel.cpp
Core/PCE/PceSf2RomMapper.cpp
Core/PCE/PceTimer.cpp
Core/PCE/PceVce.cpp
Core/PCE/PceVdc.cpp
Core/PCE/PceVpc.cpp"
# Sega Master System / Game Gear / SG-1000
MESEN_SMS="
Core/SMS/Carts/SmsCart.cpp
Core/SMS/Debugger/DummySmsCpu.cpp
Core/SMS/Debugger/SmsAssembler.cpp
Core/SMS/Debugger/SmsDebugger.cpp
Core/SMS/Debugger/SmsDisUtils.cpp
Core/SMS/Debugger/SmsEventManager.cpp
Core/SMS/Debugger/SmsTraceLogger.cpp
Core/SMS/Debugger/SmsVdpTools.cpp
Core/SMS/SmsBiosMapper.cpp
Core/SMS/SmsConsole.cpp
Core/SMS/SmsControlManager.cpp
Core/SMS/SmsCpu.cpp
Core/SMS/SmsFmAudio.cpp
Core/SMS/SmsMemoryManager.cpp
Core/SMS/SmsNtscFilter.cpp
Core/SMS/SmsPsg.cpp
Core/SMS/SmsVdp.cpp"
# WonderSwan / WonderSwan Color
MESEN_WS="
Core/WS/APU/WsApu.cpp
Core/WS/Carts/WsCart.cpp
Core/WS/Carts/WsCartBandai2001.cpp
Core/WS/Carts/WsCartBandai2003.cpp
Core/WS/Carts/WsCartWonderWitch.cpp
Core/WS/Carts/WsRtc.cpp
Core/WS/Debugger/DummyWsCpu.cpp
Core/WS/Debugger/WsDebugger.cpp
Core/WS/Debugger/WsDisUtils.cpp
Core/WS/Debugger/WsEventManager.cpp
Core/WS/Debugger/WsPpuTools.cpp
Core/WS/Debugger/WsTraceLogger.cpp
Core/WS/WsConsole.cpp
Core/WS/WsControlManager.cpp
Core/WS/WsCpu.cpp
Core/WS/WsCpuPrefetch.cpp
Core/WS/WsDefaultVideoFilter.cpp
Core/WS/WsDmaController.cpp
Core/WS/WsEeprom.cpp
Core/WS/WsMemoryManager.cpp
Core/WS/WsPpu.cpp
Core/WS/WsSerial.cpp
Core/WS/WsTimer.cpp
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
#   GB core (src/gb/*) — it has its OWN list, MGBA_GB, plus MGBA_SM83 for the
#       SM83 CPU. M_CORE_GB is ON. The one file this list used to carry on its
#       own (src/gb/audio.c, for GBA-era GB audio tables) is NOT here: it is in
#       MGBA_GB with the rest of the GB core, and listing it twice is a
#       duplicate-symbol link error, not a harmless repeat.
# ⛔ mGBA's third-party/lzma/* is absent from MGBA because the APP gets those
# symbols from PPSSPP's identical 19.00 SDK copy (ext/lzma-sdk, in PPSPP_EXT_C);
# two copies break the app link the way lua/xxhash did.
#
# ⛔ BUT THE EARLIER JUSTIFICATION FOR THIS WAS WRONG, AND IT COST A REAL BUG.
# It used to claim that nothing in the kept mGBA subset references it. That is
# false: src/util/vfs/vfs-lzma.c IS in MGBA below, and it calls
# SzArEx_Init/Open/Extract, InFile_Open, FileInStream_CreateVTable,
# LookToRead2_CreateVTable and CrcGenerateTable directly -- that file IS the .7z
# archive support VDirOpenArchive dispatches to, and mCoreFind calls
# VDirOpenArchive on EVERY ROM path. On the app the PPSSPP copy satisfies them.
# On the HOST there is no PPSSPP, so they fell through to abort-on-call stubs in
# host_harness_stub.cpp -- and the first harness that booted a GBA ROM died
# inside the stub. That is exactly why GBA had never been host-verified.
# MGBA_LZMA below is the real SDK, compiled by build-host.sh only.
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
# ---- Game Boy / Game Boy Color, from mGBA's OWN src/gb/CMakeLists.txt ----
#
# ⛔ DERIVED FROM mGBA, NOT HAND-PICKED, AND THAT IS THE FIX. The Android
# overlay's GB list omitted src/gb/mbc/mbc.c -- the DISPATCHER whose table takes
# the &GBMBC*Create/&GBMBC*Load pointers to the mbc/ functions -- so all of those
# would have compiled and been unreferenced, and a GB cart with an MBC (most of
# them) could not have loaded. It also carried src/gb/test/*, which is mGBA's own
# unit tests and not part of the library. Both are fixed by generating the list.
#
# ⛔ M_CORE_GB IS WHAT MAKES THIS REACHABLE. mGBA's mCoreFind walks a filter table
# whose GB entry is compiled ONLY under M_CORE_GB; without the define the file is
# not just unhandled, it is UNRECOGNISED, so gba_load_rom's GB branch is dead code
# and a .gb reports "not a Game Boy or GBA ROM".
MGBA_GB="
src/gb/audio.c
src/gb/cheats.c
src/gb/core.c
src/gb/debugger/cli.c
src/gb/debugger/debugger.c
src/gb/debugger/symbols.c
src/gb/extra/proxy.c
src/gb/gb.c
src/gb/input.c
src/gb/io.c
src/gb/mbc.c
src/gb/mbc/huc-3.c
src/gb/mbc/licensed.c
src/gb/mbc/mbc.c
src/gb/mbc/pocket-cam.c
src/gb/mbc/tama5.c
src/gb/mbc/unlicensed.c
src/gb/memory.c
src/gb/overrides.c
src/gb/renderers/cache-set.c
src/gb/renderers/software.c
src/gb/serialize.c
src/gb/sio.c
src/gb/sio/lockstep.c
src/gb/sio/printer.c
src/gb/timer.c
src/gb/video.c"
# ---- SM83 (the Game Boy CPU), from mGBA's src/sm83/CMakeLists.txt ----
# Needed only because M_CORE_GB is on; the GBA's ARM core is separate (src/arm).
MGBA_SM83="
src/sm83/debugger/cli-debugger.c
src/sm83/debugger/debugger.c
src/sm83/debugger/memory-debugger.c
src/sm83/decoder.c
src/sm83/isa-sm83.c
src/sm83/sm83.c"
# mGBA's bundled 7z SDK, needed by vfs-lzma.c. Compiled by build-host.sh ONLY:
# the app supplies these symbols from PPSSPP's copy instead (see the note above).
MGBA_LZMA="
src/third-party/lzma/7zArcIn.c src/third-party/lzma/7zBuf.c
src/third-party/lzma/7zCrc.c src/third-party/lzma/7zCrcOpt.c
src/third-party/lzma/7zDec.c src/third-party/lzma/7zFile.c
src/third-party/lzma/7zStream.c src/third-party/lzma/Bcj2.c
src/third-party/lzma/Bra.c src/third-party/lzma/Bra86.c
src/third-party/lzma/BraIA64.c src/third-party/lzma/CpuArch.c
src/third-party/lzma/Delta.c src/third-party/lzma/Lzma2Dec.c
src/third-party/lzma/LzmaDec.c
"

MGBA_DEFS="-DM_CORE_GB -DBUILD_STATIC -DENABLE_DEBUGGERS -DENABLE_DIRECTORIES -DENABLE_SCRIPTING -DENABLE_VFS -DENABLE_VFS_FD -DHAVE_FREELOCALE -DHAVE_LOCALE -DHAVE_LOCALTIME_R -DHAVE_NEWLOCALE -DHAVE_PTHREAD_CREATE -DHAVE_PTHREAD_SETNAME_NP -DHAVE_PTHREAD_SET_NAME_NP -DHAVE_REALPATH -DHAVE_SETLOCALE -DHAVE_STRDUP -DHAVE_STRLCPY -DHAVE_STRNDUP -DHAVE_USELOCALE -DHAVE_VASPRINTF -DHAVE_XLOCALE -DLUA_VERSION_ONLY='\"5.4\"' -DM_CORE_GBA -DUSE_LUA -DUSE_LZMA -DUSE_PTHREADS -D_DARWIN_C_SOURCE"

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
# NOTE: PPSPP_LUA is satisfied by the app's own lua-5.4.7, not compiled
# separately. The two trees are the same 5.4.7 release (verified by diff), so
# one copy serves both the script engine and PPSSPP's LuaContext; compiling
# both puts 446 duplicate lua/xxhash-class symbols in the archive, which the
# app link (force_load) rejects. The list stays as a drift guard: the subset
# test verifies these files still exist upstream. The Linux host proof keeps
# compiling this list because it links no app lua.
# (Same story for xxhash: the app's xxhash/xxhash.c was unreferenced — verified
# by object scan — while PPSSPP hashes textures through zstd's bundled copy.)
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
