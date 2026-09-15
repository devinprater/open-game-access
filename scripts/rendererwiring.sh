#!/usr/bin/env bash
# rendererwiring.sh — how is the 3D renderer wired, and what happens with a null
# one? Android installs a renderer explicitly; my core passes nullptr.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "########## 1. Renderer.h: how is Rend3D created? ##########"
grep -n 'Rend3D\|Renderer(' "$SRC/Renderer.h" | head -20

echo
echo "########## 2. Renderer base ctor body ##########"
awk '/^Renderer::Renderer/,/^}/' "$SRC/Renderer.cpp" | head -25

echo
echo "########## 3. GPU3D: does a missing renderer stall the GX FIFO? ##########"
grep -n 'Rend\|Renderer' "$SRC/GPU3D.cpp" | head -25

echo
echo "########## 4. GXSTAT / FIFO stall logic ##########"
grep -n 'GXStall\|FIFO\|GXSTAT\|CheckFIFO' "$SRC/GPU3D.cpp" | head -30

echo
echo "########## 5. how does Android build its Renderer arg? ##########"
grep -n 'Renderer' -B3 -A6 "/mnt/c/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/src/main/cpp/EmulatorArgsBuilder.cpp" | head -40
