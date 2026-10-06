#!/usr/bin/env bash
# wsl.sh — the single entry point for anything that has to run inside WSL.
#
# The git checkout this file lives in (~/oga-work) is the only copy of the
# project: edit, build and commit here. Scripts run in place — there is no
# mirror to sync, so a script can never run against a stale copy.
#
#   wsl.sh build-core            rebuild the device core archive
#   wsl.sh build-sim             build the simulator core + Swift app
#   wsl.sh sim-test [frames]     host-side playthrough test with real script
#   wsl.sh xtool                 xtool build/run
#   wsl.sh <any script> [args]   any scripts/<name>.sh in the project
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/Vendor"

cd "$ROOT" || exit 1
export PATH=/usr/local/swift/bin:/usr/local/bin:$PATH

name="${1:-status}"
shift || true
if [ -f "$ROOT/scripts/$name.sh" ]; then
  exec bash "$ROOT/scripts/$name.sh" "$@"
elif [ -x "$ROOT/scripts/$name" ]; then
  exec "$ROOT/scripts/$name" "$@"
else
  echo "!! no such script: scripts/$name.sh" >&2
  exit 1
fi
