#!/usr/bin/env bash
# lua-check.sh — syntax-check every Lua file we ship or shim.
#
# ⛔ DO NOT CHAIN THIS AS `luac -p f && echo OK || echo ERRORS`. `luac -p` writes its
# diagnostics to stderr and still exits 0 in some builds, so the chain reported
# "SYNTAX OK" on a file with a genuine syntax error. Capture output, test for it
# explicitly, and report per file.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

command -v luac >/dev/null 2>&1 || { echo "!! luac not found — scoop install lua" >&2; exit 2; }

fail=0
checked=0
while IFS= read -r f; do
  checked=$((checked + 1))
  out=$(luac -p "$f" 2>&1)
  if [ -n "$out" ]; then
    echo "  FAIL  $f"
    echo "$out" | sed 's/^/          /'
    fail=$((fail + 1))
  else
    echo "  ok    $f"
  fi
done < <(find . -name "*.lua" \
           -not -path "./Vendor/*" -not -path "./.git/*" \
           -not -path "*/xtool*" -not -path "*/node_modules/*" 2>/dev/null)

echo
echo "checked $checked file(s), $fail with syntax errors"
[ "$fail" -eq 0 ] || exit 1
