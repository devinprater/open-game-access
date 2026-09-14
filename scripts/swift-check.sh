#!/usr/bin/env bash
# swift-check.sh — type-check the Swift targets (no device, no SDK link).
set -uo pipefail
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin
cd "$HOME/pokemon-access-ios" || exit 1
export POKECORE_LIB="$HOME/pokemon-access-ios/Vendor/libpokecore.a"

echo "=== GameSession.swift: what core entry points does it use? ==="
grep -oE 'poke_[a-z_]+' Sources/PokemonAccess/GameSession.swift | sort -u

echo
echo "=== InputBridge.swift ==="
grep -oE 'poke_[a-z_]+|POKE_[A-Z_]+' Sources/PokemonAccess/InputBridge.swift | sort -u

echo
echo "=== SpeechEngine.swift ==="
grep -oE 'poke_[a-z_]+' Sources/PokemonAccess/SpeechEngine.swift | sort -u

echo
echo "=== every poke_ symbol exported by the header ==="
grep -oE 'poke_[a-z_]+ *\(' Sources/CPokeCore/include/pokecore.h | sed 's/ *($//' | sort -u
