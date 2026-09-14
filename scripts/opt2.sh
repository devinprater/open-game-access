#!/usr/bin/env bash
# opt2.sh — relink with the -O3 objects and measure 5 frames.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Keep every object that is NOT a -O1 core object (the -O3 copies come from fastobj).
KEEP=""
for o in Vendor/hostobj/*.o; do
  b=$(basename "$o")
  case "$b" in
    ARM.cpp.o|ARMInterpreter*.o|NDS.cpp.o|NDSCart.cpp.o|GPU*.o|DMA.cpp.o|DMA_Timings.cpp.o|\
SPU.cpp.o|Wifi*.o|Utils.cpp.o|SPI.cpp.o|SPI_Firmware.cpp.o|RTC.cpp.o|Savestate.cpp.o|\
FreeBIOS.cpp.o|ROMList.cpp.o|Mic.cpp.o|DSi*.o|CP15.cpp.o|CRC32.cpp.o|FATIO.cpp.o|\
FATStorage.cpp.o|GBACart*.o|ARCodeFile.cpp.o|ARDatabaseDAT.cpp.o|AREngine.cpp.o)
      continue ;;
  esac
  KEEP="$KEEP $o"
done

echo "fast objects: $(ls Vendor/fastobj/*.o | wc -l)"
echo "keeping: $(echo $KEEP | wc -w) shared objects"

g++ -O3 -g -ISources/CPokeCore/include -o Vendor/speedtest2-o3 Core/speedtest2.c \
    Vendor/fastobj/*.o $KEEP -lpthread -lm -ldl 2>&1 | grep -E 'undefined reference' | head -5
echo "link done"

timeout 150 ./Vendor/speedtest2-o3 "$HOME/hosttest-data/black.nds" "$HOME/hosttest-data/noop.lua" 5 \
  > "$HOME/sp-o3.log" 2>&1
echo "exit=$? (124=timeout)"
grep -E '== ' "$HOME/sp-o3.log" | tail -3
