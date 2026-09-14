#!/usr/bin/env bash
set -uo pipefail
cd "$HOME/open-game-access" || exit 1
echo "=== every Lua file that would be published (path / bytes / sha256 prefix) ==="
find . -name '*.lua' -print0 | while IFS= read -r -d '' f; do
  printf '%-72s %9d  %s\n' "$f" "$(wc -c < "$f")" "$(sha256sum "$f" | cut -c1-16)"
done
echo
echo "=== is there a GBA/GB script set too? ==="
ls -la app/src/main/assets/lua/ 2>/dev/null | head
ls app/src/main/assets/lua/gb 2>/dev/null | head -5
echo
echo "=== main.lua provenance header ==="
head -6 Sources/OpenGameAccess/Resources/main.lua
