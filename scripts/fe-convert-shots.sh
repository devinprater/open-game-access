#!/usr/bin/env bash
# fe-convert-shots.sh — convert every PPM in ~/fe/out to PNG next to the evidence dir.
# Names are hard-coded per iteration rather than built with a shell loop variable,
# because expanding `$f` inside a nested quoting context through the Windows->WSL
# boundary produced empty names (".ppm") and silent failures.
set -uo pipefail
SRC="$HOME/fe/out"
DST="/mnt/c/Users/Devin Prater/AppData/Local/Temp/fesave/shots"
CONV="$HOME/open-game-access/scripts/ppm2png.py"
mkdir -p "$DST"

for p in "$SRC"/*.ppm; do
  [ -f "$p" ] || continue
  base="$(basename "$p" .ppm)"
  python3 "$CONV" "$p" "$DST/$base.png" 2>&1 | tail -1
done

echo
echo "== available PNGs =="
ls -la "$DST"/*.png 2>/dev/null | tail -20
