#!/usr/bin/env bash
# setrenderer.sh — what does GPU::SetRenderer do with a null renderer?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== GPU::SetRenderer (full) ==="
grep -n 'void GPU::SetRenderer' -A 22 "$SRC/GPU.cpp"

echo
echo "=== SoftRenderer registration mechanism ==="
grep -rn 'SoftRenderer::SoftRenderer' -A 10 "$SRC/GPU_Soft.cpp" 2>/dev/null | head -14

echo
echo "=== Is there a global renderer factory list? ==="
grep -rn 'RendererReg\|RegisterRenderer' "$SRC"/*.h "$SRC"/*.cpp 2>/dev/null | head -10
