#!/usr/bin/env bash
# Run all standalone registered-adapter host tests.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for test_script in dbz-adapter-test.sh gba-adapter-test.sh dissidia-adapter-test.sh dq9-adapter-test.sh osk-echo-test.sh android-gb-bridge-test.sh; do
  printf '\n== %s ==\n' "$test_script"
  bash "$ROOT/scripts/$test_script"
done
