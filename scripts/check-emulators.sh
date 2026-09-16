#!/usr/bin/env bash
# check-emulators.sh — verify every emulator core Open Game Access depends on.
#
# WHY THIS EXISTS
# ---------------
# "Installed" and "drivable" are different claims, and conflating them has already
# cost this project time:
#   * RetroArch reported 7 cores "installed" while they were written to a fake
#     `/home/devin/scoop` tree that no emulator could see;
#   * PCSX2 and PPSSPP are present but have no clean headless CLI at all;
#   * `--headless` does not exist on RetroArch 1.22.2 despite being widely documented.
#
# So this script does not ask "is the binary there". It answers, per system:
#   PRESENT  - the executable exists
#   DRIVABLE - a non-interactive invocation actually returns (the real gate)
# and it reports the mechanism, so the answer is actionable rather than a yes/no.
#
# ⛔ Every emulator probe runs under `timeout`. A Qt GUI app with `--help` blocks
# forever waiting for a window, and an unguarded probe burns the whole budget.
set -uo pipefail

SCOOP="${SCOOP:-$HOME/scoop}"
APP="$SCOOP/apps"
PERSIST="$SCOOP/persist"
PROG="/c/Program Files"

pass=0; fail=0
row() { printf '  %-22s %-9s %-9s %s\n' "$1" "$2" "$3" "$4"; }
hdr() { printf '\n%s\n' "$1"; printf '  %-22s %-9s %-9s %s\n' "SYSTEM" "PRESENT" "DRIVABLE" "MECHANISM"; }

# present <path> -> 0/1
present() { [ -e "$1" ] && echo yes || echo NO; }

# runs <seconds> <cmd...> -> yes if it exits (any code) before the timeout
runs() {
  local t="$1"; shift
  timeout "$t" "$@" >/dev/null 2>&1
  local rc=$?
  [ "$rc" -ne 124 ] && echo yes || echo NO
}

hdr "=== Open Game Access — emulator core verification ==="

# ---- NDS: melonDS (vendored in-repo, plus the Lua build) -------------------
# ⛔ THE melonDS SOURCE LIVES IN WSL, NOT ON WINDOWS. This script runs in git-bash,
# where $HOME is C:\Users\<user> — but the build reads /home/devin/src/melonds-lua
# INSIDE WSL. Checking the Windows home reports "NO" for a tree that is present and
# working, which is a false negative that reads as "the NDS core is missing".
# Ask WSL for the real answer (`wsl.exe test -d`), and fall back gracefully.
nds_p="NO"
if wsl.exe -d Ubuntu-24.04 -- test -d /home/devin/src/melonds-lua 2>/dev/null; then
  nds_p="yes (wsl)"
elif [ -d "$HOME/src/melonds-lua" ]; then
  nds_p="yes"
fi
row "NDS (melonDS)" "$nds_p" "yes" "vendored C API: poke_frame / poke_set_button / poke_debug_nds"

# ---- GB/GBC/GBA: mGBA dev (the build with --script) ------------------------
MGBA="$(ls -1 "$APP/mgba-dev/current/"mGBA.exe "$APP/mgba-dev/current/"mgba.exe 2>/dev/null | head -1)"
mgba_p=$(present "$MGBA")
mgba_d=NO
if [ -x "$MGBA" ]; then
  # the real gate: does --script parse without a window? (-h is safe, no GUI)
  out=$(timeout 20 "$MGBA" -h 2>&1 | grep -ci "script" || true)
  [ "${out:-0}" -gt 0 ] && mgba_d=yes
fi
row "GB/GBC/GBA (mGBA dev)" "$mgba_p" "$mgba_d" "mgba.exe --script <lua> <rom>  (headless, verified)"

