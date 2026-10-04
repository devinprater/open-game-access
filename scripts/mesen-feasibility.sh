#!/usr/bin/env bash
# mesen-feasibility.sh — WHICH Mesen2 consoles build for this project's targets.
#
# ⛔ GENERALISES nes-feasibility.sh, AND FOR THE SAME REASON IT EXISTS: an
# admission test measures a core BEFORE the registry calls it PLANNED, so a
# console is never promised a core that cannot compile. Mesen2 ships seven
# console cores in one tree (Core/NES, SNES, Gameboy, GBA, PCE, SMS, WS), each
# self-contained and linking only against Core/Shared — so "which Mesen systems
# can Open Game Access host" is a measurable question, not a guess.
#
# ⛔ THIS IS AN ADMISSION TEST, NOT A BUILD. It compiles with -fsyntax-only. No
# ROM is booted, no host glue exists, and PASS here does NOT mean a backend.
#
# ⛔ THE NDK IS WINDOWS-HOSTED, SO THE COMPILER RUNS THROUGH cmd.exe AND THE TREE
# IS STAGED ON THE WINDOWS SIDE. The NDK's clang cannot open WSL paths and the
# source tree is in WSL, so neither side sees the other's paths. Utilities/ is a
# SIBLING of Core/, so staging Core/ alone fails on pch.h.
#
# Usage: scripts/mesen-feasibility.sh [mesen-src]
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
rm -rf "$STAGE"; mkdir -p "$STAGE"

CONSOLES="NES SNES Gameboy GBA PCE SMS WS"

echo "== staging Core/ + Utilities/ on the Windows side (Utilities/ is a Core/ sibling)"
for d in Core Utilities; do cp -r "$MESEN/$d" "$STAGE/" 2>/dev/null; done

( cd "$MESEN" && find Core/Shared -name '*.cpp' | sort ) > "$STAGE/tus-SHARED.txt"
SHARED_N=$(wc -l < "$STAGE/tus-SHARED.txt" | tr -d ' ')

TOTAL=0
for c in $CONSOLES; do
  ( cd "$MESEN" && find "Core/$c" -name '*.cpp' | sort ) > "$STAGE/tus-$c.txt"
  n=$(wc -l < "$STAGE/tus-$c.txt" | tr -d ' ')
  [ "$n" -eq 0 ] && { echo "!! Core/$c has no .cpp files — Mesen tree moved?" >&2; exit 1; }
  TOTAL=$((TOTAL + n + SHARED_N))
done
echo "== compiling $TOTAL TU-compiles across $CONSOLES + SHARED for aarch64-linux-android26"

cat > "$STAGE/run.bat" <<'BAT'
@echo off
setlocal enabledelayedexpansion
set CXX=C:\asdk\ndk\PLACEHOLDER\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe
set SYS=C:\asdk\ndk\PLACEHOLDER\toolchains\llvm\prebuilt\windows-x86_64\sysroot
set STAGE=C:\oga-mesen-stage
set BASE=-I%STAGE% -I%STAGE%\Core -I%STAGE%\Core\Shared -I%STAGE%\Core\Shared\Base -I%STAGE%\Core\Dependencies
for %%C in (SHARED NES SNES Gameboy GBA PCE SMS WS) do (
  if "%%C"=="SHARED" ( set INC=%BASE% ) else ( set INC=%BASE% -I%STAGE%\Core\%%C )
  set OK=0
  set BAD=0
  del /q %STAGE%\fail-%%C.txt 2>nul
  for /f "usebackq delims=" %%F in ("%STAGE%\tus-%%C.txt") do (
    "%CXX%" --target=aarch64-linux-android26 --sysroot="%SYS%" -std=c++17 -fsyntax-only -O0 -w -include %STAGE%\Core\pch.h !INC! "%STAGE%\%%F" >nul 2>>%STAGE%\fail-%%C.txt
    if !ERRORLEVEL! EQU 0 ( set /a OK+=1 ) else ( set /a BAD+=1 & echo %%F>>%STAGE%\fail-%%C.txt )
  )
  echo RESULT %%C !OK! !BAD!
)
BAT

NDKVER="$(basename "$NDK")"
sed -i "s|PLACEHOLDER|$NDKVER|g" "$STAGE/run.bat"
cp "$STAGE/run.bat" /mnt/c/Users/Public/oga-mesen-sweep.bat

cd /mnt/c/Users/Public || exit 1
OUT="$(cmd.exe /c oga-mesen-sweep.bat 2>&1 | tr -d '\r')"
echo "$OUT"
echo

echo "== per-console admission"
FAILED=0
printf '  %-9s %-6s %s\n' CONSOLE TUs VERDICT
for c in SHARED NES SNES Gameboy GBA PCE SMS WS; do
  n=$(wc -l < "$STAGE/tus-$c.txt" | tr -d ' ')
  bad="$(grep -cE '\.cpp$' "$STAGE/fail-$c.txt" 2>/dev/null | tr -d ' ')"
  bad="${bad:-0}"
  if [ "$bad" -eq 0 ]; then
    printf '  %-9s %-6s %s\n' "$c" "$n" "PASS"
  else
    printf '  %-9s %-6s %s\n' "$c" "$n" "FAIL ($bad)"
    grep -E '\.cpp$' "$STAGE/fail-$c.txt" | head -8 | sed 's/^/      /'
    FAILED=1
  fi
done
echo

if [ "$FAILED" -ne 0 ]; then
  echo "!! at least one Mesen console does NOT build for the target — see above" >&2
  exit 1
fi

echo "== the MEASURED source lists (paste into core-sources.sh)"
echo "   Core/Shared is compiled ONCE and shared by every console."
echo
for c in NES SNES Gameboy GBA PCE SMS WS; do
  echo "MESEN_$c:"
  cat "$STAGE/tus-$c.txt"
  echo
done
echo "MESEN_SHARED:"
cat "$STAGE/tus-SHARED.txt"
echo
echo "PASS: all seven Mesen console cores compile for aarch64-linux-android26."
echo "⚠ ADMISSION TEST ONLY. No ROM booted, no host glue, no backend exists."
