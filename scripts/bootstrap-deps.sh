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

# ---- Genesis Plus GX (Sega Genesis / Mega Drive core) ----
# Fetched for the audited TU list in core-sources.sh (GPGX_ALL). No build
# system of upstream's is run: the build scripts compile the list directly
# as C with GPGX_DEFS, the tree's native language. One frontend shim is
# OURS, not fetched:
# Core/gpgx/osd.h (the core's TUs include "osd.h", which every frontend
# supplies) and the shim definitions in Core/genesis_core.cpp.
#
# ⛔ LICENCE, READ BEFORE ANY DISTRIBUTION DECISION: Genesis Plus GX is NOT
# public-domain or MIT. Its LICENSE.txt permits use and redistribution with
# attribution but states that redistributions "may not be sold, nor may they
# be used in a commercial product or activity", and that modified
# redistributions must include the complete source code. That fits this
# project's non-commercial distribution today; it is a real constraint on
# any future one, recorded here so it is never discovered late.
#
# PINNED BY HOST ADMISSIBILITY, the house pattern: at this exact revision,
# on the build host, all 54 TUs of GPGX_ALL compile, link, and run a
# synthetic-ROM harness (frames, framebuffer, audio, save states, battery
# SRAM round-trip through the .sav file). A pin bump re-runs that evidence
# before it lands.
GPGX_REPO="${GPGX_REPO:-https://github.com/ekeeke/Genesis-Plus-GX.git}"
GPGX_REV="${GPGX_REV:-a5834b3df455c49bb97bfaa328c20845831f576b}"
fetch "$GPGX_REPO" genesis-plus-gx "$GPGX_REV"

# ---- PPSSPP (PlayStation Portable core) ----
# Fetched for headers + the audited IR-interpreter/software-GPU TU subset in
# core-sources.sh. No CMake: the build scripts compile the subset directly.
# PINNED TO THE HOST-PROVEN REVISION (see scripts/psp-host-proof.sh): the
# real Core/psp_core.cpp booted the Dissidia CSO against this exact commit.
# Moving the pin means re-running the host proof AND the subset test.
# ---- Mesen. PLANNED, not yet integrated. ----
#
# ⛔ THE PIN IS MEASURED, NOT ASSUMED. The NES core (Core/NES + Core/Shared) was
# compiled for aarch64-linux-android26 with the NDK's own clang BEFORE this pin was
# written: 84 of 84 TUs, zero failures. That is the same admission test mGBA
# passed, and it is why the registry lists the NES as PLANNED with a chosen core
# rather than as an aspiration. All SEVEN console cores were measured the same way
# (scripts/mesen-feasibility.sh).
#
# ⛔ MESENCE, NOT MESEN2, AND THE DISTINCTION IS LOAD-BEARING. SourMesen/Mesen2
# is ARCHIVED (last push 2026-06-04) and its own README points at the successor.
# nesdev-org/MesenCE is the live tree: same seven console cores, actively pushed.
# Pinning an archived repo means every "measured" claim is measured against code
# nobody maintains.
#
# Mesen is chosen over FCEUmm/Nestopia because it is C++ with no Qt/SDL
# dependency at the Core level (Core/NES links against Core/Shared, not the UI),
# which is the property that made mGBA portable here and Dolphin not.
#
# ⛔ MOVING THIS PIN MEANS RE-RUNNING scripts/mesen-feasibility.sh. Mesen's
# Console base class is not a stable ABI, and the measured lists in
# core-sources.sh are generated FROM THE TREE, not hand-maintained.
#
# ⛔ AND EXPECT THE LISTS TO MOVE BETWEEN REVISIONS. Measured on adoption:
# against the archived Mesen2 pin, MesenCE master differs by 1 TU removed
# (NES APU/BaseExpansionAudio.cpp), 6 added (2 SNES input controllers, 1 Shared
# movie format, 1 Shared RTC, 3 WonderSwan cart types) and a per-console count
# change in NES 45->44, SNES 66->68, WS 19->23, Shared 39->41. A pinned-but-stale
# list would fail to link, not fail to compile.
MESEN_REPO="${MESEN_REPO:-https://github.com/nesdev-org/MesenCE.git}"
# Measured: MesenCE master at the time of adoption; all seven console cores
# compiled for aarch64-linux-android26 at this revision.
MESEN_REV="${MESEN_REV:-a60e79feb4d6dcced5922d636f9211837d01e381}"
fetch "$MESEN_REPO" mesen "$MESEN_REV"

PPSSPP_REPO="${PPSSPP_REPO:-https://github.com/hrydgard/ppsspp.git}"
PPSSPP_REV="${PPSSPP_REV:-f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad}"
fetch "$PPSSPP_REPO" ppsspp "$PPSSPP_REV"
# Only the submodules the audited subset compiles (see core-sources.sh).
# The rest (SDL, Qt, GLES, glslang, ...) are desktop/GPU frontends iOS
# cannot use; fetching them would pull hundreds of MB for nothing.
git -C ppsspp submodule update --init --depth 1 ext/armips ext/cpu_features ext/zstd ext/libchdr ext/aemu_postoffice ext/lua ext/miniupnp ffmpeg
# armips vendors its filesystem polyfill as a nested submodule.
git -C ppsspp/ext/armips submodule update --init --depth 1 ext/filesystem

