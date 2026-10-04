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
MGBA_REPO="${MGBA_REPO:-https://github.com/mgba-emu/mgba.git}"
# ⛔ PINNED TO THE HOST-PROVEN REVISION. The GBA core (Core/gba_core.cpp +
# scripts/core-sources.sh MGBA list) was proven against this exact commit with
# the audited HAVE_* defines in core-sources.sh; moving the pin means
# re-running the host proof (boot Emerald, Ready, byte-exact reads) before it
# ships. See Core/mgba_version_stub.cpp — its baked-in revision must match.
MGBA_REV="${MGBA_REV:-543a197582c30364584d773a974d7f991892fa43}"
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

# ---- mGBA (Game Boy Advance core) ----
# Fetched for headers + the fixed TU subset in core-sources.sh. No CMake, no
# configure: the build scripts compile the subset directly with the audited
# MGBA_DEFS and a generated flags.h (see build-core.sh).
fetch "$MGBA_REPO" mgba "$MGBA_REV"

# ---- PPSSPP (PlayStation Portable core) ----
# Fetched for headers + the audited IR-interpreter/software-GPU TU subset in
# core-sources.sh. No CMake: the build scripts compile the subset directly.
# PINNED TO THE HOST-PROVEN REVISION (see scripts/psp-host-proof.sh): the
# real Core/psp_core.cpp booted the Dissidia CSO against this exact commit.
# Moving the pin means re-running the host proof AND the subset test.
# ---- Mesen (NES). PLANNED, not yet integrated. ----
#
# ⛔ THE PIN IS MEASURED, NOT ASSUMED. Mesen2's NES core (Core/NES + Core/Shared)
# was compiled for aarch64-linux-android26 with the NDK's own clang BEFORE this
# pin was written: 84 of 84 TUs, zero failures. That is the same admission test
# mGBA passed, and it is why the registry lists the NES as PLANNED with a chosen
# core rather than as an aspiration.
#
# Mesen2 is chosen over FCEUmm/Nestopia because it is C++ with no Qt/SDL
# dependency at the Core level (Core/NES links against Core/Shared, not the UI),
# which is the property that made mGBA portable here and Dolphin not.
#
# ⛔ MOVING THIS PIN MEANS RE-RUNNING scripts/nes-feasibility.sh. Mesen's Console
# base class is not a stable ABI, and the source list below is generated from the
# tree, not hand-maintained.
MESEN_REPO="${MESEN_REPO:-https://github.com/SourMesen/Mesen2.git}"
# Measured: master at the time of writing; the NES core is complete and stable
# upstream, and this is the revision that passed the 84/84 compile.
MESEN_REV="${MESEN_REV:-b9fa69ddc6d0a331fb103fdb5eef6904305703c2}"
fetch "$MESEN_REPO" mesen "$MESEN_REV"

PPSSPP_REPO="${PPSSPP_REPO:-https://github.com/hrydgard/ppsspp.git}
PPSSPP_REV="${PPSSPP_REV:-f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad}"
fetch "$PPSSPP_REPO" ppsspp "$PPSSPP_REV"
# Only the submodules the audited subset compiles (see core-sources.sh).
# The rest (SDL, Qt, GLES, glslang, ...) are desktop/GPU frontends iOS
# cannot use; fetching them would pull hundreds of MB for nothing.
git -C ppsspp submodule update --init --depth 1 ext/armips ext/cpu_features ext/zstd ext/libchdr ext/aemu_postoffice ext/lua ext/miniupnp ffmpeg
# armips vendors its filesystem polyfill as a nested submodule.
git -C ppsspp/ext/armips submodule update --init --depth 1 ext/filesystem

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
  mgba         $( [ -d mgba ] && echo present || echo MISSING )
  ppsspp       $( [ -d ppsspp ] && echo present || echo MISSING )
  lua-$LUA_VER      $( [ -d "lua-$LUA_VER" ] && echo present || echo MISSING )

Next:
  ./wsl.sh build-core        # iOS device core archive
  ./wsl.sh build-sim-app     # iOS Simulator .app + zip

Apple SDK note: the iOS build additionally needs Apple's Darwin SDK, which is
Apple-licensed and NOT distributed here. Obtain it through your own Apple
Developer account and install it with \`xtool sdk install\` — see docs/building.md.
EOF