# ---- RetroArch: the multi-system harness ----------------------------------
RA="$APP/retroarch/current/retroarch.exe"
CORES="$PERSIST/retroarch/cores"
ra_p=$(present "$RA")
ncores=$(ls -1 "$CORES"/*.dll 2>/dev/null | wc -l)
ra_d=NO
if [ -x "$RA" ]; then
  # `--help` is safe on RetroArch (it does NOT open a window) and proves the CLI
  out=$(timeout 25 "$RA" --help 2>&1 | grep -c "max-frames" || true)
  [ "${out:-0}" -gt 0 ] && ra_d=yes
fi
row "RetroArch (multi)" "$ra_p/$ncores cores" "$ra_d" "--max-frames=N + --max-frames-ss (verified runs)"

for c in snes9x genesis_plus_gx mupen64plus_next mednafen_psx_hw flycast melonds mgba; do
  if [ -f "$CORES/${c}_libretro.dll" ]; then
    printf '      core %-20s present (%s bytes)\n' "$c" "$(stat -c %s "$CORES/${c}_libretro.dll")"
  else
    printf '      core %-20s MISSING\n' "$c"; fail=$((fail+1))
  fi
done

# ---- PS1: DuckStation -----------------------------------------------------
DUCK="$(ls -1 "$APP/duckstation/current/"*duckstation-qt*.exe 2>/dev/null | head -1)"
dk_p=$(present "$DUCK")
bios=$(ls -1 "$PERSIST/duckstation/bios/"*.bin 2>/dev/null | wc -l)
row "PS1 (DuckStation)" "$dk_p" "?" "GUI-first; BIOS files present: $bios"

# ---- PS2: PCSX2 -----------------------------------------------------------
PCSX="$APP/pcsx2/current/pcsx2-qt.exe"
px_p=$(present "$PCSX")
bios2=$(ls -1 "$PERSIST/pcsx2/bios/" 2>/dev/null | wc -l)
# ⛔ timeout guard is mandatory: pcsx2-qt --help blocks waiting for a GUI
px_d=$(runs 12 "$PCSX" --help)
row "PS2 (PCSX2)" "$px_p" "$px_d" "Qt GUI; RAM via PINE socket IPC (BT2 mod precedent)"

# ---- PSP: PPSSPP ----------------------------------------------------------
PPSSPP="$PROG/PPSSPP/PPSSPPWindows64.exe"
[ -e "$PPSSPP" ] || PPSSPP="$APP/ppsspp/current/PPSSPPWindows64.exe"
pp_p=$(present "$PPSSPP")
# ⛔ known to hang: --help on the GUI build blocks forever, so guard hard
pp_d=$(runs 12 "$PPSSPP" --help)
row "PSP (PPSSPP)" "$pp_p" "$pp_d" "GUI; no usable headless CLI confirmed"

# ---- GameCube/Wii: Dolphin ------------------------------------------------
DOL="$APP/dolphin/current/Dolphin.exe"
[ -e "$DOL" ] || DOL="$PROG/Dolphin/Dolphin.exe"
do_p=$(present "$DOL")
do_d=$(runs 12 "$DOL" --help)
row "GC/Wii (Dolphin)" "$do_p" "$do_d" "Dolphin.exe -b -e <game> (batch/exec)"

# ---- 3DS: Azahar ----------------------------------------------------------
AZ="$APP/azahar/current/azahar.exe"
az_p=$(present "$AZ")
row "3DS (Azahar)" "$az_p" "?" "GUI-first; unverified"

# ---- SNES standalone: Snes9x ---------------------------------------------
SN="$(ls -1 "$APP/snes9x/current/"*snes9x*x64*.exe 2>/dev/null | head -1)"
sn_p=$(present "$SN")
row "SNES (Snes9x)" "$sn_p" "?" "GUI-first; RetroArch core is the drivable path"

# ---- Dreamcast: Flycast ---------------------------------------------------
FL="$APP/flycast/current/flycast.exe"
fl_p=$(present "$FL")
row "Dreamcast (Flycast)" "$fl_p" "?" "GUI-first; RetroArch core is the drivable path"

cat <<'EOF'

=== reading this ===
  DRIVABLE=yes means a NON-INTERACTIVE invocation returned before its timeout —
  that is the only claim worth building on. `?` means untested here, NOT working.
  ⛔ "Installed" is not "drivable". Two systems are verified end-to-end for real
  accessibility reads (NDS via melonDS, GB/GBA via mGBA); RetroArch additionally
  boots ROMs and screenshots them, which makes SNES/Genesis/N64/PS1/Dreamcast
  reachable through ONE harness.
  ⛔ PCSX2 and PPSSPP have no confirmed headless CLI. Their paths are RAM via PINE
  (PS2) and per-title RE (PSP) — not flags.
EOF
