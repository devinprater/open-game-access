#!/usr/bin/env bash
# swift-check.sh — REAL checks on the Swift layer, each one able to fail.
#
# ⛔ WHAT THIS REPLACED, AND WHY IT MATTERS. The old version of this file said
# "type-check the Swift targets (no device, no SDK link)" and then ran four greps. With
# `set -uo pipefail` (no -e) and no failure path it could never fail, and nothing called
# it — a claimed gate that gated nothing. That is the exact class this repo keeps hitting
# (a gate that runs here is not a gate that runs there; a list that silently omits a file).
#
# WHAT IT CHECKS NOW:
#   1. a real `swiftc -typecheck` over the files Swift-on-Linux can actually compile;
#   2. the C ABI mirror invariant — Core/adapter.h's Command enum vs AdapterCommand.swift
#      (the raw values ARE the ABI, and a drift silently rejects a new command);
#   3. every poke_ symbol Swift calls is declared in the C header.
#
# ⛔ AND IT NAMES WHAT IT CANNOT CHECK. Files importing UIKit, SwiftUI, AVFoundation or
# GameController cannot be type-checked on Linux. They are listed as SKIPPED, loudly —
# a skipped file is not a verified file, and the script must not imply otherwise.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# swiftc is not on a stock CI runner's PATH and not in the same place on every host, so
# find it rather than hard-coding one layout.
if ! command -v swiftc >/dev/null 2>&1; then
  for d in /usr/local/swift/usr/bin /usr/local/swift/bin /opt/swift/usr/bin \
           /usr/share/swift/usr/bin "$HOME/.local/swift/usr/bin"; do
    if [ -x "$d/swiftc" ]; then PATH="$d:$PATH"; export PATH; break; fi
  done
fi

fail=0

echo "=== 1. the C ABI mirror: C++ Command vs the Swift copy"
python3 scripts/abi-mirror-check.py || fail=1

echo
echo "=== 2. every poke_ symbol Swift calls is declared in the header"
python3 scripts/swift-check.py poke-symbols || fail=1

echo
echo "=== 3. real swiftc type-check (Linux-reachable files)"
if command -v swiftc >/dev/null 2>&1; then
  python3 scripts/swift-check.py typecheck || fail=1
else
  # ⛔ NOT a pass. Reported so a green run cannot be mistaken for a verified Swift tree.
  echo "   UNRUN: no swiftc on this host, so nothing was type-checked."
  echo "   ⛔ The ABI mirror and symbol checks above DID run; the type-check did not."
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "swift-check: FAILED"
  exit 1
fi
echo "swift-check: PASSED (skipped files are named above — they are NOT verified)"
