#!/usr/bin/env bash
# swift-check.sh — type-check the Swift targets (no device, no SDK link).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
cd "$ROOT" || exit 1
export POKECORE_LIB="$ROOT/Vendor/libpokecore.a"

echo "=== GameSession.swift: what core entry points does it use? ==="
grep -oE 'poke_[a-z_]+' Sources/OpenGameAccess/GameSession.swift | sort -u

echo
echo "=== InputBridge.swift ==="
grep -oE 'poke_[a-z_]+|POKE_[A-Z_]+' Sources/OpenGameAccess/InputBridge.swift | sort -u

echo
echo "=== SpeechEngine.swift ==="
grep -oE 'poke_[a-z_]+' Sources/OpenGameAccess/SpeechEngine.swift | sort -u

echo
echo "=== every poke_ symbol exported by the header ==="
grep -oE 'poke_[a-z_]+ *\(' Sources/CPokeCore/include/pokecore.h | sed 's/ *($//' | sort -u
