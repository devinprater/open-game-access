#!/usr/bin/env bash
# Mutation test: prove the adapter-sources gate fails when an adapter's source is dropped
# from OGA_GLUE, and passes when it is present. A gate that only ever passes is not a gate.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
SRC=scripts/core-sources.sh
cp "$SRC" /tmp/oga-core-sources.bak

python3 scripts/adapter-sources-check.py >/dev/null 2>&1
clean=$?
[ "$clean" -eq 0 ] || { echo "FAIL: clean tree does not pass"; cp /tmp/oga-core-sources.bak "$SRC"; exit 1; }

python3 - <<'PY'
import pathlib
p = pathlib.Path("scripts/core-sources.sh")
t = p.read_text()
t = t.replace("n64_adapter.cpp\n", "", 1)
p.write_text(t)
PY
python3 scripts/adapter-sources-check.py 2>&1 | head -n 3
sab=$?
cp /tmp/oga-core-sources.bak "$SRC"

if [ "$sab" -eq 0 ]; then
  echo "FAIL: removing n64_adapter.cpp from OGA_GLUE did not trip the gate"
  exit 1
fi
echo "PASS: adapter-sources gate catches a registered adapter missing from OGA_GLUE"
