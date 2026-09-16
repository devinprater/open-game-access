#!/usr/bin/env python3
"""Point fechapter's range section at ALL five 0x80 bitmap buffers, and widen the
current-map search. Runs from the repo root on either platform."""
import pathlib
import sys

# ⛔ Do NOT hardcode a Windows path: this script gets run inside WSL too, where
# C:/Users/... does not exist, and the failure looks like a missing file rather than
# a wrong path.
here = pathlib.Path(__file__).resolve().parent
p = here.parent / "Core" / "fechapter.cpp"
if not p.exists():
    p = pathlib.Path("Core/fechapter.cpp")
if not p.exists():
    print(f"!! fechapter.cpp not found from {here}")
    sys.exit(1)
s = p.read_text(encoding="utf-8")

old_start = s.index("    // ---- 2. movement range ----")
old_end = s.index("    // Per-tile impassability + occupancy")
new = '''    // ---- 2. movement range ----
    // MapStateManager holds FIVE 0x80-byte buffers (map.hpp): unk_c30, unk_cb0,
    // unk_d30, unk_db0, unk_e30. Each is 0x80 bytes = 1024 bits = 32x32, i.e. one bit
    // per tile — so any of them could be the blue movement overlay. unk_d30 is the one
    // the decomp's range loop reads, but it reads all-ones outside a move preview, so
    // sample ALL of them across frames and report the one that actually varies into a
    // plausible range. Guessing which buffer is "the" bitmap is how an earlier attempt
    // concluded the overlay does not exist.
    printf("\\n---- movement range: all candidate bitmaps ----\\n");
    struct Buf { const char* name; uint32_t off; };
    const Buf bufs[] = {
        {"unk_c30", 0xC30}, {"unk_cb0", 0xCB0}, {"unk_d30", 0xD30},
        {"unk_db0", 0xDB0}, {"unk_e30", 0xE30},
    };
    const int NBUF = 5;

    // Sample every 40 frames and keep the most "range-like" reading per buffer:
    // nonzero and not all-1024.
    int bestCount[NBUF], bestFrame[NBUF];
    for (int i = 0; i < NBUF; i++) { bestCount[i] = -1; bestFrame[i] = -1; }

    for (int probe = 0; probe < 1500; probe++) {
        for (int b = 0; b < NBUF; b++) {
            uint32_t base = msm + bufs[b].off;
            int on = 0;
            for (int i = 0; i < 0x400; i++)
                if ((R8(base + (i >> 3)) >> (i & 7)) & 1) on++;
            if (on > 0 && on < 1024) {
                if (bestCount[b] < 0) { bestCount[b] = on; bestFrame[b] = frames + probe; }
            }
        }
        if (!poke_frame(core)) break;
    }
    for (int b = 0; b < NBUF; b++) {
        if (bestCount[b] >= 0)
            printf("  %s: RANGE-LIKE  %d tiles reachable at frame %d\\n",
                   bufs[b].name, bestCount[b], bestFrame[b]);
        else
            printf("  %s: no range-like reading in 1500 frames (always 0 or all-1024)\\n",
                   bufs[b].name);
    }

    // Print the most promising buffer as a picture around the cursor.
    for (int b = 0; b < NBUF; b++) {
        if (bestCount[b] < 0) continue;
        uint32_t base = msm + bufs[b].off;
        int cx = R8(msm + 0x10 + 0x08), cy = R8(msm + 0x10 + 0x09);  // cursor tile
        printf("\\n  %s map (rows %d..%d, cursor at %d,%d):\\n",
               bufs[b].name, cy > 2 ? cy - 2 : 0, cy + 2, cx, cy);
        for (int y = (cy > 2 ? cy - 2 : 0); y <= cy + 2 && y < 32; y++) {
            char row[40]; int q = 0;
            for (int x = 0; x < 32; x++) {
                int idx = x | (y << 5);
                bool on = (R8(base + (idx >> 3)) >> (idx & 7)) & 1;
                row[q++] = (x == cx && y == cy) ? '@' : (on ? '#' : '.');
            }
            row[q] = 0;
            printf("    y=%-2d %s\\n", y, row);
        }
        break;   // just the first range-like buffer is enough to confirm
    }

'''
s = s[:old_start] + new + s[old_end:]

# Make the current-map search dereference: a MapData entry has char* at +0 that names
# the map, so look for any RAM pointer whose target's first field points at a string.
s = s.replace(
    '''        if (mapTable && v >= mapTable && v < mapTable + 0x1C * 64) {''',
    '''        if (mapTable && v >= mapTable && v < mapTable + 0x1C * 512) {''')
p.write_text(s, encoding="utf-8")
print("range section rewritten to probe all five buffers")
