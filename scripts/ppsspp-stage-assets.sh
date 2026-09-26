#!/usr/bin/env bash
# ppsspp-stage-assets.sh — copy the audited PPSSPP runtime-asset subset into a
# staging directory for app packaging.
#
# The manifest (PPSPP_ASSETS in scripts/core-sources.sh) is the strace-proven
# boot set: config tables, the soft-GPU debug font atlas, and the VFPU LUTs
# the IR interpreter consults. Shaders, UI langs, and the debugger are
# excluded (unused by this embedding); flash0 is excluded because it is
# Sony IP that PPSSPP auto-installs from the game disc on first boot.
#
# Usage: ppsspp-stage-assets.sh [ppsspp-src] [dest-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
SRC="${1:-${PPSPP_SRC:-$HOME/src/ppsspp}}/assets"
DST="${2:-$ROOT/build-assets/ppsspp-assets}"
[ -d "$SRC" ] || { echo "!! no PPSSPP assets at $SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
mkdir -p "$DST"
n=0
for f in $PPSPP_ASSETS; do
  [ -f "$SRC/$f" ] || { echo "!! manifest names 'assets/$f' but it is missing from $SRC" >&2; exit 1; }
  mkdir -p "$DST/$(dirname "$f")"
  cp "$SRC/$f" "$DST/$f"
  n=$((n + 1))
done
echo "== staged $n PPSSPP runtime assets -> $DST"
