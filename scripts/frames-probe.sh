#!/usr/bin/env bash
# frames-probe.sh — isolate emulator cost from script cost.
#
# poke_start() requires a script, so "no script" is expressed as the compat shim
# plus a one-line loop that just yields every frame: the emulator does all its
# work and Lua gets resumed exactly as it would be, but main.lua's per-frame RAM
# reading never happens. Comparing that against the full script is what tells a
# slow emulator from a slow script.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ROM="$HOME/hosttest-data/black.nds"
SHIM="Sources/PokemonAccess/Resources/bizhawk_compat.lua"
DATA="$HOME/hosttest-data"

{ cat "$SHIM"; printf '\nwhile true do\n  emu.frameadvance()\nend\n'; } > "$DATA/noop.lua"
echo "noop.lua: $(wc -l < "$DATA/noop.lua") lines"

for SPEC in "noop:$DATA/noop.lua" "full:$DATA/script.lua"; do
  TAG="${SPEC%%:*}"; SCRIPT="${SPEC#*:}"
  for N in 30 300; do
    echo "=== $TAG / $N frames ==="
    timeout 120 ./Vendor/timingtest "$ROM" "$SCRIPT" "$N" > "$HOME/probe-$TAG-$N.log" 2>&1
    echo "exit=$? (124=timeout)"
    grep -E 'frames in|stopped|took|SPEAK' "$HOME/probe-$TAG-$N.log" | tail -4
    echo
  done
done
