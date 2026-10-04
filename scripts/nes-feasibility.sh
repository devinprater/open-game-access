#!/usr/bin/env bash
# nes-feasibility.sh — does the Mesen NES core build for this project's targets?
#
# ⛔ THIS IS AN ADMISSION TEST, NOT A BUILD. It answers one question: can the NES
# core be compiled for aarch64-linux-android at all? Nothing here links a ROM,
# boots a game, or claims the backend exists. mGBA was admitted to this repo the
# same way — measured before it was promised.
#
# ⛔ THE NDK IS WINDOWS-HOSTED, SO THIS RUNS THE COMPILER THROUGH cmd.exe AND
# STAGES THE TREE ON THE WINDOWS SIDE. The NDK's clang cannot open WSL paths, and
# the source tree is in WSL, so neither side can see the other's paths directly.
# Utilities/ is a SIBLING of Core/, so staging Core/ alone fails on pch.h.
#
# Usage: scripts/nes-feasibility.sh [mesen-src]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESEN="${1:-$HOME/src/mesen}"
[ -d "$MESEN/Core" ] || { echo "!! no Mesen source at $MESEN" >&2; exit 1; }

NDK="${OGA_NDK:-$(ls -d /mnt/c/asdk/ndk/* 2>/dev/null | sort -V | tail -1)}"
[ -d "$NDK" ] || { echo "!! no Android NDK found (set OGA_NDK)" >&2; exit 1; }
WINNDK="$(echo "$NDK" | sed 's|^/mnt/\([a-z]\)/|\1:/|')"
CXX="$WINNDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
SYS="$WINNDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
[ -f "$(echo "$CXX" | sed 's|^\([a-z]\):|/mnt/\1|')" ] || {
  echo "!! no clang++ at $CXX" >&2; exit 1; }

STAGE="/mnt/c/oga-mesen-stage"
echo "== staging the NES core on the Windows side (Utilities/ is a Core/ sibling)"
rm -rf "$STAGE"; mkdir -p "$STAGE"
for d in Core Utilities; do cp -r "$MESEN/$d" "$STAGE/" 2>/dev/null; done

# The measured set: the NES core plus the Shared layer it links against.
( cd "$MESEN" && find Core/NES Core/Shared -name '*.cpp' | sort ) > "$STAGE/tus.txt"
COUNT="$(wc -l < "$STAGE/tus.txt" | tr -d ' ')"
echo "== compiling $COUNT TUs for aarch64-linux-android26"

cat > "$STAGE/run.bat" <<'BAT'
@echo off
setlocal enabledelayedexpansion
set CXX=C:\asdk\ndk\PLACEHOLDER\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe
set SYS=C:\asdk\ndk\PLACEHOLDER\toolchains\llvm\prebuilt\windows-x86_64\sysroot
set INC=-IC:\oga-mesen-stage -IC:\oga-mesen-stage\Core -IC:\oga-mesen-stage\Core\NES -IC:\oga-mesen-stage\Core\Shared -IC:\oga-mesen-stage\Core\Shared\Base -IC:\oga-mesen-stage\Core\Dependencies
del /q C:\oga-mesen-stage\fail.txt 2>nul
set OK=0
set BAD=0
for /f "usebackq delims=" %%F in ("C:\oga-mesen-stage\tus.txt") do (
  "%CXX%" --target=aarch64-linux-android26 --sysroot="%SYS%" -std=c++17 -fsyntax-only -O0 -w -include C:\oga-mesen-stage\Core\pch.h %INC% "C:\oga-mesen-stage\%%F" >nul 2>>C:\oga-mesen-stage\fail.txt
  if !ERRORLEVEL! EQU 0 ( set /a OK+=1 ) else ( set /a BAD+=1 & echo %%F>>C:\oga-mesen-stage\fail.txt )
)
echo OK=!OK! BAD=!BAD!
BAT

NDKVER="$(basename "$NDK")"
sed -i "s|PLACEHOLDER|$NDKVER|g" "$STAGE/run.bat"
cp "$STAGE/run.bat" /mnt/c/Users/Public/oga-nes-stage-run.bat

cd /mnt/c/Users/Public || exit 1
OUT="$(cmd.exe /c oga-nes-stage-run.bat 2>&1 | tr -d '\r')"
echo "$OUT"
echo
echo "== failing TUs =="
FAILS="$(grep -E '^Core/' "$STAGE/fail.txt" 2>/dev/null)"
if [ -n "$FAILS" ]; then
  echo "$FAILS" | head -20
  echo "!! $COUNT TUs offered, some failed — the NES core is NOT admitted yet" >&2
  exit 1
fi
echo "(none)"
echo
echo "== the MEASURED source list (paste into core-sources.sh MESEN_NES) =="
cat "$STAGE/tus.txt"
echo
echo "PASS: the Mesen NES core compiles for aarch64-linux-android26 ($COUNT/$COUNT TUs)."
echo "⚠ This is an ADMISSION TEST. No ROM was booted and no host glue exists."
