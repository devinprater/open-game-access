#!/usr/bin/env bash
# forkdiff.sh — what did melonDS-lua actually change in the core? A fork change
# to boot/GPU/RunFrame is the prime suspect for "same ROM renders on Android,
# not here".
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
cd "$SRC"

echo "=== is it a git repo? ==="
git rev-parse --short HEAD 2>/dev/null && git log --oneline -5 2>/dev/null

echo
echo "=== what does the latest commit touch? ==="
git show --stat HEAD 2>/dev/null | head -25

echo
echo "=== Lua-related hooks inside the CORE (not tools/) ==="
grep -rln 'Lua\|lua_' . --include=*.cpp --include=*.h 2>/dev/null | head -20

echo
echo "=== does NDS::RunFrame or GPU call into Lua? ==="
grep -n 'lua\|Lua' NDS.cpp 2>/dev/null | head -10
grep -n 'lua\|Lua' GPU.cpp 2>/dev/null | head -10
