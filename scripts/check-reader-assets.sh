#!/bin/bash
# check-reader-assets.sh — the Android GB/GBA reader set must be the CURRENT set.
#
# The Android app ships the reader Lua from app/src/main/assets/lua/gb/;
# the canonical set lives in Sources/OpenGameAccess/Resources/gba-lua/
# (iOS copies that whole directory via Package.swift). The two trees have
# drifted before: a 2026-09-15 Android snapshot shipped for weeks without
# oga_bootstrap.lua / mgba_compat.lua (added 2026-09-20), and the old gate
# (`grep -qi lua` over the APK) could not see it. The APK gates in
# .github/workflows/android-apk.yml and scripts/android-build-local.sh now
# assert the files by name; this script is the source-tree half of the same
# check, run by the workflow before the build.
#
# NOTE (2026-10-22): TODO.md records a build-time STAGING design for this
# (2026-10-07, 4870f45) — stage from gba-lua, no in-tree copy. That staging
# and this script were both absent from main@7567ac3 while the workflow
# still referenced the script, so the workflow failed before building.
# Until the staging is restored, the in-tree copy is what ships and this
# check keeps it honest: every runtime .lua in gba-lua must exist in the
# Android assets with identical content. host-sim-rom.lua is a host test
# harness, not a runtime file, and is excluded; the Android-only sounds/
# directory is not compared.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
DST="$ROOT/app/src/main/assets/lua/gb"
fail=0
for f in "$SRC"/*.lua; do
  b="$(basename "$f")"
  [ "$b" = "host-sim-rom.lua" ] && continue
  if [ ! -f "$DST/$b" ]; then
    echo "!! $b is in the reader set but missing from app/src/main/assets/lua/gb/" >&2
    fail=1
  elif ! cmp -s "$f" "$DST/$b"; then
    echo "!! $b differs between Sources/.../gba-lua and app/src/main/assets/lua/gb/" >&2
    fail=1
  fi
done
[ "$fail" -eq 0 ] && echo "ok: the Android GB/GBA reader assets match the current reader set"
exit "$fail"
