#!/usr/bin/env bash
# bootstrap-deps.sh — fetch the emulator core and Lua into a build tree.
#
# ⛔ THE CORE IS NOT VENDORED IN THIS REPOSITORY, ON PURPOSE. melonDS is a
# separate GPL-3.0 project with its own repository; copying it in would (a) bloat
# the repo by hundreds of MB, (b) invite silent drift from upstream, and (c) muddy
# which licence covers which file. This script fetches the exact revisions the
# published builds used.
#
# Usage:  ./scripts/bootstrap-deps.sh [dest]
# Default dest: ~/src   (the path the build scripts expect)
set -euo pipefail

DEST="${1:-$HOME/src}"
mkdir -p "$DEST"
cd "$DEST"

# Pinned revisions. `main`/`master` would make every build a moving target and
# make a published binary impossible to reproduce; pin and bump deliberately.
MELONDS_LUA_REPO="${MELONDS_LUA_REPO:-https://github.com/NPO-197/melonDS-lua.git}"
LUA_VER="${LUA_VER:-5.4.7}"

fetch() { # fetch <url> <dir> <ref>
  local url="$1" dir="$2" ref="${3:-}"
  if [ -d "$dir/.git" ]; then
    echo "== $dir already present, updating"
    git -C "$dir" fetch --all --tags --quiet || true
  else
    echo "== cloning $url -> $dir"
    git clone --quiet "$url" "$dir"
  fi
  if [ -n "$ref" ]; then
    echo "== checking out $ref"
    git -C "$dir" checkout --quiet "$ref"
  fi
  echo "   HEAD: $(git -C "$dir" rev-parse --short HEAD 2>/dev/null || echo unknown)"
}

# ---- melonDS with the Lua accessibility scripting fork ----
# The fork's whole reason for existing is accessibility scripting ("The aim ... is
# to support accessibility features and trackers"). Upstream melonDS has no Lua.
fetch "$MELONDS_LUA_REPO" melonds-lua master

# ---- Lua 5.4 (MIT) ----
if [ ! -d "lua-$LUA_VER" ]; then
  echo "== fetching Lua $LUA_VER"
  curl -fL -o "lua-$LUA_VER.tar.gz" \
    "https://www.lua.org/ftp/lua-$LUA_VER.tar.gz"
  tar xzf "lua-$LUA_VER.tar.gz"
  rm -f "lua-$LUA_VER.tar.gz"
fi
echo "   lua-$LUA_VER/src: $(ls "lua-$LUA_VER/src"/*.c 2>/dev/null | wc -l) C files"

# ---- what the build scripts expect ----
echo
cat <<EOF
== done. Source tree: $DEST

  melonds-lua  $( [ -d melonds-lua ] && echo present || echo MISSING )
  lua-$LUA_VER      $( [ -d "lua-$LUA_VER" ] && echo present || echo MISSING )

Next:
  ./wsl.sh build-core        # iOS device core archive
  ./wsl.sh build-sim-app     # iOS Simulator .app + zip

Apple SDK note: the iOS build additionally needs Apple's Darwin SDK, which is
Apple-licensed and NOT distributed here. Obtain it through your own Apple
Developer account and install it with \`xtool sdk install\` — see docs/building.md.
EOF
