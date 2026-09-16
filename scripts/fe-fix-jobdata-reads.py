#!/usr/bin/env python3
"""Fix the JobData reads in feterrain2.cpp by line index (patches kept failing on
whitespace, and the region was edited more than once)."""
import pathlib

p = pathlib.Path("C:/Users/Devin Prater/open-game-access/Core/feterrain2.cpp")
lines = p.read_text(encoding="utf-8").splitlines(keepends=True)

# Locate the block by content, not by a fixed line number.
start = None
for i, ln in enumerate(lines):
    if "JobData.unk_28 is the movement type" in ln:
        start = i
        break
if start is None:
    print("!! anchor not found")
    raise SystemExit(1)

# The block runs from the comment to the printf's closing line.
end = start
while end < len(lines) and "PrintableAt(jid, 20)" not in lines[end]:
    end += 1
end += 1  # include that line

print("replacing lines", start + 1, "..", end)
for i in range(start, end):
    print("  -", lines[i].rstrip())

new = [
    "            // JobData (include/unit.hpp):\n",
    "            //   /* 28 */ u8 unk_28;   <- movement TYPE (cost-matrix row index)\n",
    "            //   /* 29 */ u8 mov;      <- the class's movement stat\n",
    "            // \u26d4 unk_28 is a BYTE. Reading it with R32 gave 50464512 (0x03025C00),\n",
    "            // which looks like an address and is really one byte plus 3 neighbours.\n",
    "            uint8_t movType = pj ? R8(pj + 0x28) : 0xFF;\n",
    "            uint8_t jobMov  = pj ? R8(pj + 0x29) : 0;\n",
    "            printf(\"  slot %d unitMov=%2d jobMov=%2d movType=%3u HP=%2d (%2d,%2d) fac=%d jid=%s\\n\",\n",
    "                   slot, R8S(a + 0x6D), jobMov, movType, hp, x, y, fac,\n",
    "                   InRam(jid, 8) ? PrintableAt(jid, 20).c_str() : \"?\");\n",
    "            if (pj && movType < 32 && Plausible(unk28)) {\n",
    "                int str2 = ((int32_t) R32(unk28) + 3) & ~3;\n",
    "                printf(\"      cost row %u:\", movType);\n",
    "                for (int c = 0; c < 16; c++)\n",
    "                    printf(\" %4d\", (int) R8S(unk28 + 4 + (uint32_t)(movType * str2 + c)));\n",
    "                printf(\"\\n\");\n",
    "            }\n",
]
lines[start:end] = new
p.write_text("".join(lines), encoding="utf-8")
print("written")
