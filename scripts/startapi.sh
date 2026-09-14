#!/usr/bin/env bash
# startapi.sh — what exactly does NDS::Start() do, and what must precede it?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
echo "SRC=$SRC"

echo "=== Start / RunFrame / Stop bodies ==="
grep -n 'void NDS::Start()' -A 14 "$SRC/NDS.cpp"
echo '---'
grep -n 'void NDS::Stop()' -A 8 "$SRC/NDS.cpp"
echo '--- RunFrame head ---'
grep -n 'void NDS::RunFrame()' -A 30 "$SRC/NDS.cpp" | head -40
