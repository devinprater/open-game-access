#!/usr/bin/env bash
# vba-reference-check.sh — determine whether VBA v3.1.0 can be used as a scripted
# known-good reference for the mGBA port.
#
# ⛔ WHY THIS MATTERS. The strongest available test of the mGBA shim is not "it produced
# some text" — it is running the SAME save in both emulators and diffing the spoken output.
# VBA v3.1.0 with the native Tolk/NVDA DLLs is the reference implementation that has worked
# for years. If it can be driven non-interactively, that comparison becomes automatable.
#
# ⛔ DO NOT LAUNCH VBA BLINDLY. It is a GUI app with a screen reader attached. Launching it
# while the user is at the machine would start speaking unexpectedly. This script only
# INSPECTS configuration; it does not run the emulator.
set -uo pipefail

P="/c/Users/Devin Prater/Dropbox/programs/pokemon-access"

echo "=============== vba.ini (relevant keys) ==============="
if [ -f "$P/vba.ini" ]; then
  grep -iE "lua|script|rom|battery|save|recent" "$P/vba.ini" 2>/dev/null | head -20
else
  echo "  !! no vba.ini"
fi

echo
echo "=============== ROMs available? ==============="
# Pokemon ROMs for the nine supported games, anywhere the user keeps them.
found=0
for dir in "$HOME/Dropbox/Games" "$HOME/Dropbox" "$HOME/Downloads" "$HOME/Documents"; do
  [ -d "$dir" ] || continue
  while IFS= read -r f; do
    echo "  $(basename "$f")"
    found=$((found + 1))
  done < <(find "$dir" -maxdepth 3 \( -iname "*pokemon*.gba" -o -iname "*pokemon*.gbc" -o -iname "*pokemon*.gb" \) 2>/dev/null | head -20)
done
[ "$found" -eq 0 ] && echo "  (none found in the usual places)"

echo
echo "=============== existing saves (the reference state) ==============="
ls -la "$P/save" 2>/dev/null | head -12
echo "  battery/ :"
ls "$P/battery" 2>/dev/null | head -12

echo
echo "=============== is there a scripted/headless entry? ==============="
# VBA-M has -l for Lua; stock VBA 1.8 does not run scripts from the CLI at all.
"$P/vba.exe" --help 2>&1 | head -5 || true
echo "  (empty output above means vba.exe accepts no --help and has no CLI scripting)"

echo
echo "=============== the DLLs are 32-bit? ==============="
for d in "$P/lua/Tolk.dll" "$P/lua/audio.dll" "$P/lua51.dll"; do
  [ -f "$d" ] || continue
  # PE header: machine type at offset 0x3C -> PE sig -> 2 bytes machine.
  m=$(od -An -tx2 -j$((0x3C)) -N4 "$d" 2>/dev/null | awk '{print $1}')
  printf '  %-14s ' "$(basename "$d")"
  if [ -n "$m" ]; then
    off=$((16#$m))
    mach=$(od -An -tx2 -j$((off + 4)) -N2 "$d" 2>/dev/null | tr -d ' ')
    case "$mach" in
      4c01) echo "32-bit (i386)" ;;
      6486) echo "64-bit (x64)" ;;
      *)    echo "machine=0x$mach" ;;
    esac
  else
    echo "?"
  fi
done

echo
echo "=============== conclusion inputs ==============="
echo "  VBA is configured with luaDir, so it auto-runs the reader ON LOAD."
echo "  Whether that can be scripted end-to-end depends on the ROM + save being present"
echo "  and on VBA accepting a CLI ROM argument (untested here — launching would speak)."
