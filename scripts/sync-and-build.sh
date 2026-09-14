#!/usr/bin/env bash
# sync-and-build.sh — copy the project from the Windows side into WSL and build
# the core. The Windows copy stays the source of truth (it is where the user
# edits and where the Swift package lives); WSL gets a mirror so the ~120-file
# C++ build runs on ext4 instead of across the 9p mount.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

WIN_SRC="/mnt/c/Users/Devin Prater/open-game-access"
WSL_DST="$ROOT"

mkdir -p "$WSL_DST"
# Mirror sources; keep WSL-side build outputs (Vendor/obj, Vendor/*.a, .build).
rsync -a --delete \
  --exclude '.build/' \
  --exclude 'Vendor/obj/' \
  --exclude 'Vendor/*.a' \
  --exclude 'xtool/' \
  "$WIN_SRC/" "$WSL_DST/"

cd "$WSL_DST"
exec bash scripts/build-core.sh "$@"
