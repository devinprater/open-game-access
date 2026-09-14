#!/usr/bin/env bash
# opt3.sh — FULL core rebuild at -O3 (Delta's "fast" setting) and measure.
# The previous attempt mixed -O1 and -O3 objects and missed units; a complete
# second object tree is the only honest comparison.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
LUA="$HOME/src/lua-5.4.7/src"
OBJ="$ROOT/Vendor/fastobj"
mkdir -p "$OBJ"

COMMON="-O3 -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything"
INC="-I$ROOT/Core -I$ROOT/Sources/CPokeCore/include -I$SRC -I$LUA -I$SRC/teakra/include"
CXXFLAGS="$COMMON $INC -std=c++17"
CFLAGS="$COMMON $INC -std=gnu11"

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
TEAKRA="
teakra/src/teakra.cpp teakra/src/ahbm.cpp teakra/src/apbp.cpp teakra/src/btdmp.cpp
teakra/src/disassembler.cpp teakra/src/disassembler_c.cpp teakra/src/dma.cpp
teakra/src/memory_interface.cpp teakra/src/mmio.cpp teakra/src/parser.cpp
teakra/src/processor.cpp teakra/src/timer.cpp teakra/src/test_generator.cpp
"
GLUE="$ROOT/Core/poke_platform.cpp $ROOT/Core/pokecore.cpp"

compile() {
  local lang="$1" src="$2" tag="$3"
  local out="$OBJ/$tag.o"
  [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0
  local flags="$CXXFLAGS"; local cc="g++"
  case "$lang" in
    cc)  flags="$CFLAGS"; cc="gcc" ;;
    lua) flags="$CFLAGS -DLUA_USE_POSIX"; cc="gcc" ;;
  esac
  "$cc" $flags -c "$src" -o "$out" 2> "$OBJ/$tag.err" || { echo "FAIL $tag"; tail -8 "$OBJ/$tag.err"; }
}

{
  for f in $CORE_CPP; do printf '%s|cxx|%s\n' "$SRC/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $CORE_C;   do printf '%s|cc|%s\n'  "$SRC/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in $TEAKRA;   do printf '%s|cxx|%s\n' "$SRC/$f" "$(echo "$f" | tr '/' '_')"; done
  for f in "$LUA"/*.c; do b="$(basename "$f" .c)"
    [ "$b" = "lua" ] || [ "$b" = "luac" ] || printf '%s|lua|%s\n' "$f" "lua_$b"
  done
  for f in $GLUE; do printf '%s|cxx|%s\n' "$f" "$(basename "$f" .cpp)"; done
} > "$OBJ/list.txt"

echo "== compiling $(wc -l < "$OBJ/list.txt") units at -O3 =="
export CXXFLAGS CFLAGS OBJ
export -f compile
xargs -a "$OBJ/list.txt" -P "$(nproc)" -I{} bash -c '
  IFS="|" read -r src lang tag <<< "{}"
  compile "$lang" "$src" "$tag"
'
echo "objects: $(ls "$OBJ"/*.o | wc -l)"

g++ -O3 -g -I"$ROOT/Sources/CPokeCore/include" -o "$ROOT/Vendor/speedtest2-o3" \
    "$ROOT/Core/speedtest2.c" "$OBJ"/*.o -lpthread -lm -ldl 2>&1 | grep -E 'undefined reference' | head -5
echo "linked"

timeout 200 "$ROOT/Vendor/speedtest2-o3" "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/sp-o3.log" 2>&1
echo "exit=$? (124=timeout)"
grep -E '== ' "$HOME/sp-o3.log" | tail -3
