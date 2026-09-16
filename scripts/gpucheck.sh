#!/usr/bin/env bash
# gpucheck.sh — does the GPU actually have a renderer?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== GPU constructor: what renderer does it default to? ==="
grep -n 'GPU::GPU' -A 30 "$SRC/GPU.cpp" | head -40

echo
echo "=== GPU::SetRenderer ==="
grep -n 'void GPU::SetRenderer' -A 25 "$SRC/GPU.cpp" | head -30

echo
echo "=== GPU::GetFramebuffers ==="
grep -n 'bool GPU::GetFramebuffers' -A 15 "$SRC/GPU.cpp" | head -20
