#!/usr/bin/env bash
# scan-roms.sh — feasibility screening for every NDS game in the folder.
#
# Boots each ROM headlessly, measures whether it runs and renders and whether its
# text is plain data in RAM, and appends one JSON line per game to
# $ROOT/rom-screen/scan.jsonl. Text-in-RAM is the single biggest predictor of how
# expensive an accessibility reader will be: a game whose dialogue is ASCII in
# RAM is a reader away; a game that rasterises its own font in glyph tiles is a
# font-decoding project first.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
OUTDIR="$ROOT/rom-screen"
FRAMES="${FRAMES:-6000}"
mkdir -p "$OUTDIR" "$HOME/roms"

bash "$ROOT/scripts/build-host.sh" || exit 1

g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/screen Core/screen.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -10
[ -x Vendor/screen ] || { echo "!! screen did not link"; exit 1; }

WIN="/mnt/c/Users/Devin Prater/Dropbox/Games/NDS"
: > "$OUTDIR/scan.jsonl"

echo "== screening $FRAMES frames/ROM =="
i=0
for f in "$WIN"/*.nds; do
  base="$(basename "$f")"
  i=$((i+1))
  # 9p is slow for a 256 MB ROM; copy to ext4 once.
  local="$HOME/roms/$base"
  [ -f "$local" ] || cp "$f" "$local"
  printf '%-58s ' "$base"
  line=$(PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua" \
         timeout 900 ./Vendor/screen "$local" "$FRAMES" 2>/dev/null | tail -1)
  if [ -z "$line" ]; then line='{"error":"no output"}'; fi
  printf '%s\n' "$line" | head -c 400
  echo
  python3 - "$base" "$line" <<'PY' >> "$OUTDIR/scan.jsonl"
import json, sys
name, line = sys.argv[1], sys.argv[2]
try:
    d = json.loads(line)
except Exception:
    d = {"error": "unparseable"}
d["rom"] = name
print(json.dumps(d))
PY
done
echo
echo "== scanned $i ROMs -> $OUTDIR/scan.jsonl"
