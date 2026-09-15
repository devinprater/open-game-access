#!/usr/bin/env bash
# renderer3d.sh — where does Rend3D come from? A null/failed Renderer3D would
# make the GX FIFO claim to be full forever, which stalls the ARM9 in exactly the
# wait loop observed (CPUStop_GXStall).
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== renderer source files ==="
ls "$SRC" | grep -i render

echo
echo "=== SoftRenderer constructor: full ==="
awk '/^SoftRenderer::SoftRenderer/,/^}/' "$SRC/GPU_Soft.cpp" | head -45

echo
echo "=== where is Rend3D assigned? ==="
grep -rn 'Rend3D =' "$SRC" --include=*.cpp | head -10

echo
echo "=== Renderer base ctor / SetRenderer3D ==="
grep -rn 'Renderer::Renderer\|SetRenderer3D\|Rend3D' "$SRC/GPU_Soft.cpp" "$SRC/GPU.cpp" | head -15
