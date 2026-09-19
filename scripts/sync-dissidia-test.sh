#!/bin/bash
# sync dissidia adapter files into the canonical tree and build+run the host test.
set -eu
R=/home/devin/oga-work
W="/mnt/c/Users/Devin Prater/open-game-access"
cp -f "$W/Core/dissidia_adapter.cpp" "$R/Core/dissidia_adapter.cpp"
cp -f "$W/Core/dissidia_adapter_test.cpp" "$R/Core/dissidia_adapter_test.cpp"
cp -f "$W/Core/adapters.cpp" "$R/Core/adapters.cpp"
cp -f "$W/scripts/build-host.sh" "$R/scripts/build-host.sh"
cp -f "$W/scripts/core-sources.sh" "$R/scripts/core-sources.sh"
cp -f "$W/scripts/psp-find-widget.py" "$R/scripts/psp-find-widget.py" 2>/dev/null || true
cp -f "$W/scripts/psp-find-uiroot.py" "$R/scripts/psp-find-uiroot.py" 2>/dev/null || true
cp -f "$W/scripts/psp-find-uiroot2.py" "$R/scripts/psp-find-uiroot2.py" 2>/dev/null || true
cp -f "$W/scripts/psp-find-uiroot3.py" "$R/scripts/psp-find-uiroot3.py" 2>/dev/null || true
cp -f "$W/scripts/psp-find-uiroot4.py" "$R/scripts/psp-find-uiroot4.py" 2>/dev/null || true
echo "=== status ==="
git -C "$R" status --short
mkdir -p /home/devin/oga-test
echo "=== build ==="
g++ -O1 -g -fwrapv -fno-strict-aliasing -I"$R/Core" -I"$R/Sources/CPokeCore/include" -std=c++17 \
  -o /home/devin/oga-test/dissidia-test \
  "$R/Core/dissidia_adapter_test.cpp" \
  "$R/Core/dissidia_adapter.cpp" \
  "$R/Core/adapters.cpp"
echo "build rc=$?"
echo "=== run ==="
/home/devin/oga-test/dissidia-test
