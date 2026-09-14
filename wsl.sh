#!/usr/bin/env bash
# wsl.sh — the single entry point for anything that has to run inside WSL.
#
# Windows stays the source of truth (that is where the project is edited); this
# mirrors it into WSL first, so a script never runs against a stale copy. Build
# outputs are preserved so the ~120-file core build is not redone every call.
#
#   wsl.sh build-core            rebuild the device core archive
#   wsl.sh build-sim             build the simulator core + Swift app
#   wsl.sh sim-test [frames]     host-side playthrough test with real script
#   wsl.sh xtool                 xtool build/run
#   wsl.sh <any script> [args]   any scripts/<name>.sh in the project
set -uo pipefail

WIN_SRC="/mnt/c/Users/Devin Prater/pokemon-access-ios"
WSL_DST="$HOME/pokemon-access-ios"

mkdir -p "$WSL_DST"
# Vendor/ and xtool-sim/ are WSL-side build outputs: mirroring them (or letting
# --delete remove them) costs a full ~120-file core rebuild and throws away the
# packaged simulator app. Windows is the source of truth for SOURCES only.
rsync -a --delete \
  --exclude '.build/' \
  --exclude 'xtool/' \
  --exclude 'xtool-sim/' \
  --exclude 'Vendor/' \
  "$WIN_SRC/" "$WSL_DST/"

mkdir -p "$WSL_DST/Vendor"

cd "$WSL_DST" || exit 1
export PATH=/usr/local/swift/bin:/usr/local/bin:/usr/bin:/bin

name="${1:-status}"
shift || true
if [ -f "$WSL_DST/scripts/$name.sh" ]; then
  exec bash "$WSL_DST/scripts/$name.sh" "$@"
elif [ -x "$WSL_DST/scripts/$name" ]; then
  exec "$WSL_DST/scripts/$name" "$@"
else
  echo "!! no such script: scripts/$name.sh" >&2
  exit 1
fi