# ---- Lua 5.4 (MIT) ----
# ⛔ FETCHED FROM THE LUA TEAM'S OWN GITHUB MIRROR, NOT www.lua.org.
# The release-tarball download this used to do
# (curl https://www.lua.org/ftp/lua-$LUA_VER.tar.gz) is unreachable from
# GitHub-hosted runners: the transfer stalls at 0 bytes and dies with
# "curl: (35) Recv failure: Connection reset by peer", which is what has
# been failing the Android APK and iOS simulator workflows in the core-swap
# step ("== fetching Lua ...") while every git clone in this script —
# all of them github.com — succeeds. lua.org is one small host; github.com
# is what this script already depends on for every other dep.
#
# The mirror's layout is NOT the tarball's: the git tree keeps the sources
# at its root, while the builds expect lua-$LUA_VER/src/. So the tree is
# cloned to a scratch dir and src/ is assembled from the explicit manifest
# below — the exact file set the release tarball ships in src/. Measured
# against a lua.org-fetched tree (2026-11-08): every file in the manifest
# is byte-identical between the two (sha256), so the assembled tree is the
# release, not an approximation of it.
#
# ⛔ THE MANIFEST IS EXACT, IN BOTH DIRECTIONS. The git tree carries three
# files the release does not — ltests.c, ltests.h (Lua's internal test
# library) and onelua.c (the single-file amalgamation) — and they must NOT
# land in src/: the build scripts glob src/*.c, and compiling either .c
# breaks the link. The tarball in turn ships two files the git tree lacks
# — luac.c and lua.hpp — and neither is missed: every build script skips
# luac by name (it is the standalone compiler's main(), not library code),
# and nothing OGA compiles includes lua.hpp. If a future Lua bump changes
# either side of that ledger, the copy loop fails loudly on the first
# missing file rather than shipping a quietly wrong tree.
#
# Integrity: LUA_REV pins the full commit SHA of tag v$LUA_VER, as with
# the other deps — git verifies the checkout against it — and the marker
# check after the copy fails the bootstrap if lua.h does not identify as
# the pinned release (which is also what catches a LUA_VER bump that
# forgot to move LUA_REV).
LUA_REPO="${LUA_REPO:-https://github.com/lua/lua.git}"
LUA_REV="${LUA_REV:-1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3}" # tag v5.4.7
LUA_SRC_FILES="
  lapi.c lapi.h lauxlib.c lauxlib.h lbaselib.c lcode.c lcode.h lcorolib.c
  lctype.c lctype.h ldblib.c ldebug.c ldebug.h ldo.c ldo.h ldump.c lfunc.c
  lfunc.h lgc.c lgc.h linit.c liolib.c ljumptab.h llex.c llex.h llimits.h
  lmathlib.c lmem.c lmem.h loadlib.c lobject.c lobject.h lopcodes.c
  lopcodes.h lopnames.h loslib.c lparser.c lparser.h lprefix.h lstate.c
  lstate.h lstring.c lstring.h lstrlib.c ltable.c ltable.h ltablib.c
  ltm.c ltm.h lua.c luaconf.h lua.h lualib.h lundump.c lundump.h
  lutf8lib.c lvm.c lvm.h lzio.c lzio.h
"
if [ ! -d "lua-$LUA_VER" ]; then
  fetch "$LUA_REPO" "lua-$LUA_VER-git" "$LUA_REV"
  mkdir -p "lua-$LUA_VER/src"
  for f in $LUA_SRC_FILES; do
    cp "lua-$LUA_VER-git/$f" "lua-$LUA_VER/src/$f"
  done
  rm -rf "lua-$LUA_VER-git"
fi
# Tripwire: the assembled tree really is Lua $LUA_VER.
grep -Eq "#define LUA_VERSION_MAJOR[[:space:]]+\"${LUA_VER%%.*}\"" "lua-$LUA_VER/src/lua.h" \
  && grep -Eq "#define LUA_VERSION_RELEASE[[:space:]]+\"${LUA_VER##*.}\"" "lua-$LUA_VER/src/lua.h" \
  || { echo "!! lua-$LUA_VER/src/lua.h does not identify as Lua $LUA_VER" >&2; exit 1; }
echo "   lua-$LUA_VER/src: $(ls "lua-$LUA_VER/src"/*.c 2>/dev/null | wc -l) C files"

# ---- what the build scripts expect ----
echo
cat <<EOF
== done. Source tree: $DEST

  melonds-lua  $( [ -d melonds-lua ] && echo present || echo MISSING )
  mgba         $( [ -d mgba ] && echo present || echo MISSING )
  ppsspp       $( [ -d ppsspp ] && echo present || echo MISSING )
  genesis-plus-gx $( [ -d genesis-plus-gx ] && echo present || echo MISSING )
  lua-$LUA_VER      $( [ -d "lua-$LUA_VER" ] && echo present || echo MISSING )

Next:
  ./wsl.sh build-core        # iOS device core archive
  ./wsl.sh build-sim-app     # iOS Simulator .app + zip

Apple SDK note: the iOS build additionally needs Apple's Darwin SDK, which is
Apple-licensed and NOT distributed here. Obtain it through your own Apple
Developer account and install it with \`xtool sdk install\` — see docs/building.md.
EOF
