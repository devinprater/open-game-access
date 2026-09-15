#!/usr/bin/env bash
# fe-identity.sh — game identity record for every ROM under analysis.
#
# ⛔ WHY THIS MATTERS: addresses and symbols differ across regions and revisions. A
# memory map recorded without the exact ROM it came from is a trap for the next person
# — the fields will be off by a few bytes and nothing will say why. So every finding in
# docs/reverse-engineering/ names the ROM identity it was measured against, and this
# script produces that identity.
#
# ⛔ NO ROM CONTENT IS EMITTED. Only hashes and header fields: the point is to identify
# a file the user already owns, not to reproduce it.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

files=("$@")
if [ ${#files[@]} -eq 0 ]; then
  for e in nds gba gb gbc smc sfc nes z64 n64 iso cue chd; do
    while IFS= read -r -d '' f; do files+=("$f"); done \
      < <(find "$HOME/roms" -maxdepth 2 -iname "*.$e" -print0 2>/dev/null)
  done
fi
[ ${#files[@]} -gt 0 ] || { echo "no ROMs given and none found under ~/roms" >&2; exit 2; }

for f in "${files[@]}"; do
  [ -f "$f" ] || { echo "!! missing: $f" >&2; continue; }
  echo "=============================================================="
  echo "file      : $f"
  echo "bytes     : $(stat -c %s "$f")"
  echo "sha1      : $(sha1sum "$f" | cut -d' ' -f1)"
  echo "sha256    : $(sha256sum "$f" | cut -d' ' -f1)"
  case "${f,,}" in
    *.nds)
      # NDS header: title at 0x00, game code at 0x0C, maker at 0x10, revision at 0x1E.
      printf 'title     : %s\n' "$(dd if="$f" bs=1 skip=0 count=12 2>/dev/null | tr -d '\0')"
      printf 'gamecode  : %s\n' "$(dd if="$f" bs=1 skip=12 count=4 2>/dev/null)"
      printf 'makercode : %s\n' "$(dd if="$f" bs=1 skip=16 count=2 2>/dev/null)"
      printf 'revision  : %d\n' "$(od -An -tu1 -j30 -N1 "$f" 2>/dev/null | tr -d ' ')"
      ;;
    *.gba)
      printf 'title     : %s\n' "$(dd if="$f" bs=1 skip=0xA0 count=12 2>/dev/null | tr -d '\0')"
      printf 'gamecode  : %s\n' "$(dd if="$f" bs=1 skip=0xAC count=4 2>/dev/null)"
      ;;
    *.gb|*.gbc)
      printf 'title     : %s\n' "$(dd if="$f" bs=1 skip=0x134 count=15 2>/dev/null | tr -d '\0')"
      ;;
    *.smc|*.sfc)
      printf 'header@200: %s\n' "$(dd if="$f" bs=1 skip=0x7FC0 count=21 2>/dev/null | tr -d '\0' | head -c 21)"
      ;;
  esac
done
