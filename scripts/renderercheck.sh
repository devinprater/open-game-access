#!/usr/bin/env bash
# renderercheck.sh — THE decisive question: what does the GPU do when
# args.Renderer is null?
#
# Android explicitly installs a renderer (GLRenderer/SoftRenderer). This core
# passes args.Renderer = nullptr and assumes "the core's default". If a null
# renderer means the 3D engine never consumes its FIFO, a game that waits on
# GXSTAT's FIFO-empty bit spins forever: no graphics ever loaded, white screen,
# CPUs alive. That matches every symptom observed.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== NDSArgs::Renderer consumption ==="
grep -rn 'args.Renderer\|Renderer = \|SetRenderer' "$SRC/NDS.cpp" | head -20

echo
echo "=== GPU.h: renderer member + SetRenderer ==="
grep -n 'Renderer\*\|SetRenderer\|Renderer3D' "$SRC/GPU.h" | head -20

echo
echo "=== GPU.cpp: does GPU::GPU install a default renderer? ==="
grep -n 'GPU::GPU' -A 30 "$SRC/GPU.cpp" | head -45
