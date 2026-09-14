#!/usr/bin/env bash
# verify-portable.sh — syntax-check every script and prove the portable build path
# still works on this (Linux/WSL) machine.
#
# A portability fix that breaks the working environment is worse than no fix, so
# this runs the real build after the rewrite rather than only checking syntax.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 1

echo "== bash syntax check on every script =="
bad=0
for f in scripts/*.sh; do
  if ! bash -n "$f" 2>/tmp/synerr; then
    echo "SYNTAX FAIL: $f"; head -3 /tmp/synerr; bad=1
  fi
done
[ "$bad" -eq 0 ] && echo "  all scripts parse"
echo

echo "== host build still works =="
bash scripts/build-host.sh 2>&1 | tail -3
echo

echo "== simulator core build still works =="
bash scripts/build-sim.sh 2>&1 | tail -6
echo

echo "== device core build still works =="
bash scripts/build-core.sh 2>&1 | tail -4
