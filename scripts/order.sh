#!/usr/bin/env bash
# order.sh — what call order does a working frontend use to start a ROM?
set -uo pipefail
echo "=== upstream melonDS Qt frontend: booting sequence ==="
grep -n 'SetupDirectBoot\|->Reset()\|SetNDSCart\|Start()' /tmp/up_NDS.cpp | head -20

echo
echo "=== upstream NDS::Reset: does it touch the GPU/LCD power? ==="
grep -n 'void NDS::Reset' -A 80 /tmp/up_NDS.cpp | grep -nE 'PowerControl9|GPU\.|SetPowerCnt|Reset' | head -20

echo
echo "=== FORK NDS::Reset: same region ==="
grep -n 'void NDS::Reset' -A 80 "$HOME/src/melonds-lua/src/NDS.cpp" | grep -nE 'PowerControl9|GPU\.|SetPowerCnt|Reset' | head -20

echo
echo "=== where PowerControl9 = 0x820F is set in each ==="
grep -n 'PowerControl9 = ' /tmp/up_NDS.cpp | head
grep -n 'PowerControl9 = ' "$HOME/src/melonds-lua/src/NDS.cpp" | head
