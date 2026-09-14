#!/usr/bin/env bash
# irisolate.sh — does the IR cart frontend stall Pokemon Black?
#
# CartRetailIR::SPITransmitReceive returns an uninitialised `ret` for any IR
# command that is not 0x00 or 0x08 — and B/W (IRBO, irversion 2) is an IR cart.
#
# This builds a variant of the core where Black is loaded as a PLAIN retail cart
# (skipping the IR path) and compares. If Black then progresses, the IR device is
# the stall; if it stays parked at 0x020882DC, IR is innocent and the cause is
# elsewhere.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
BK="$HOME/src/melonds-lua-irpatch"

echo "=== make a patched copy of the core tree ==="
rm -rf "$BK"
cp -r "$SRC" "$BK"
cp "$SRC/NDSCart.cpp" "$BK/NDSCart.cpp"

# Force irversion back to 0 so Black loads via CartRetail instead of CartRetailIR.
python3 - <<'PY'
import os
p = os.path.expanduser('~/src/melonds-lua-irpatch/NDSCart.cpp')
s = open(p, encoding='utf-8', errors='replace').read()
old = "            irversion = 2; // Pokémon HG/SS, B/W, B2/W2"
new = "            irversion = 2; // Pokémon HG/SS, B/W, B2/W2\n            irversion = 0; // IRPATCH: force plain retail cart"
assert old in s, "anchor not found"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("patched: Black will load as a plain retail cart")
PY

echo
echo "=== rebuild only the two TUs that matter, into a separate object dir ==="
OBJ="$HOME/irpatch-obj"; mkdir -p "$OBJ"
cp "$HOME/pokemon-access-ios/Vendor/hostobj/"*.o "$OBJ/" 2>/dev/null

g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 -Wno-everything \
  -I"$HOME/pokemon-access-ios/Core" -I"$HOME/pokemon-access-ios/Sources/CPokeCore/include" \
  -I"$BK" -I"$HOME/src/lua-5.4.7/src" -I"$BK/teakra/include" -std=c++17 \
  -c "$BK/NDSCart.cpp" -o "$OBJ/NDSCart.o" 2>&1 | grep -E '\berror\b' | head -5
echo "NDSCart.o rebuilt: $([ -f "$OBJ/NDSCart.o" ] && echo yes || echo NO)"

echo
echo "=== link the blackprobe against the IR-patched objects ==="
g++ -O2 -g -I"$HOME/pokemon-access-ios/Core" -I"$HOME/pokemon-access-ios/Sources/CPokeCore/include" \
  -I"$BK" -std=c++17 -o "$HOME/pokemon-access-ios/Vendor/blackprobe_ir" \
  "$HOME/pokemon-access-ios/Core/blackprobe.cpp" "$OBJ"/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -5
echo "linked: $([ -x "$HOME/pokemon-access-ios/Vendor/blackprobe_ir" ] && echo yes || echo NO)"

echo
echo "########## BLACK as a PLAIN RETAIL cart (IR path skipped), 60000 frames ##########"
cd "$HOME/pokemon-access-ios"
timeout 400 ./Vendor/blackprobe_ir "$HOME/hosttest-data/black.nds" "" "" "" 60000 2>&1 | tail -14
