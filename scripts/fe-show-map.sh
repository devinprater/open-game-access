#!/usr/bin/env bash
# fe-show-map.sh — print the map-state and units sections of a feterrain2 dump.
set -uo pipefail
F="${1:-$HOME/fe/out/terrain2-5900.txt}"
[ -f "$F" ] || { echo "!! no dump at $F" >&2; ls -la "$HOME/fe/out/" 2>/dev/null | grep terrain; exit 2; }
echo "### map state ###"
awk '/---- map state/{f=1} /---- units/{f=0} f' "$F"
echo
echo "### units ###"
awk '/---- units/{f=1} f' "$F"
