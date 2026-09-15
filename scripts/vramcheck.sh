#!/usr/bin/env bash
# vramcheck.sh — the decisive test: has the game written ANY graphics data?
#
#   * VRAM all zeros  → the game never got far enough to load graphics: it is
#                       stuck in early code, and the white screen is a symptom of
#                       that, not a rendering bug.
#   * VRAM has data but the framebuffer is flat → rendering/display-config issue.
# Also prints the display registers and halt state.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== GPU: what is public? ==="
grep -n 'PowerControl9\|DISPSTAT\|VRAM\b\|Framebuffer\|u8 VRAM\|public:\|private:' "$SRC/GPU.h" | head -30

echo
echo "=== is there a VRAMBank pointer? ==="
grep -n 'VRAM' "$SRC/GPU.h" | head -15

echo
echo "=== NDS: VRAM accessor? ==="
grep -n 'VRAM' "$SRC/NDS.h" | head -10
