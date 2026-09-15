#!/usr/bin/env bash
# apply-android-findings.sh — port the Android port's core-level fixes into the
# iOS core source tree.
#
# Android app: me.magnum.melonds.dev, the one that speaks Pokémon Black on
# TalkBack. Its core carries two fixes the upstream melonDS-lua tree does not.
# One is already in the iOS tree (nothing to do); the other is not, and it is
# the difference between a Pokémon game booting and not.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
F="$SRC/NDSCart/CartRetailIR.cpp"

[ -f "$F" ] || { echo "!! no $F"; exit 1; }

if grep -q 'u8 ret = 0;' "$F"; then
  echo "== already fixed: SPITransmitReceive initialises ret"
else
  python3 - "$F" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()

old = """    // TODO: emulate actual IR comm

    u8 ret;
    switch (IRCmd)
    {
    case 0x00: // pass-through
        ret = CartRetail::SPITransmitReceive(val);
        break;

    case 0x08: // ID
        ret = 0xAA;
        break;
    }
"""

new = """    // TODO: emulate actual IR comm

    // `ret` MUST be initialised. `IRCmd` can legitimately be a value outside
    // {0x00, 0x08} (Pokemon B/W and HG/SS issue other IR commands), and an
    // uninitialised return hands the game stack garbage. Measured symptom on the
    // Android port before this fix: the game's boot code never passes IR init,
    // so it renders nothing and the accessibility script only ever speaks its
    // loader line, forever.
    u8 ret = 0;
    switch (IRCmd)
    {
    case 0x00: // pass-through
        ret = CartRetail::SPITransmitReceive(val);
        break;

    case 0x08: // ID
        ret = 0xAA;
        break;

    default:
        break;
    }
"""

if old not in s:
    print("!! the expected source block was not found — refusing to patch blindly")
    sys.exit(1)
open(p, "w").write(s.replace(old, new, 1))
print("patched", p)
PY
fi

echo "== verification =="
sed -n '77,105p' "$F"
echo
echo "diff vs original upstream copy (if present):"
diff <(git -C "$HOME/src/melonds-lua" show HEAD:src/NDSCart/CartRetailIR.cpp 2>/dev/null) "$F" 2>/dev/null | head -30
