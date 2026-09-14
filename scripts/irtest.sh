#!/usr/bin/env bash
# irtest.sh — CartRetailIR::SPITransmitReceive returns an UNINITIALISED variable
# for any IR command that is not 0x00 or 0x08:
#
#     u8 ret;
#     switch (IRCmd)
#     {
#     case 0x00: ret = CartRetail::SPITransmitReceive(val); break;
#     case 0x08: ret = 0xAA; break;
#     }          // <-- no default: ret is garbage
#
# Pokemon Black/White are IR carts (gamecode IRBO -> irversion 2), so they talk
# to this device during boot. If the game polls IR and gets garbage, it may spin.
#
# Test: does Black progress if the IR device is replaced by a plain retail cart?
# That isolates the IR frontend from everything else.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== is IR actually reached by B/W? check the game code mapping ==="
python3 - <<'PY'
gc = "IRBO"
print("gamecode:", gc)
print("(gamecode & 0xFF) == 'I':", (ord(gc[0]) & 0xFF) == ord('I'))
b = (ord(gc[0]) | (ord(gc[1])<<8) | (ord(gc[2])<<16) | (ord(gc[3])<<24))
print("gamecode as u32: 0x%08X" % b)
print("(gamecode >> 8) & 0xFF =", (b >> 8) & 0xFF, "= chr", chr((b>>8)&0xFF))
print("  -> < 'P'(0x50)?", ((b>>8)&0xFF) < ord('P'), "=> irversion", 1 if ((b>>8)&0xFF) < ord('P') else 2)
PY

echo
echo "=== the IR transmit function as it stands (uninitialised ret) ==="
grep -n 'SPITransmitReceive' -A 25 "$SRC/NDSCart/CartRetailIR.cpp" | tail -30

echo
echo "=== does SPITransmitReceive get called for the cart at all? ==="
grep -rn 'SPITransmitReceive' "$SRC/SPI.cpp" "$SRC/NDS.cpp" | head -10
