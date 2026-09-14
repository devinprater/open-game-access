#!/usr/bin/env bash
# opt-and-rom.sh — (1) does -Ofast fix the interpreter speed? (2) is the stall
# specific to Pokémon Black, or does any ROM crawl?
set -uo pipefail
cd "$HOME/pokemon-access-ios"
ROM="$HOME/hosttest-data/black.nds"
NOOP="$HOME/hosttest-data/noop.lua"

echo "###### rebuild the CORE at -Ofast (Delta's setting) ######"
# Recompile just the hot CPU/graphics translation units at -Ofast into a
# separate object dir so the -O1 objects are untouched for comparison.
OBJ2="$HOME/pokemon-access-ios/Vendor/fastobj"
mkdir -p "$OBJ2"

SRC="$HOME/src/melonds-lua/src"
HOT="ARM.cpp ARMInterpreter.cpp ARMInterpreter_ALU.cpp ARMInterpreter_Branch.cpp
     ARMInterpreter_LoadStore.cpp NDSCart.cpp NDS.cpp GPU.cpp GPU2D.cpp GPU2D_Soft.cpp
     GPU3D.cpp GPU3D_Soft.cpp GPU3D_Texcache.cpp DMA.cpp SPU.cpp Wifi.cpp WifiAP.cpp
     Utils.cpp SPI.cpp RTC.cpp Savestate.cpp FreeBIOS.cpp ROMList.cpp Mic.cpp
     DSi.cpp DSi_AES.cpp DSi_Camera.cpp DSi_DSP.cpp DSi_I2C.cpp DSi_I2S.cpp DSi_NAND.cpp
     DSi_NDMA.cpp DSi_NWifi.cpp DSi_SD.cpp DSi_SPI_TSC.cpp CP15.cpp CRC32.cpp
     DMA_Timings.cpp FATIO.cpp FATStorage.cpp GBACart.cpp GBACartMotionPak.cpp
     ARCodeFile.cpp ARDatabaseDAT.cpp AREngine.cpp"

FLAGS="-O3 -ffast-math -fno-math-errno -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything"
INC="-I$HOME/pokemon-access-ios/Core -I$HOME/pokemon-access-ios/Sources/CPokeCore/include -I$SRC -I$HOME/src/lua-5.4.7/src -I$SRC/teakra/include"

echo "compiling $(echo $HOT | wc -w) units at -O3..."
echo "$HOT" | tr ' ' '\n' | grep -v '^$' | xargs -P "$(nproc)" -I{} bash -c \
  "g++ $FLAGS $INC -std=c++17 -c '$SRC/{}' -o '$OBJ2/{}'.o 2>/dev/null"

ls "$OBJ2"/*.o 2>/dev/null | wc -l

echo
echo "###### 5 frames at -O3 (fast objects + rest) ######"
g++ -O3 -g -ISources/CPokeCore/include -o Vendor/speedtest2-o3 Core/speedtest2.c \
    "$OBJ2"/*.o Vendor/hostobj/poke_platform.o Vendor/hostobj/pokecore.o \
    Vendor/hostobj/lua_*.o Vendor/hostobj/teakra_*.o 2>&1 | grep -E '\berror\b|undefined' | head -5
echo "link=$?"
timeout 150 ./Vendor/speedtest2-o3 "$ROM" "$NOOP" 5 > "$HOME/sp-o3.log" 2>&1
echo "exit=$?"
grep -E '== ' "$HOME/sp-o3.log" | tail -3
