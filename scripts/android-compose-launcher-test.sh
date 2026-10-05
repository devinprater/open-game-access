#!/usr/bin/env bash
# android-compose-launcher-test.sh — the Compose launcher's contract with the
# native reader, plus the mutation check that proves the rules can fail.
#
# WHY THE .sh WRAPPER EXISTS. adapter-tests.sh invokes every entry in its list as
# `bash scripts/<name>`, so a .py there silently does nothing -- which is how a
# check stops running without anyone noticing. The python file holds the rules; this
# wrapper runs it and then the sabotage pass.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

python3 scripts/android-compose-launcher-test.py "$ROOT" || exit 1

if [ -f scripts/android-compose-launcher-negative-test.sh ]; then
  bash scripts/android-compose-launcher-negative-test.sh || exit 1
fi
