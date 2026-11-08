#!/usr/bin/env bash
# android-apply-overlay.sh — install this repo's Android overlay onto the cloned
# melonDS-android frontend, and force the emulator core to use its JIT.
#
# Replaces the old blind `cp -r app/src/main/. frontend/app/src/main/`, which
# OVERWROTE upstream files instead of adding to them. That clobber is why the
# build failed: our res/values/strings.xml (1 entry) replaced upstream's (475),
# so ~50 R.string.* lookups in unmodified upstream Kotlin stopped resolving, and
# our AndroidManifest.xml (2 activities) replaced upstream's (7.6 KB of them).
#
# Every edit below is idempotent: re-running changes nothing.
#
# Usage: scripts/android-apply-overlay.sh <frontend-dir> [repo-root]
set -euo pipefail

FRONTEND="${1:?usage: android-apply-overlay.sh <frontend-dir> [repo-root]}"
ROOT="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
OVERLAY="$ROOT/app/native-overlay"
MAIN="$ROOT/app/src/main"

[ -d "$FRONTEND" ] || { echo "!! no frontend at $FRONTEND" >&2; exit 1; }

say() { printf '== %s\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Our C++ glue and Kotlin accessibility classes. These are NEW files
#    (upstream has no such names), so a plain copy is safe.
# ---------------------------------------------------------------------------
say "copying the overlay sources (new files only)"
cp -r "$OVERLAY/." "$FRONTEND/"
mkdir -p "$FRONTEND/app/src/main/java/me/magnum/melonds/accessibility"
cp -r "$OVERLAY/app/src/main/java/me/magnum/melonds/accessibility/." \
      "$FRONTEND/app/src/main/java/me/magnum/melonds/accessibility/" 2>/dev/null || true
cp -r "$MAIN/java/com" "$FRONTEND/app/src/main/java/" 2>/dev/null || true
cp -r "$MAIN/assets/." "$FRONTEND/app/src/main/assets/" 2>/dev/null || true
cp -r "$MAIN/cpp/." "$FRONTEND/app/src/main/cpp/" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 1b. STAGE the Game Boy / GBA reader set from its SINGLE source of truth.
#
# ⛔ THIS USED TO BE A TRACKED DUPLICATE, AND IT DRIFTED. app/src/main/assets/
# lua/gb/ was a hand-copied second tree (197 files); by the time anyone looked it
# was five days stale and missing oga_bootstrap.lua and mgba_compat.lua — the
# loader and the mGBA API shim — so the GBA path shipped to Android without the
# two files that make it work, while its Kotlin side pointed straight at it. iOS
# was unaffected only because Package.swift .copy()s the whole canonical
# directory.
#
# Generating it here means a reader fix is applied ONCE. If you are tempted to
# add a file under app/src/main/assets/lua/gb/, put it in
# Sources/OpenGameAccess/Resources/gba-lua/ instead — the gate fails on a
# missing reader file, and this step overwrites that directory anyway.
# ---------------------------------------------------------------------------
say "staging the Game Boy / GBA reader set from its canonical tree"
READERS="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
[ -d "$READERS" ] || { echo "!! no reader set at $READERS" >&2; exit 1; }
[ -f "$READERS/oga_bootstrap.lua" ] || {
  echo "!! the canonical reader set has no oga_bootstrap.lua -- refusing to stage a broken set" >&2
  exit 1; }
[ -f "$READERS/mgba_compat.lua" ] || {
  echo "!! the canonical reader set has no mgba_compat.lua -- refusing to stage a broken set" >&2
  exit 1; }

GB_DST="$FRONTEND/app/src/main/assets/lua/gb"
rm -rf "$GB_DST"
mkdir -p "$GB_DST"
cp -r "$READERS/." "$GB_DST/"
# ⛔ HOST DEV TOOLS MUST NOT SHIP. host-sim-rom.lua asserts its own loadfile
# paths, and oga_capture.lua dofile()s a path under a developer's Dropbox and
# writes to hardcoded Windows Temp paths. Neither is reader runtime.
rm -f "$GB_DST/host-sim-rom.lua" "$GB_DST/oga_capture.lua"
echo "     staged $(find "$GB_DST" -type f | wc -l) files (sounds: $(find "$GB_DST/sounds" -type f 2>/dev/null | wc -l))"

# ---------------------------------------------------------------------------
# 1c. STAGE the NES reader sets from their SINGLE source of truth.
#
# The same rule as 1b, for the same reason: the NES readers (Dragon
# Warrior Access, Zelda 1 Access) live canonically in
# Sources/OpenGameAccess/Resources/nes-lua/ -- one directory per set,
# each with its oga_nes_reader.lua entry -- and iOS copies that whole
# tree into its bundle (Package.swift). A second tracked copy here
# would drift the way the GB one did. The sets are gated the way
# scripts/nes-reader-assets-test.sh gates them: every set directory
# must carry its reader entry, and there must be at least the two
# shipped sets. The Android session stages a WRITABLE copy of the set
# the core names at load (the readers write into their own tree); the
# APK only ever carries this read-only staging.
# ---------------------------------------------------------------------------
say "staging the NES reader sets from their canonical tree"
NES_READERS="$ROOT/Sources/OpenGameAccess/Resources/nes-lua"
[ -d "$NES_READERS" ] || { echo "!! no NES reader sets at $NES_READERS" >&2; exit 1; }
NES_SET_COUNT=0
for setdir in "$NES_READERS"/*/; do
  [ -f "$setdir/oga_nes_reader.lua" ] || {
    echo "!! NES reader set $setdir has no oga_nes_reader.lua -- refusing to stage a broken set" >&2
    exit 1; }
  NES_SET_COUNT=$((NES_SET_COUNT+1))
done
[ "$NES_SET_COUNT" -ge 2 ] || {
  echo "!! only $NES_SET_COUNT NES reader set(s) staged -- the shipped readers are missing" >&2
  exit 1; }

NES_DST="$FRONTEND/app/src/main/assets/nes-lua"
rm -rf "$NES_DST"
mkdir -p "$NES_DST"
cp -r "$NES_READERS/." "$NES_DST/"
echo "     staged $NES_SET_COUNT NES reader sets ($(find "$NES_DST" -type f | wc -l) files)"

# ---------------------------------------------------------------------------
# 2. Resources: our app label gets its OWN key. Upstream already defines
#    `app_name` for its own product, and overwriting it would break upstream
#    code -- which is exactly the class of bug being fixed here.
# ---------------------------------------------------------------------------
say "adding our own label key (app_name belongs to upstream)"
python3 - "$FRONTEND" <<'PY'
import sys, os
r = os.path.join(sys.argv[1], "app/src/main/res/values/strings.xml")
if not os.path.exists(r):
    print("   (no upstream strings.xml; nothing to add)"); sys.exit(0)
t = open(r, encoding="utf-8").read()
if 'name="oga_app_name"' in t:
    print("   oga_app_name already present")
else:
    t = t.replace("</resources>",
                  '    <string name="oga_app_name">Open Game Access</string>\n</resources>')
    open(r, "w", encoding="utf-8").write(t)
    print("   added oga_app_name")
PY

# ---------------------------------------------------------------------------
# 3. Manifest: APPEND our activity to upstream's application block.
#    Replacing the manifest deleted every upstream activity.
# ---------------------------------------------------------------------------
say "adding our launcher activity to the upstream manifest"
python3 - "$FRONTEND" <<'PY'
import sys, os
m = os.path.join(sys.argv[1], "app/src/main/AndroidManifest.xml")
s = open(m, encoding="utf-8").read()
if "com.devin.opengameaccess.MainActivity" in s:
    print("   already present; nothing added"); sys.exit(0)
activity = '''
        <!-- Open Game Access: the WebView host that runs the accessibility Lua
             scripts. ADDED to the upstream manifest, not replacing it. -->
        <activity
            android:name="com.devin.opengameaccess.MainActivity"
            android:exported="true"
            android:label="@string/oga_app_name"
            android:configChanges="orientation|screenSize|keyboardHidden|screenLayout|smallestScreenSize"
            android:launchMode="singleTask">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
'''
i = s.rindex("</application>")
open(m, "w", encoding="utf-8").write(s[:i] + activity + s[i:])
print("   inserted our activity before </application>")
PY

# ---------------------------------------------------------------------------
# 4. The core swap breaks the network CMake file. The Lua fork's
#    src/net/CMakeLists.txt calls pkg_check_modules(ENet ...) unconditionally,
#    and neither tree ever calls find_package(PkgConfig) -- hence
#    'Unknown CMake command "pkg_check_modules"'. Upstream's own Android file
#    comments that call out and links the `enet` target the frontend already
#    builds, so do the same.
# ---------------------------------------------------------------------------
say "fixing the swapped-in core's net/CMakeLists.txt (pkg_check_modules)"
NET="$FRONTEND/melonDS-android-lib/src/net/CMakeLists.txt"
if [ -f "$NET" ] && grep -qE '^[[:space:]]*pkg_check_modules\(ENet' "$NET"; then
  python3 - "$NET" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """    pkg_check_modules(ENet REQUIRED IMPORTED_TARGET libenet)
    fix_interface_includes(PkgConfig::ENet)
    target_link_libraries(net-utils PUBLIC PkgConfig::ENet)"""
new = """    # The Android frontend builds enet itself as a subdirectory target, and
    # neither tree calls find_package(PkgConfig), so upstream's Android fork
    # comments this out and links the target directly. Same fix here.
    target_link_libraries(net-utils PUBLIC enet)"""
if old in s:
    open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))
    print("   replaced the pkg_check_modules(ENet ...) block")
else:
    print("   (pattern not found; left as is)")
PY
else
  echo "   (no uncommented pkg_check_modules(ENet ...) -- nothing to fix)"
fi

# ---------------------------------------------------------------------------
# 5. JIT. melonDS decides ENABLE_JIT with a cmake_dependent_option keyed on an
#    ARCHITECTURE value that detect_architecture() derives from a compiler macro
#    via check_symbol_exists. That probe is NOT reliable when cross-compiling, so
#    pass the architecture explicitly and require the JIT.
#    Measured on melonDS's own option: ARCHITECTURE=ARM64 -> ENABLE_JIT=ON with
#    backend ARMJIT_A64; ARCHITECTURE=ARM -> ENABLE_JIT=OFF (correct -- 32-bit
#    ARM has no A64 backend and must stay on the interpreter).
# ---------------------------------------------------------------------------
say "forcing the core's JIT on and its GL renderer off (in CMake, not Gradle)"
CMAKE="$FRONTEND/app/CMakeLists.txt"
if grep -q 'OGA_BUILD_SETTINGS' "$CMAKE"; then
  echo "   already set; nothing to do"
else
  python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

# Must run BEFORE add_subdirectory of the core: these are cache options the core
# reads when it configures.
anchor = "add_subdirectory(${CORE-LIB} ./melonDS-android-lib)"
assert anchor in s, "core add_subdirectory anchor not found"

block = """# ---------------------------------------------------------------------------
# OGA_BUILD_SETTINGS -- Open Game Access build settings.
#
# Set here rather than in Gradle because Gradle's Kotlin DSL does not accept an
# `arguments` list in the top-level externalNativeBuild block (it fails with
# "Unresolved reference 'arguments'"), and because CMAKE_ANDROID_ARCH_ABI is only
# known once CMake is configuring. Deriving ARCHITECTURE from it removes a whole
# class of silent mismatch between a Gradle property and the actual compiler.
# ---------------------------------------------------------------------------

# OUR CORE'S OPENGL RENDERER IS DESKTOP GL, NOT GLES. Its GL files differ from the
# shell core's by 2071 lines (GPU_OpenGL.cpp alone: 1017) and its shaders carry no
# "#version 320 es", so on Android it calls what GLES3 does not have (glClearDepth,
# glDepthRange, glDrawBuffer, glMapBuffer, glBindFragDataLocation,
# GL_UNSIGNED_SHORT_1_5_5_5_REV). Porting that GL is a project of its own; switch
# the renderer off instead. GPU_Soft.cpp / GPU2D_Soft.cpp / GPU3D_Soft.cpp are NOT
# behind this option and build unconditionally, so games still run.
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)

# JIT: on for 64-bit ABIs only. ARCHITECTURE must match what the compiler really
# is, because the core uses it both to pick the JIT sources (ARMJIT_x64 vs
# ARMJIT_A64) and to decide ENABLE_JIT -- and ARMJIT_Compiler.h hard-errors with
# "The current target platform doesn't have a JIT backend" if they disagree.
# Derive it from the ABI CMake already knows instead of trusting a Gradle
# property.
if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(ARCHITECTURE "ARM64" CACHE STRING "" FORCE)
    set(ENABLE_JIT ON CACHE BOOL "" FORCE)
elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(ARCHITECTURE "x86_64" CACHE STRING "" FORCE)
    set(ENABLE_JIT ON CACHE BOOL "" FORCE)
else()
    # 32-bit ARM has no A64 backend in this core; the interpreter is correct.
    set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
endif ()

# Release, not Debug: the native code was being built with no optimisation at all.
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

"""

s = s.replace(anchor, block + anchor, 1)
open(p, "w", encoding="utf-8").write(s)
print("   CMAKE_BUILD_TYPE=Release, ENABLE_JIT (per ABI), ARCHITECTURE, ENABLE_OGLRENDERER=OFF")
PY
fi

# ---------------------------------------------------------------------------
# 6. androidx.webkit: our MainActivity uses WebViewAssetLoader, which upstream
#    does not depend on. Without it the Kotlin step fails with
#    "Unresolved reference 'webkit'".
# ---------------------------------------------------------------------------
GRADLE="$FRONTEND/app/build.gradle.kts"
say "adding the androidx.webkit dependency"
if grep -q 'androidx.webkit' "$GRADLE"; then
  echo "   already present; nothing to do"
else
  python3 - "$GRADLE" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
m = re.search(r'^dependencies \{\n', s, re.M)
if not m:
    print("   !! no dependencies block found"); sys.exit(1)
ins = ('    // Open Game Access: MainActivity serves assets over\n'
       '    // https://appassets.androidplatform.net with WebViewAssetLoader.\n'
       '    implementation("androidx.webkit:webkit:1.11.0")\n')
open(p, "w", encoding="utf-8").write(s[:m.end()] + ins + s[m.end():])
print("   added androidx.webkit")
PY
fi

# ---------------------------------------------------------------------------
# 7. The GBA/Game Boy native path.
#
# ⛔ THIS SECTION FIXES A REAL OMISSION. The overlay has always copied
# MGBACore.cpp / MGBARunner.cpp / MGBAScriptJNI.cpp / PokeScript.cpp into the
# frontend's cpp dir, and copied GbAccessibilityScript.kt / GbRomResolver.kt into
# the Java tree -- but NOTHING listed those .cpp files in app/CMakeLists.txt, so they
# were never compiled and libmgba was never linked. Verified against the shipped
# .so: it contains NDS/GPU/ARMJIT and ZERO MGBARunner/MGBACore symbols, and zero Lua
# symbols either (PokeScript.cpp, the NDS path's Lua host, was equally unwired).
#
# This builds the mGBA core and Lua as a CMake subproject for Android, then wires
# the four overlay files and the archive into the frontend target.
# ---------------------------------------------------------------------------
say "building the GBA/Game Boy core for Android"

MGBA_SRC="${MGBA_SRC:-}"
LUA_SRC="${LUA_SRC:-}"

if [ -z "$MGBA_SRC" ] || [ -z "$LUA_SRC" ]; then
  # ⛔ NOT a silent skip. Doing nothing here is exactly how the GBA path went
  # missing: the sources were present, nothing built them, and no step complained.
  # Set OGA_ALLOW_NO_GBA=1 to build the DS-only app deliberately.
  if [ "${OGA_ALLOW_NO_GBA:-0}" = "1" ]; then
    echo "   GBA core SKIPPED (OGA_ALLOW_NO_GBA=1)"
  else
    echo "   !! MGBA_SRC / LUA_SRC not set, so the GBA/Game Boy core would NOT be built." >&2
    echo "      Set MGBA_SRC and LUA_SRC (e.g. MGBA_SRC=~/src/mgba LUA_SRC=~/src/lua-5.4.7)," >&2
    echo "      or set OGA_ALLOW_NO_GBA=1 to build a DS-only app on purpose." >&2
    exit 1
  fi
else
  [ -d "$MGBA_SRC/src" ] || { echo "   !! no mGBA source at $MGBA_SRC" >&2; exit 1; }
  [ -d "$LUA_SRC/src" ]  || { echo "   !! no Lua source at $LUA_SRC" >&2; exit 1; }

  # The source trees must be reachable from the GRADLE/CMake build, which runs on
  # the Windows/host side of WSL. Copy them in, exactly as the Lua core is copied.
  mkdir -p "$FRONTEND/app/mgba"
  rm -rf "$FRONTEND/app/mgba/src" "$FRONTEND/app/mgba/lua"
  cp -r "$MGBA_SRC" "$FRONTEND/app/mgba/src"
  cp -r "$LUA_SRC"  "$FRONTEND/app/mgba/lua"
  # ⛔ Copy the SOURCE, not the whole checkout. mGBA ships cinema/ -- 243
  # .gb/.gba/.sav test ROMs, 38 MB, its own MIT-licensed fixtures -- plus a 16 MB
  # .git. Nothing in this build references cinema/ (zero mentions in the audited
  # source list), and shipping game-ROM files, even test ones, is exactly what the
  # ROM guard exists to stop. The guard was right; the copy was wrong.
  rm -rf "$FRONTEND/app/mgba/src/cinema" "$FRONTEND/app/mgba/src/.git"
  # Also drop stale build output. CI clones fresh and would not have these, but the
  # copy must not depend on the source checkout being pristine: a leftover build dir
  # carries .o files that bloat the tree and trip the repo guard.
  rm -rf "$FRONTEND/app/mgba/src/build-ios" "$FRONTEND/app/mgba/src/build-host" \
         "$FRONTEND/app/mgba/src/build"
  if [ ! -d "$FRONTEND/app/mgba/src/src" ]; then
    echo "   !! the copied mGBA tree is not usable (no src/)" >&2; exit 1
  fi
  if find "$FRONTEND/app/mgba/src" -type f \( -iname '*.gb' -o -iname '*.gba' -o -iname '*.sav' \) 2>/dev/null | grep -q .; then
    echo "   !! game ROM files survived in the copied mGBA tree" >&2; exit 1
  fi
  echo "     mGBA source copied; cinema/ and .git/ removed, ROM check asserted"

  # mGBA's flags.h. CMake normally writes this from flags.h.in; here the
  # template's #cmakedefine lines are neutralised so every feature comes from the
  # -D list above. Same sed as scripts/build-core.sh uses for iOS (CMake's regex
  # cannot do it: there `.` matches newlines).
  # mGBA generates version.c from git at configure time; we have no configure step.
  # Core/mgba_version_stub.cpp is the repo's answer for exactly this, used by the iOS
  # build. Same stub here -- it is not part of the overlay, so bring it along.
  cp "$ROOT/Core/mgba_version_stub.cpp" "$FRONTEND/app/mgba/version_stub.cpp"
  echo "     added the mGBA version-symbol stub"

  mkdir -p "$FRONTEND/app/mgba/gen/mgba"
  sed -e 's/#cmakedefine01 \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      -e 's/#cmakedefine \([A-Za-z_0-9]*\).*/#ifndef \1\n#endif/' \
      "$MGBA_SRC/src/core/flags.h.in" > "$FRONTEND/app/mgba/gen/mgba/flags.h"
  echo "     generated mgba/flags.h"

  python3 - "$ROOT/scripts/core-sources.sh" "$FRONTEND/app/mgba/CMakeLists.txt" <<'PY'
import re, sys
srcs = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r'MGBA="(.*?)"\n', srcs, re.S)
files = [f for f in m.group(1).split() if f.endswith('.c')]

# ⛔ THE GB AND SM83 SETS ARE READ FROM scripts/core-sources.sh, NOT WRITTEN HERE.
# They used to be a hand-maintained copy, and it was WRONG in this very file: it
# omitted src/gb/mbc/mbc.c, the dispatcher holding the &GBMBC*Create/&GBMBC*Load
# pointers, so every mbc/ file compiled unreferenced and a GB cart with a memory
# bank controller could not be built. It also carried src/gb/test/* (mGBA's own
# tests, not the library). The iOS lists are generated from mGBA's CMake; Android
# reads the same ones, so the two platforms cannot ship different emulators.
def _oga_list(srcs, name):
    m = re.search(r'^' + name + r'="(.*?)"\n', srcs, re.S | re.M)
    assert m, "no " + name + " list in " + sys.argv[1]
    return [f for f in m.group(1).split() if f.endswith('.c')]

GB_EXTRA = _oga_list(srcs, "MGBA_GB")
SM83_EXTRA = _oga_list(srcs, "MGBA_SM83")
assert GB_EXTRA, "MGBA_GB came back empty -- refusing to generate a broken CMakeLists"
assert "src/gb/mbc/mbc.c" in GB_EXTRA, "the MBC dispatcher is missing from MGBA_GB"
LZMA_EXTRA = [
    "src/third-party/lzma/7zArcIn.c", "src/third-party/lzma/7zBuf.c",
    "src/third-party/lzma/7zCrc.c", "src/third-party/lzma/7zCrcOpt.c",
    "src/third-party/lzma/7zDec.c", "src/third-party/lzma/7zFile.c",
    "src/third-party/lzma/7zStream.c", "src/third-party/lzma/Bcj2.c",
    "src/third-party/lzma/Bra.c", "src/third-party/lzma/Bra86.c",
    "src/third-party/lzma/CpuArch.c", "src/third-party/lzma/Delta.c",
    "src/third-party/lzma/Lzma2Dec.c", "src/third-party/lzma/LzmaDec.c",
    # 7zDec references IA64_Convert. PPSSPP's set omits this file, but leaving it out
    # here left the symbol undefined.
    "src/third-party/lzma/BraIA64.c",
]
NL = chr(10)
with open(sys.argv[2], "w", encoding="utf-8") as fh:
    fh.write("""# Generated by scripts/android-apply-overlay.sh -- do not edit.
#
# The AUDITED mGBA source list, the same one iOS builds (scripts/core-sources.sh).
# mGBA's own CMakeLists is not used: it wants json-c and a system Lua, and is not
# written for the NDK. This list is proven on iOS and was verified to compile
# 126/126 for aarch64-linux-android26 before this file existed.

cmake_minimum_required(VERSION 3.22)
project(oga-mgba C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ⛔ HAVE_PTHREAD_SET_NAME_NP is the BSD spelling and must NOT be defined on Android:
# defining it makes src/gba/core.c and src/gba/video.c fail with
# 'call to undeclared function pthread_set_name_np'. Android has the other spelling.
# ⛔ These must be PUBLIC. add_compile_definitions() only applies to THIS
# subdirectory, so the accessibility hosts -- which compile in the frontend target --
# saw none of them and the mGBA headers hid detachDebugger (ENABLE_DEBUGGERS) and
# VFileOpen (ENABLE_VFS) behind their guards. PUBLIC propagates through the link.
set(OGA_MGBA_DEFS
    BUILD_STATIC
    ENABLE_DEBUGGERS ENABLE_DIRECTORIES ENABLE_SCRIPTING ENABLE_VFS ENABLE_VFS_FD
    HAVE_FREELOCALE HAVE_LOCALE HAVE_LOCALTIME_R HAVE_NEWLOCALE
    HAVE_PTHREAD_CREATE HAVE_PTHREAD_SETNAME_NP
    HAVE_REALPATH HAVE_SETLOCALE HAVE_STRDUP HAVE_STRLCPY HAVE_STRNDUP
    HAVE_USELOCALE HAVE_VASPRINTF HAVE_XLOCALE
    # ⛔ HAVE_STRTOF_L is REQUIRED on Android and is NOT in the iOS set. mGBA ships a
    # strtof_l fallback under #ifndef HAVE_STRTOF_L, and bionic already provides one
    # (bits/stdlib_inlines.h), so without this the build dies with
    # 'redefinition of strtof_l'. mGBA's own CMakeLists sets it for
    # ANDROID AND ANDROID_NDK_MAJOR GREATER 13 -- this is that branch.
    HAVE_STRTOF_L
    M_CORE_GBA M_CORE_GB
    USE_LUA USE_LZMA USE_PTHREADS
)

# mGBA's generated flags.h. CMake normally writes this from flags.h.in; the
# template's #cmakedefine lines are neutralised so every enabled feature comes from
# the -D list above. Same technique scripts/build-core.sh uses for iOS.
# ⛔ The pattern must consume the WHOLE line (note the trailing .*). Some flags are
# written `#cmakedefine MINIMAL_CORE @MINIMAL_CORE@`, and an unanchored match leaves
# the @VAR@ placeholder behind as literal text in a C header.
# mGBA's flags.h is generated by the SHELL (sed) into ./gen, not here.
# ⛔ CMake's regex treats `.` as matching NEWLINES, so rewriting the template
# in-CMake silently consumed the rest of the file and produced an unterminated
# #ifndef. scripts/android-apply-overlay.sh does it with the same sed
# scripts/build-core.sh uses for iOS, which is already proven.

""")
    fh.write("set(MGBA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)\n")
    fh.write("set(LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/lua)\n\n")
    fh.write("set(MGBA_SOURCES\n")
    for f in files:
        fh.write(f"    ${{MGBA_SRC}}/{f}\n")
    fh.write(")\n\n")
    # Extra source sets the audited GBA-only list does not carry.
    extra_head = NL.join([
        "",
        "# --- Game Boy core. M_CORE_GB is set above, which REGISTERS the GB",
        "#     core in mGBA's filter table; without the sources it would be",
        "#     referenced but never defined. This set is MGBA_GB from",
        "#     scripts/core-sources.sh, generated from mGBA's own CMakeLists. ---",
        "set(GB_SOURCES",
    ])
    fh.write(extra_head + NL)
    for f in GB_EXTRA:
        fh.write(f"    ${{MGBA_SRC}}/{f}" + NL)
    fh.write(")" + NL)

    fh.write(NL.join([
        "",
        "# --- SM83 (Game Boy CPU), from mGBA's src/sm83/CMakeLists.txt. Needed only",
        "#     because M_CORE_GB is on. ---",
        "set(SM83_SOURCES",
    ]) + NL)
    for f in SM83_EXTRA:
        fh.write(f"    ${{MGBA_SRC}}/{f}" + NL)
    fh.write(")" + NL)

    fh.write(NL.join([
        "",
        "# --- LZMA (mGBA's bundled SDK). vfs-lzma.c needs the 7z archive reader.",
        "#     iOS borrows PPSSPP's copy of this SDK; Android builds no PPSSPP, so",
        "#     mGBA's own is compiled here instead. ---",
        "set(LZMA_SOURCES",
    ]) + NL)
    for f in LZMA_EXTRA:
        fh.write(f"    ${{MGBA_SRC}}/{f}" + NL)
    fh.write(")" + NL)

    fh.write("""\nfile(GLOB LUA_SOURCES ${LUA_SRC}/src/*.c)

add_library(oga-mgba STATIC
    ${MGBA_SOURCES} ${GB_SOURCES} ${SM83_SOURCES} ${LZMA_SOURCES} ${LUA_SOURCES}
    version_stub.cpp)

target_include_directories(oga-mgba PUBLIC
    ${MGBA_SRC}/include
    ${MGBA_SRC}/src
    ${MGBA_SRC}/src/third-party/lzma
    ${CMAKE_CURRENT_SOURCE_DIR}/gen  # the shell-generated mgba/flags.h
    ${LUA_SRC}/src
)

target_compile_options(oga-mgba PRIVATE -w)
target_compile_definitions(oga-mgba PRIVATE ${OGA_MGBA_DEFS})

# Propagate to consumers: the accessibility hosts need the same view of mGBA's
# headers, or the guards above hide the members they call.
target_compile_definitions(oga-mgba INTERFACE ${OGA_MGBA_DEFS})
""")
print(f"     generated the mGBA build ({len(files)} audited sources + Lua)")
PY

  # ---- wire it into the frontend ----
  python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

if "oga-mgba" in s:
    print("     already wired; nothing to do")
    sys.exit(0)

NL = chr(10)

# 1. build the mGBA subproject. It must come before the core's add_subdirectory is
#    irrelevant, but before target_link_libraries is not.
anchor = "add_subdirectory(${CORE-LIB} ./melonDS-android-lib)"
if anchor not in s:
    print("     !! core add_subdirectory anchor not found"); sys.exit(1)
intro = NL.join([
    "# The GBA/Game Boy core. Built from an audited source list rather than mGBA's",
    "# own CMakeLists -- see scripts/android-apply-overlay.sh section 7 for why.",
    "add_subdirectory(mgba ./oga-mgba)",
    "",
])
s = s.replace(anchor, intro + anchor, 1)

# 2. the four accessibility hosts were copied here but never LISTED.
hosts = NL.join([
    "        # Open Game Access accessibility hosts. Previously copied here but never",
    "        # listed, so they were never compiled -- see the overlay script.",
    "        ${ANDROID-CPP}/PokeScript.cpp",
    "        ${ANDROID-CPP}/MGBACore.cpp",
    "        ${ANDROID-CPP}/MGBARunner.cpp",
    "        ${ANDROID-CPP}/MGBAScriptJNI.cpp",
])
s = s.replace("        ${ANDROID-CPP}/MelonInstance.cpp",
              "        ${ANDROID-CPP}/MelonInstance.cpp" + NL + hosts, 1)

# 3. mGBA's and Lua's headers.
inc = NL.join([
    "target_include_directories(melonDS-android-frontend PUBLIC",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/mgba/src/include",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/mgba/src/src",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/mgba/lua/src",
])
s = s.replace("target_include_directories(melonDS-android-frontend PUBLIC", inc, 1)

# 4. link the archive.
s = s.replace(
    "target_link_libraries(melonDS-android-frontend android core net-utils oboe faad enet rcheevos EGL GLESv3)",
    "target_link_libraries(melonDS-android-frontend android core net-utils oboe faad enet rcheevos EGL GLESv3 oga-mgba)", 1)

open(p, "w", encoding="utf-8").write(s)
print("     wired oga-mgba + the four accessibility hosts into the frontend")
PY
fi


# ---------------------------------------------------------------------------
# 8. The PSP native path (PPSSPP subset).
#
# Mirrors section 7's mGBA generator: parse the AUDITED lists out of
# scripts/core-sources.sh (PPSPP_CORE / PPSPP_EXT_CPP / PPSPP_EXT_C /
# PPSPP_ARM / PPSPP_X86 / PPSPP_X86_ASM / PPSPP_GLUE), copy the pinned
# PPSSPP tree in, generate miniupnpcstrings.h with miniupnp's own script
# (the same command build-core.sh runs), and emit a CMake subproject
# building a static oga-ppsspp. Upstream PPSSPP's Android build is NOT
# used: it links a monolithic ppsspp_jni with the full UI and the
# hardware GPU backends; this subset is the iOS-proven one (IR
# interpreter + software GPU, no GL/Vulkan anywhere).
#
# Per-ABI gating happens INSIDE the generated CMake, where
# CMAKE_ANDROID_ARCH_ABI is known:
#   arm64-v8a:  subset + PPSPP_ARM + PPSPP_ANDROID_ARM64 (the cpu_features
#               AArch64 impl no iOS list carries; see core-sources.sh)
#   x86_64:     subset + PPSPP_X86 + PPSPP_X86_ASM
#   armeabi-v7a: the lists were never audited for 32-bit ARM, so the
#               subproject builds an EMPTY oga-ppsspp there; the frontend
#               link line stays unconditional and PSP is simply absent.
# -DMOBILE_DEVICE matches the iOS ppspp/ppsppc cases in build-core.sh.
#
# Stage 5b adds ffmpeg: USE_FFMPEG, defined exactly as build-core.sh
# defines it for iOS, against the minimal static libs
# scripts/build-ffmpeg-android.sh cross-builds from the same pinned
# fork with build-ffmpeg.sh's decoder/demuxer set. Without it the
# subset's sceMpeg/sceVideocodec PMF paths are compiled out and KH
# Birth by Sleep's opening movie never advances (stage-4 find). The
# installs are staged under ppsspp/ffmpeg/<abi>/ below, beside the
# generated CMake that links them.
#
# Stage 2 adds the debug boot-proof harness: PspSessionJNI.cpp rides in
# the oga-ppsspp-glue OBJECT library (so it exists on exactly the ABIs
# the core does -- armeabi-v7a, where the subset is empty, gets neither),
# PspProbe.kt drives it through an intent extra (nothing in the launcher
# reaches it), and the runtime assets are staged into the APK from
# build-assets/ below.
#
# Stage 4 adds the adapter host to the same glue library:
# PspAdapterHost.cpp (the Android oga::Host) plus the OGA adapter layer
# it drives — Core's adapter.h/announce/osk_echo and the four PSP
# adapter TUs, with Core/story/ (Dissidia's scene include) beside them.
# They are OGA code copied in like psp_core.cpp, NOT part of the
# PPSSPP subset lists in core-sources.sh; the registry TU
# (Core/adapters.cpp) is deliberately not among them — see
# PspAdapterHost.cpp's header for why, and launcher contract §10 for
# the drift guard on its PSP dispatch table.
# ---------------------------------------------------------------------------
say "building the PSP core (PPSSPP subset) for Android"

PPSSPP_SRC="${PPSSPP_SRC:-$HOME/src/ppsspp}"

if [ ! -d "$PPSSPP_SRC/Core" ]; then
  # Same rule as section 7: a silent skip is how a core goes missing.
  if [ "${OGA_ALLOW_NO_PSP:-0}" = "1" ]; then
    echo "   PSP core SKIPPED (OGA_ALLOW_NO_PSP=1)"
  else
    echo "   !! no PPSSPP source at $PPSSPP_SRC, so the PSP core would NOT be built." >&2
    echo "      Run scripts/bootstrap-deps.sh, or set PPSSPP_SRC, or set" >&2
    echo "      OGA_ALLOW_NO_PSP=1 to build without PSP on purpose." >&2
    exit 1
  fi
else
  mkdir -p "$FRONTEND/app/ppsspp"
  rm -rf "$FRONTEND/app/ppsspp/src"
  cp -r "$PPSSPP_SRC" "$FRONTEND/app/ppsspp/src"
  # The checkout's .git is not build input (same rule as the mGBA copy).
  rm -rf "$FRONTEND/app/ppsspp/src/.git"
  if [ ! -f "$FRONTEND/app/ppsspp/src/Core/System.cpp" ]; then
    echo "   !! the copied PPSSPP tree is not usable (no Core/)" >&2; exit 1
  fi
  echo "     PPSSPP source copied (pinned $(git -C "$PPSSPP_SRC" rev-parse --short HEAD 2>/dev/null || echo unknown))"

  # miniupnpcstrings.h: generated by miniupnp's own script from VERSION,
  # exactly as scripts/build-core.sh does for iOS.
  mkdir -p "$FRONTEND/app/ppsspp/gen"
  ( cd "$FRONTEND/app/ppsspp/src/ext/miniupnp/miniupnpc" && \
    sh updateminiupnpcstrings.sh "$FRONTEND/app/ppsspp/gen/miniupnpcstrings.h" miniupnpcstrings.h.in ) > /dev/null
  echo "     generated miniupnpcstrings.h"
  # The v7a stub TU (see the header comment): a real file, so the generated
  # CMake can name it unconditionally per-ABI.
  printf '// Generated by scripts/android-apply-overlay.sh.\n// armeabi-v7a gets an empty oga-ppsspp: the audited PPSSPP subset\n// lists were never measured for 32-bit ARM. PSP is absent there.\n' \
    > "$FRONTEND/app/ppsspp/gen/oga_ppsspp_empty.cpp"

  # The vendored-cousin rename script (see the generated CMakeLists):
  # it runs as a POST_BUILD step on the oga-ppsspp archive.
  cat > "$FRONTEND/app/ppsspp/gen/oga_rename_cousins.py" <<'PYEOF'
#!/usr/bin/env python3
# oga_rename_cousins.py -- generated by scripts/android-apply-overlay.sh.
#
# Rename the vendored-cousin symbols in PPSSPP's Android objects:
# melonDS's core vendors the same Dolphin-descended code (src/dolphin/)
# that PPSSPP's Common/ descends from, under the same symbol names:
#
#   * the emitter families in the Gen:: and Arm64Gen:: namespaces
#     (PPSSPP's copy is a diverged superset; neither can serve the
#     other in one .so), and
#   * the CPUInfo family: the global `cpu_info` object and CPUInfo's
#     methods. The two trees' CPUInfo structs have DIFFERENT layouts,
#     so sharing the symbol is not a benign first-wins: measured on
#     test34, PPSSPP's sampler JIT guards read feature flags through
#     the shared object while x64Emitter's assert read them back
#     false, and the boot died at 2.2s with
#     "(x64Emitter.cpp:WriteBMI2Op:1499) Trying to use BMI2 on a
#     system that doesn't support it."
#
# Every matching symbol in each file given -- definitions and
# references alike -- gets a ppsspp_ prefix, so PPSSPP code (subset
# archive AND the glue objects, which reference cpu_info for core
# counts) binds PPSSPP's copies and melonDS keeps its own. Runs as a
# a POST_BUILD step on oga-ppsspp for the archive and a custom target
# for the glue objects, each file self-contained, so incremental
# rebuilds of either stay renamed.
# Idempotent: prefixed names no longer match the patterns.
#
# The other first-wins twins between the two trees (the LZMA SDK and
# xxhash C APIs, libc++ instantiations) are the same upstream code
# with the same ABI on both sides; they are deliberately NOT renamed.
import os
import subprocess
import sys

PATTERNS = ("_ZN3Gen", "_ZNK3Gen", "_ZTV3Gen", "_ZTI3Gen", "_ZTS3Gen",
            "_ZN8Arm64Gen", "_ZNK8Arm64Gen", "_ZTV8Arm64Gen",
            "_ZTI8Arm64Gen", "_ZTS8Arm64Gen",
            "_ZN7CPUInfo", "_ZNK7CPUInfo", "_ZTV7CPUInfo",
            "_ZTI7CPUInfo", "_ZTS7CPUInfo")
EXACT = ("cpu_info",)


def rename_in(path, objcopy, nm):
    if not os.path.exists(path):
        print("oga-ppsspp: rename target does not exist: %s" % path)
        sys.exit(1)
    proc = subprocess.run([nm, path], capture_output=True, text=True)
    if proc.returncode != 0:
        print("oga-ppsspp: nm failed on %s: %s" % (path, proc.stderr.strip()))
        sys.exit(1)
    out = proc.stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if parts and (parts[-1].startswith(PATTERNS) or parts[-1] in EXACT):
            names.add(parts[-1])
    if not names:
        print("oga-ppsspp: no cousin symbols in %s" % os.path.basename(path))
        return
    map_path = path + ".renames"
    with open(map_path, "w", encoding="utf-8") as fh:
        for n in sorted(names):
            fh.write("%s ppsspp_%s\n" % (n, n))
    subprocess.run([objcopy, "--redefine-syms", map_path, path], check=True)
    print("oga-ppsspp: renamed %d cousin symbols in %s"
          % (len(names), os.path.basename(path)))


def main():
    objcopy, args = sys.argv[1], sys.argv[2:]
    nm = os.path.join(os.path.dirname(objcopy), "llvm-nm")
    files = []
    for a in args:
        if os.path.isdir(a):
            # A target's object directory: rename every object in it.
            # (Callers pass this instead of $<TARGET_OBJECTS:...>: a
            # genex list arrives as ONE semicolon-joined argument and
            # the shell eats the rest -- measured, twice.)
            for root, _dirs, names in os.walk(a):
                files.extend(os.path.join(root, n) for n in names
                             if n.endswith(".o"))
        else:
            files.append(a)
    for path in sorted(files):
        rename_in(path, objcopy, nm)


main()
PYEOF
  echo "     generated the cousin-symbol rename script"

  # The glue rides along like mGBA's version stub does in section 7:
  # Core/psp_core.cpp is OGA code, not part of the copied upstream tree.
  mkdir -p "$FRONTEND/app/ppsspp/glue"
  cp "$ROOT/Core/psp_core.cpp" "$ROOT/Core/psp_core.h" "$FRONTEND/app/ppsspp/glue/"
  cp "$OVERLAY/app/src/main/cpp/PspAndroidShims.cpp" "$FRONTEND/app/ppsspp/glue/"
  cp "$OVERLAY/app/src/main/cpp/PspSessionJNI.cpp" "$FRONTEND/app/ppsspp/glue/"
  # Stage 4: the adapter host + the adapter layer it drives (Core TUs,
  # copied like psp_core.cpp; Core/story/ is Dissidia's scene include).
  cp "$OVERLAY/app/src/main/cpp/PspAdapterHost.cpp" \
     "$OVERLAY/app/src/main/cpp/PspAdapterHost.h" "$FRONTEND/app/ppsspp/glue/"
  cp "$ROOT/Core/adapter.h" "$ROOT/Core/announce.h" "$ROOT/Core/announce.cpp" \
     "$ROOT/Core/osk_echo.h" "$ROOT/Core/osk_echo.cpp" \
     "$ROOT/Core/dissidia_adapter.cpp" "$ROOT/Core/dbzar_adapter.cpp" \
     "$ROOT/Core/mku_adapter.cpp" "$ROOT/Core/khbbs_adapter.cpp" \
     "$FRONTEND/app/ppsspp/glue/"
  cp -r "$ROOT/Core/story" "$FRONTEND/app/ppsspp/glue/"
  echo "     added the psp_core glue + the Android platform shims"

  # The runtime assets ride in the APK as assets/ppsspp-assets/ (PspProbe
  # extracts them to filesDir at runtime; psp_core's VFS needs real
  # files). build-assets/ppsspp-assets is the canonical staged copy
  # (scripts/ppsspp-stage-assets.sh). Fail loudly here rather than after
  # a full build: the core names any missing file at load time, but
  # only once the APK is already running.
  if [ ! -f "$ROOT/build-assets/ppsspp-assets/compat.ini" ]; then
    echo "   !! no staged PPSSPP assets at $ROOT/build-assets/ppsspp-assets" >&2
    echo "      Run scripts/ppsspp-stage-assets.sh first." >&2; exit 1
  fi
  rm -rf "$FRONTEND/app/src/main/assets/ppsspp-assets"
  cp -r "$ROOT/build-assets/ppsspp-assets" "$FRONTEND/app/src/main/assets/ppsspp-assets"
  echo "     staged the PPSSPP runtime assets into the APK assets"

  # ---- ffmpeg for the subset (stage 5b) ----
  # build-ffmpeg-android.sh is stamp-gated, so this costs nothing when
  # the installs are current; it FAILS LOUDLY when the NDK is missing,
  # because a silent skip here is how USE_FFMPEG would quietly vanish
  # and BBS's opening movie would stall again. The installs are copied
  # into the frontend tree beside the generated CMake (which links
  # them per-ABI), so the build tree stays self-contained like the
  # copied PPSSPP tree above.
  FFMPEG_SRC="$PPSSPP_SRC/ffmpeg" \
    bash "$ROOT/scripts/build-ffmpeg-android.sh" arm64-v8a || exit 1
  FFMPEG_SRC="$PPSSPP_SRC/ffmpeg" \
    bash "$ROOT/scripts/build-ffmpeg-android.sh" x86_64 || exit 1
  rm -rf "$FRONTEND/app/ppsspp/ffmpeg"
  for abi in arm64-v8a x86_64; do
    ffout="${FFMPEG_ANDROID_OUT:-$HOME/ffmpeg-android}/$abi"
    for lib in avcodec avformat avutil swresample swscale; do
      [ -f "$ffout/lib/lib$lib.a" ] || { echo "   !! ffmpeg android/$abi missing lib$lib.a at $ffout" >&2; exit 1; }
    done
    mkdir -p "$FRONTEND/app/ppsspp/ffmpeg/$abi/lib"
    cp "$ffout"/lib/libavcodec.a "$ffout"/lib/libavformat.a \
       "$ffout"/lib/libavutil.a "$ffout"/lib/libswresample.a \
       "$ffout"/lib/libswscale.a "$FRONTEND/app/ppsspp/ffmpeg/$abi/lib/"
    cp -r "$ffout/include" "$FRONTEND/app/ppsspp/ffmpeg/$abi/include"
  done
  echo "     staged the minimal ffmpeg (USE_FFMPEG) for arm64-v8a + x86_64"

  python3 - "$ROOT/scripts/core-sources.sh" "$FRONTEND/app/ppsspp/CMakeLists.txt" "$FRONTEND/app/ppsspp" <<'PY'
import os, re, sys
srcs = open(sys.argv[1], encoding="utf-8").read()
out_path, ppsspp_dir = sys.argv[2], sys.argv[3]

def _oga_list(name, exts):
    m = re.search(r'^' + name + r'="(.*?)"\n', srcs, re.S | re.M)
    assert m, "no " + name + " list in " + sys.argv[1]
    files = []
    for f in m.group(1).split():
        f = f.lstrip("/")
        if any(f.endswith(e) for e in exts):
            files.append(f)
    assert files, name + " came back empty -- refusing to generate a broken CMakeLists"
    return files

CORE   = _oga_list("PPSPP_CORE", (".cpp",))
EXT_CPP = _oga_list("PPSPP_EXT_CPP", (".cpp",))
EXT_C  = _oga_list("PPSPP_EXT_C", (".c",))
ARM    = _oga_list("PPSPP_ARM", (".c",))
X86    = _oga_list("PPSPP_X86", (".c",))
X86_ASM = _oga_list("PPSPP_X86_ASM", (".S",))
AND_ARM64 = _oga_list("PPSPP_ANDROID_ARM64", (".c",))
ANDROID = _oga_list("PPSPP_ANDROID", (".cpp",))
GLUE   = _oga_list("PPSPP_GLUE", (".cpp",))
assert GLUE == ["psp_core.cpp"], "PPSPP_GLUE is expected to be exactly psp_core.cpp"

# Fail at generation time, not at some later link: every named file must
# exist in the copied tree (the same guard ppsspp-subset-test.sh runs).
tree = os.path.join(ppsspp_dir, "src")
for f in CORE + EXT_CPP + EXT_C + ARM + X86 + X86_ASM + AND_ARM64 + ANDROID:
    assert os.path.isfile(os.path.join(tree, f)), "missing from the PPSSPP tree: " + f

# cpu_features TUs: build-core.sh compiles these with the ppsppx case,
# which adds -DSTACK_LINE_READER_BUFFER_SIZE=1024 (stack_line_reader.c
# needs it; harmless on its siblings, and iOS applies it to the set).
CPU_FEATURES = sorted(set(X86 + AND_ARM64))

NL = chr(10)
def emit_list(fh, var, files, prefix="${PPSPP_SRC}/"):
    fh.write("set(%s\n" % var)
    for f in files:
        fh.write("    %s%s\n" % (prefix, f))
    fh.write(")\n\n")

with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("""# Generated by scripts/android-apply-overlay.sh -- do not edit.
#
# The AUDITED PPSSPP source lists, the same ones iOS builds
# (scripts/core-sources.sh). Upstream PPSSPP's CMake/Android build is not
# used: it builds the full application (UI, Vulkan/GLES backends,
# glslang). This subset is the host- and iOS-proven configuration: IR
# interpreter + software GPU, headless platform hooks in the glue
# (glue/psp_core.cpp, OGA's Core/psp_core.cpp copied in by the overlay).
#
# Per-ABI source gating, decided here because CMAKE_ANDROID_ARCH_ABI is
# only known at configure time:
#   arm64-v8a:  PPSPP_ARM (libpng NEON) + PPSPP_ANDROID_ARM64 (the
#               cpu_features AArch64 impl; iOS never compiles one)
#   x86_64:     PPSPP_X86 (cpu_features) + PPSPP_X86_ASM (zstd asm)
#   armeabi-v7a: EMPTY -- the lists were never audited for 32-bit ARM.

cmake_minimum_required(VERSION 3.22)
project(oga-ppsspp C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PPSPP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PPSPP_GEN ${CMAKE_CURRENT_SOURCE_DIR}/gen)

# The include roots are ppspp_inc() from scripts/core-sources.sh,
# verbatim, plus the generated-headers dir (miniupnpcstrings.h) and the
# glue dir (psp_core.h). ONE definition there; this is its transcription.
set(OGA_PPSPP_INC
    ${PPSPP_SRC}
    ${PPSPP_SRC}/Common
    ${PPSPP_SRC}/ext
    ${PPSPP_SRC}/ext/snappy
    ${PPSPP_SRC}/ext/libpng17
    ${PPSPP_SRC}/ext/zstd/lib
    ${PPSPP_SRC}/ext/cpu_features/include
    ${PPSPP_SRC}/ext/armips
    ${PPSPP_SRC}/ext/armips/ext/filesystem/include
    ${PPSPP_SRC}/ext/libchdr/include
    ${PPSPP_SRC}/ext/lua
    ${PPSPP_SRC}/ext/naett-lib
    ${PPSPP_SRC}/ext/libzip
    ${PPSPP_SRC}/ext/aemu_postoffice/client
    ${PPSPP_SRC}/ext/miniupnp/miniupnpc/include
    ${PPSPP_SRC}/ext/miniupnp/miniupnpc/src
    ${PPSPP_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/glue
)

""")
    emit_list(fh, "PPSPP_CORE_SOURCES", CORE)
    emit_list(fh, "PPSPP_EXT_CPP_SOURCES", EXT_CPP)
    emit_list(fh, "PPSPP_EXT_C_SOURCES", EXT_C)
    emit_list(fh, "PPSPP_ARM_SOURCES", ARM)
    emit_list(fh, "PPSPP_X86_SOURCES", X86)
    emit_list(fh, "PPSPP_X86_ASM_SOURCES", X86_ASM)
    emit_list(fh, "PPSPP_ANDROID_ARM64_SOURCES", AND_ARM64)
    emit_list(fh, "PPSPP_ANDROID_SOURCES", ANDROID)
    fh.write("""set(OGA_PPSPP_GLUE ${CMAKE_CURRENT_SOURCE_DIR}/glue/psp_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/PspAndroidShims.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/PspSessionJNI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/PspAdapterHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/announce.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/osk_echo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/dissidia_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/dbzar_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/mku_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/glue/khbbs_adapter.cpp)

if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(OGA_PPSPP_SOURCES
        ${PPSPP_CORE_SOURCES} ${PPSPP_EXT_CPP_SOURCES} ${PPSPP_EXT_C_SOURCES}
        ${PPSPP_ARM_SOURCES} ${PPSPP_ANDROID_ARM64_SOURCES} ${PPSPP_ANDROID_SOURCES})
    set(OGA_PPSPP_HAVE_CORE ON)
elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(OGA_PPSPP_SOURCES
        ${PPSPP_CORE_SOURCES} ${PPSPP_EXT_CPP_SOURCES} ${PPSPP_EXT_C_SOURCES}
        ${PPSPP_X86_SOURCES} ${PPSPP_X86_ASM_SOURCES} ${PPSPP_ANDROID_SOURCES})
    set(OGA_PPSPP_HAVE_CORE ON)
else ()
    # armeabi-v7a (and anything unexpected): empty on purpose, see header.
    set(OGA_PPSPP_SOURCES ${PPSPP_GEN}/oga_ppsspp_empty.cpp)
    set(OGA_PPSPP_HAVE_CORE OFF)
endif ()

add_library(oga-ppsspp STATIC ${OGA_PPSPP_SOURCES})

# The glue is an OBJECT library, not part of the archive: the frontend
# target lists its objects directly (the wiring in
# scripts/android-apply-overlay.sh section 8), so psp_core.o is always
# in the .so and its references pull exactly the subset objects that
# archive semantics intend -- the arrangement iOS's single archive
# produces. (Whole-archiving the subset instead collides with the
# melonDS core's vendored cousins: CPUDetect and the x64 emitter exist
# in both trees with the same symbols.)
if (OGA_PPSPP_HAVE_CORE)
    add_library(oga-ppsspp-glue OBJECT ${OGA_PPSPP_GLUE})
    target_include_directories(oga-ppsspp-glue PRIVATE ${OGA_PPSPP_INC})
    target_compile_definitions(oga-ppsspp-glue PRIVATE MOBILE_DEVICE USE_FFMPEG)
    target_include_directories(oga-ppsspp-glue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/${CMAKE_ANDROID_ARCH_ABI}/include)
    target_compile_options(oga-ppsspp-glue PRIVATE -fwrapv -fno-strict-aliasing -w)
    set(OGA_PPSPP_GLUE_OBJECTS $<TARGET_OBJECTS:oga-ppsspp-glue> PARENT_SCOPE)
    # The glue references cousin symbols (cpu_info, for core counts), so
    # its objects get the same rename as the archive -- see the
    # isolation block below. An OBJECT library cannot take a POST_BUILD
    # command (CMake 3.22), so the rename is a custom target of its own;
    # section 8's wiring makes the frontend target depend on it, which
    # keeps the rename ahead of the link even on glue-only rebuilds.
    # The rename script gets the glue target's OBJECT DIRECTORY (one
    # argument), not $<TARGET_OBJECTS:...>: a genex list reaches the
    # shell as a single semicolon-joined string -- VERBATIM or not --
    # and the extra objects become shell commands (measured).
    add_custom_target(oga-ppsspp-rename-glue
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/oga_rename_cousins.py
                ${CMAKE_OBJCOPY}
                ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/oga-ppsspp-glue.dir
        COMMENT "oga-ppsspp-glue: renaming vendored-cousin symbols"
        VERBATIM)
    add_dependencies(oga-ppsspp-rename-glue oga-ppsspp-glue)
endif ()

# Vendored-cousin isolation: melonDS's core vendors the same
# Dolphin-descended code (src/dolphin/) that PPSSPP's Common/ descends
# from, under the same symbol names -- the Gen::/Arm64Gen:: emitter
# families (PPSSPP's are diverged supersets) and the CPUInfo family
# (cpu_info + CPUInfo's methods, with DIFFERENT struct layouts in the
# two trees). Rename PPSSPP's copies in this archive and in the glue
# objects (definitions and references alike) so each core binds its
# own; see gen/oga_rename_cousins.py for the measured failure.
if (OGA_PPSPP_HAVE_CORE)
    add_custom_command(TARGET oga-ppsspp POST_BUILD
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/oga_rename_cousins.py
                ${CMAKE_OBJCOPY} $<TARGET_FILE:oga-ppsspp>
        COMMENT "oga-ppsspp: renaming vendored-cousin symbols"
        VERBATIM)
endif ()

target_include_directories(oga-ppsspp PRIVATE ${OGA_PPSPP_INC})

# build-core.sh's ppspp/ppsppc cases: -DMOBILE_DEVICE, and the COMMON
# arithmetic flags the subset is proven with. USE_FFMPEG is defined
# in the ffmpeg block below (stage 5b), exactly as on iOS.
target_compile_definitions(oga-ppsspp PRIVATE MOBILE_DEVICE)
target_compile_options(oga-ppsspp PRIVATE -fwrapv -fno-strict-aliasing -w)

# The cpu_features set gets build-core.sh's ppsppx define.
set_source_files_properties(
""")
    for f in CPU_FEATURES:
        fh.write("    ${PPSPP_SRC}/%s\n" % f)
    fh.write("""    PROPERTIES COMPILE_DEFINITIONS STACK_LINE_READER_BUFFER_SIZE=1024)

# hwcaps.c (in the arm64 list only) needs upstream cpu_features' own
# detection results: its CMake runs check_include_file(dlfcn.h) and
# check_symbol_exists(getauxval "sys/auxv.h") and defines both for its
# hardware-detection objects. Bionic has both (getauxval since API 18;
# this app's minSdk is 24), so define both, for this file only.
set_source_files_properties(${PPSPP_SRC}/ext/cpu_features/src/hwcaps.c
    PROPERTIES COMPILE_DEFINITIONS "HAVE_DLFCN_H;HAVE_STRONG_GETAUXVAL")

# System libraries the subset references (zip/png go through Android's
# zlib; PPSSPP's logging and the Android file paths want liblog; dl for
# the loaders). PUBLIC: they belong to the frontend's final link.
target_link_libraries(oga-ppsspp PUBLIC z log dl)

# ffmpeg (stage 5b): USE_FFMPEG exactly as scripts/build-core.sh defines
# it for iOS, against the minimal static libs
# scripts/build-ffmpeg-android.sh builds (build-ffmpeg.sh's
# decoder/demuxer set, pinned fork) and section 8 stages under
# ffmpeg/<abi>/. Imported archives linked PUBLIC in dependency order,
# so the frontend's final link sees them after the subset objects that
# reference them; libm rides along (ffmpeg's tables want it).
if (OGA_PPSPP_HAVE_CORE)
    set(OGA_FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/${CMAKE_ANDROID_ARCH_ABI})
    foreach (fflib avformat avcodec swresample swscale avutil)
        add_library(ff${fflib} STATIC IMPORTED)
        set_target_properties(ff${fflib} PROPERTIES
            IMPORTED_LOCATION ${OGA_FFMPEG_DIR}/lib/lib${fflib}.a)
    endforeach ()
    target_compile_definitions(oga-ppsspp PRIVATE USE_FFMPEG)
    target_include_directories(oga-ppsspp PRIVATE ${OGA_FFMPEG_DIR}/include)
    target_link_libraries(oga-ppsspp PUBLIC ffavformat ffavcodec ffswresample ffswscale ffavutil m)
endif ()
""")
print("     generated the PPSSPP build (%d core + %d ext-cpp + %d ext-c audited sources + glue)"
      % (len(CORE), len(EXT_CPP), len(EXT_C)))
PY

  # ---- wire it into the frontend ----
  python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

NL = chr(10)

if "add_dependencies(melonDS-android-frontend oga-ppsspp-rename-glue)" in s:
    print("     already wired; nothing to do")
    sys.exit(0)

if "oga-ppsspp" in s:
    # Partially wired: an earlier build's tree kept the subdirectory and
    # the link line but lost the rename ordering (the frontend tree is
    # persistent across builds, and the old all-or-nothing check below
    # preserved the gap forever — measured in stage 4, when the frontend
    # link died on an unrenamed cpu_info in the glue). Complete just
    # that piece; anything else missing fails loudly at link time.
    link = ("target_link_libraries(melonDS-android-frontend android core "
            "net-utils oboe faad enet rcheevos EGL GLESv3 oga-mgba oga-ppsspp)")
    if link not in s:
        print("     !! partially wired frontend: link line not recognised")
        sys.exit(1)
    s = s.replace(link, link + NL +
                  "if (TARGET oga-ppsspp-rename-glue)" + NL +
                  "    add_dependencies(melonDS-android-frontend oga-ppsspp-rename-glue)" + NL +
                  "endif ()", 1)
    open(p, "w", encoding="utf-8").write(s)
    print("     completed the PSP wiring (the rename ordering was missing)")
    sys.exit(0)

anchor = "add_subdirectory(${CORE-LIB} ./melonDS-android-lib)"
if anchor not in s:
    print("     !! core add_subdirectory anchor not found"); sys.exit(1)
intro = NL.join([
    "# The PSP core (PPSSPP subset). Built from the audited source lists,",
    "# not upstream's Android build -- see scripts/android-apply-overlay.sh",
    "# section 8 for why.",
    "add_subdirectory(ppsspp ./oga-ppsspp)",
    "",
])
s = s.replace(anchor, intro + anchor, 1)

# The glue's objects ride in the frontend target itself, listed next
# to the mGBA accessibility hosts (section 7's pattern): psp_core.o is
# then always in the .so, and its references pull the subset objects
# from the archive -- normal archive semantics, as on iOS.
hosts_anchor = "        ${ANDROID-CPP}/MGBAScriptJNI.cpp"
if hosts_anchor not in s:
    print("     !! mGBA hosts anchor not found"); sys.exit(1)
s = s.replace(hosts_anchor,
              hosts_anchor + NL + "        ${OGA_PPSPP_GLUE_OBJECTS}", 1)

# Link the subset archive normally (see the generated CMakeLists for
# why whole-archive is wrong here: vendored-cousin symbol collisions).
old_link = "target_link_libraries(melonDS-android-frontend android core net-utils oboe faad enet rcheevos EGL GLESv3 oga-mgba)"
assert old_link in s, "frontend link line not found"
s = s.replace(old_link, old_link[:-1] + " oga-ppsspp)", 1)

# The glue objects must be cousin-renamed before this target links
# (the rename rewrites them in place); see the generated CMakeLists.
s = s.replace(old_link[:-1] + " oga-ppsspp)",
              old_link[:-1] + " oga-ppsspp)" + NL +
              "if (TARGET oga-ppsspp-rename-glue)" + NL +
              "    add_dependencies(melonDS-android-frontend oga-ppsspp-rename-glue)" + NL +
              "endif ()", 1)

open(p, "w", encoding="utf-8").write(s)
print("     wired oga-ppsspp into the frontend (glue objects in the target; see section 8)")
PY
fi

# ---------------------------------------------------------------------------
# 7. Accessibility: label the MENUS.
#
# Per Devin: label the menus, not all 571 UI files. The game itself is narrated by
# the accessibility Lua script, so labelling every on-screen control would be fluff.
# These are the two places a blind player actually has to navigate BY HAND.
# ---------------------------------------------------------------------------
say "labelling the menus for TalkBack"

# --- 7a. The ROM row: one merged, labelled item instead of text fragments. ---
ROMITEM="$FRONTEND/app/src/main/java/me/magnum/melonds/ui/common/component/romlist/ConfigurableRomItem.kt"
if [ -f "$ROMITEM" ] && ! grep -q 'ogaRomItemLabel\|mergeDescendants' "$ROMITEM"; then
  python3 - "$ROMITEM" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

# Needs the semantics imports.
old_imports = "import androidx.compose.ui.res.stringResource"
new_imports = """import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics"""
assert old_imports in s, "import anchor not found"
s = s.replace(old_imports, new_imports, 1)

# A single label from the ROM's own fields, and mergeDescendants so the row is one
# item. Without this TalkBack reads the title, the system and the file name as
# separate stops and the Settings button as another.
old_row = """                modifier = Modifier.height(IntrinsicSize.Min).focusRequester(mainFocusRequester)
                    .focusProperties {
                        end = romDetailsFocusRequester
                    }
                    .clickable(enabled = enabled, onClick = onClick),"""
new_row = """                modifier = Modifier.height(IntrinsicSize.Min).focusRequester(mainFocusRequester)
                    .focusProperties {
                        end = romDetailsFocusRequester
                    }
                    // One accessibility stop for the whole row, labelled from the ROM's
                    // own fields. Without this TalkBack reads the title, the system and
                    // the file name as separate fragments. No extra words: if the system
                    // is blank it is simply left out, rather than announced as "unknown".
                    .semantics(mergeDescendants = true) {
                        contentDescription = listOf(rom.name, rom.developerName)
                            .filter { it.isNotBlank() }
                            .joinToString(", ")
                    }
                    .clickable(enabled = enabled, onClick = onClick),"""
assert old_row in s, "row modifier anchor not found"
s = s.replace(old_row, new_row, 1)

open(p, "w", encoding="utf-8").write(s)
print("     ROM row now reads as one labelled item")
PY
else
  echo "     ROM row: already labelled, or not found"
fi

# --- 7b. The pause menu: give the dialog a title so TalkBack announces it. ---
EMUACT="$FRONTEND/app/src/main/java/me/magnum/melonds/ui/emulator/EmulatorActivity.kt"
if [ -f "$EMUACT" ] && ! grep -q 'ogaPauseMenuAnnounced' "$EMUACT"; then
  python3 - "$EMUACT" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """        activeOverlays.addActiveOverlay(EmulatorOverlay.PAUSE_MENU)
        AlertDialog.Builder(this)
                .setTitle(R.string.pause)
                .setItems(options) { _, which ->"""
new = """        activeOverlays.addActiveOverlay(EmulatorOverlay.PAUSE_MENU)
        // ogaPauseMenuAnnounced: the dialog title is what TalkBack announces when the
        // menu opens over the emulator surface. Without it a blind player who pressed
        // back has no confirmation the menu appeared at all.
        AlertDialog.Builder(this)
                .setTitle(R.string.pause)
                .setItems(options) { _, which ->"""
if old in s:
    s = s.replace(old, new, 1)
    open(p, "w", encoding="utf-8").write(s)
    print("     pause menu: title kept and documented as the announcement")
else:
    print("     pause menu: shape changed upstream; left alone")
PY
else
  echo "     pause menu: already annotated, or not found"
fi

# --- 7c. The PLAIN RomItem: the same label as its sibling, for the two lists that
# use it. ConfigurableRomItem is the main ROM list; RomItem is the DSiWare picker and
# the shortcut ROM picker, and neither had any semantics at all. One accessibility
# stop, labelled from the ROM's own fields, exactly as 7a does it.
PLAINROMITEM="$FRONTEND/app/src/main/java/me/magnum/melonds/ui/common/component/romlist/RomItem.kt"
if [ -f "$PLAINROMITEM" ] && ! grep -q 'mergeDescendants' "$PLAINROMITEM"; then
  python3 - "$PLAINROMITEM" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

old_imports = "import androidx.compose.ui.res.painterResource"
new_imports = """import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics"""
assert old_imports in s, "RomItem import anchor not found"
s = s.replace(old_imports, new_imports, 1)

# `item` is this component's parameter name (the sibling calls it `rom`).
old_col = """    Column(
        modifier
            .let { if (enabled) it.clickable { onClick() } else it }
            .alpha(if (enabled) 1f else DisabledAlpha)
    ) {"""
new_col = """    Column(
        modifier
            .let { if (enabled) it.clickable { onClick() } else it }
            .alpha(if (enabled) 1f else DisabledAlpha)
            // ogaRomItemLabel: one accessibility stop for the whole row, labelled from
            // the ROM's own fields. Identical treatment to ConfigurableRomItem (step 7a)
            // -- this component is what the DSiWare picker and the shortcut ROM picker
            // use, and without it TalkBack reads each row as separate text fragments.
            // No extra words: a blank developer is simply left out, not "unknown".
            .semantics(mergeDescendants = true) {
                contentDescription = listOf(item.name, item.developerName)
                    .filter { it.isNotBlank() }
                    .joinToString(", ")
            }
    ) {"""
assert old_col in s, "RomItem Column anchor not found"
s = s.replace(old_col, new_col, 1)

open(p, "w", encoding="utf-8").write(s)
print("     plain RomItem now reads as one labelled item")
PY
else
  echo "     plain RomItem: already labelled, or not found"
fi

# --- 7d. The achievement list rows (reachable from the pause menu when
# RetroAchievements is configured). Devin asked for these specifically. The shared
# RomAchievementUi row has no semantics at all, so TalkBack read it as loose fragments
# (title, description, "13/47", "250", "PTS") with nothing saying what the row is or
# whether it is unlocked. Same treatment as 7a/7c, no new words.
ACHROW="$FRONTEND/app/src/main/java/me/magnum/melonds/ui/romdetails/ui/RomAchievementUi.kt"
if [ -f "$ACHROW" ] && ! grep -q 'ogaAchievementRowLabel' "$ACHROW"; then
  python3 - "$ACHROW" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

old_imports = "import androidx.compose.ui.res.stringResource"
new_imports = """import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics"""
assert old_imports in s, "RomAchievementUi import anchor not found"
s = s.replace(old_imports, new_imports, 1)

# The label is built from strings the row ALREADY uses, resolved in composable scope
# (stringResource cannot be called inside the non-composable semantics lambda).
old_col = """    Column(
        modifier = modifier
            .focusRequester(bodyFocusRequester)"""
new_col = """    // ogaAchievementRowLabel: resolved OUTSIDE the semantics lambda, because
    // stringResource is @Composable and the semantics block is not.
    val ogaStateLabel = stringResource(
        if (isUnlocked) R.string.retro_achievements_unlocked else R.string.retro_achievements_locked
    )
    val ogaPointsLabel = stringResource(R.string.points)
    val ogaMissableLabel = if (achievement.isMissable()) stringResource(R.string.achievement_missable) else null

    Column(
        modifier = modifier
            .focusRequester(bodyFocusRequester)"""
assert old_col in s, "RomAchievementUi Column anchor not found"
s = s.replace(old_col, new_col, 1)

# ⛔ The inner content Row, NOT the outer Column: the Column owns the expand click and
# contains the "View achievement" button, and merging there would eat that button.
old_row = "        Row(Modifier.fillMaxWidth()) {"
cnt = s.count(old_row)
assert cnt == 1, f"content Row matched {cnt} times (expected 1)"
new_row = """        // ogaAchievementRowLabel: one stop for the row's information. The outer
        // Column keeps the expand click and, when expanded, the View-achievement
        // button as its own focusable action -- merging there would swallow it.
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .semantics(mergeDescendants = true) {
                    contentDescription = listOfNotNull(
                        achievement.getCleanTitle(),
                        achievement.description.takeIf { it.isNotBlank() },
                        ogaStateLabel,
                        "${achievement.points} $ogaPointsLabel",
                        if (achievementModel is AchievementUiModel.RuntimeAchievementUiModel &&
                            achievementModel.hasProgress()
                        ) {
                            "${achievementModel.runtimeAchievement.progress}/${achievementModel.runtimeAchievement.target}"
                        } else {
                            null
                        },
                        ogaMissableLabel,
                    ).joinToString(", ")
                }
        ) {"""
s = s.replace(old_row, new_row, 1)

open(p, "w", encoding="utf-8").write(s)
print("     achievement rows now read as one labelled item")
PY
else
  echo "     achievement rows: already labelled, or not found"
fi


# ---------------------------------------------------------------------------
# 9. The Nintendo DS native path (pokecore, the shared Core DS host).
#
# The DS host is NOT a new slice: it is Core/pokecore.cpp, the same
# translation unit the iOS app ships — it owns the melonDS::NDS instance,
# the embedded Lua the Pokemon Black/White reader script runs in, the
# native adapter attach (the shared registry's DS adapters), the
# announcement queue and the battle-cue snapshots. The Android side adds
# only a JNI surface over it (DsSessionJNI.cpp, riding the frontend
# target like the other accessibility hosts) — see that file's header
# for why the earlier DS starts in this tree (the MelonCore stub, the
# script-only PokeScript) could not run a game.
#
# The glue list is the DS-relevant part of scripts/core-sources.sh's
# OGA_GLUE, MINUS what Android already carries elsewhere:
#   - dissidia/dbzar/mku/khbbs_adapter.cpp, announce.cpp, osk_echo.cpp
#     already ride the oga-ppsspp-glue objects (section 8), and the
#     registry TU references them from there — compiling them again
#     here would duplicate their symbols.
#   - poke_platform.cpp is deliberately NOT compiled: it implements
#     melonDS::Platform file IO, which the frontend's PlatformAndroid
#     already provides in this library. DsSessionJNI.cpp defines the one
#     symbol pokecore needs from it (PokeSetLogForward).
#   - gba_core is NOT compiled: GB has its own Android slice, and
#     DsBackendStubs.cpp refuses its ABI (create() returns null, which
#     pokecore's load path turns into a clean refusal) — the
#     missing-symbol set was measured with nm over exactly the TUs
#     staged below, not guessed. mesen_core/snes_core ARE compiled
#     since section 10 (the MesenCE subproject below); their former
#     stubs are gone from DsBackendStubs.cpp for exactly that reason.
#
# ⛔ THE GLUE RIDES THE FRONTEND TARGET, not a subproject library (the
# PSP pattern does not transfer): pokecore shares melonDS C++ class
# ABIs with the core (NDS/ARM/JIT headers), and only the frontend
# target's exact compile definitions and include paths — the core's
# PUBLIC JIT_ENABLED, its interface include dir, the Lua headers
# section 7 added — are proven to match. A separate library compiled
# with almost-the-same flags is how a core ABI silently skews.
# ---------------------------------------------------------------------------
say "building the Nintendo DS host (pokecore) into the frontend"

if [ ! -f "$FRONTEND/app/mgba/lua/src/lua.h" ]; then
  # ⛔ NOT a silent skip — the shape section 7 established: a DS build
  # without the Lua headers section 7 staged would compile nothing and
  # ship a DS page that lies. CI always stages them.
  echo "   !! the staged Lua headers are missing (section 7 did not run?), so the DS host would NOT be built." >&2
  echo "      This stage needs section 7's wiring (MGBA_SRC and LUA_SRC set)." >&2
  exit 1
fi

DS_GLUE="$FRONTEND/app/ds-glue"
mkdir -p "$DS_GLUE"
for f in pokecore oga_core systems adapters fe_adapter fe_access \
         dbz_adapter dq9_adapter gba_adapter nes_adapter alttp_adapter \
         alttp_text alttp_names alttp_rooms n64_adapter; do
  cp "$ROOT/Core/$f.cpp" "$DS_GLUE/$f.cpp" || { echo "   !! Core/$f.cpp missing" >&2; exit 1; }
done
cp "$ROOT"/Core/*.h "$DS_GLUE/"
cp "$ROOT/Sources/CPokeCore/include/pokecore.h" "$DS_GLUE/"
echo "     staged the DS host (15 Core translation units + headers)"

CMAKE="$FRONTEND/app/CMakeLists.txt"
if grep -q 'OGA_DS_GLUE' "$CMAKE"; then
  echo "     already wired; nothing to do"
else
  python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
NL = chr(10)
tus = ["pokecore", "oga_core", "systems", "adapters", "fe_adapter",
       "fe_access", "dbz_adapter", "dq9_adapter", "gba_adapter",
       "nes_adapter", "alttp_adapter", "alttp_text", "alttp_names",
       "alttp_rooms", "n64_adapter"]
lines = [
    "",
    "# ---------------------------------------------------------------------------",
    "# OGA_DS_GLUE -- the Nintendo DS host (Core/pokecore.cpp and its glue),",
    "# appended by scripts/android-apply-overlay.sh section 9. The DS glue",
    "# rides the frontend target itself, NOT a subproject library: pokecore",
    "# shares melonDS C++ class ABIs with the core, and only this target's",
    "# exact compile definitions and include paths are proven to match it.",
    "# Skipped on armeabi-v7a, mirroring the PSP subset's stance (section 8);",
    "# the Kotlin availability probe reads the absence as 'not in this build'.",
    'if (NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")',
    "    target_sources(melonDS-android-frontend PRIVATE",
    "        ${ANDROID-CPP}/DsSessionJNI.cpp",
    "        ${ANDROID-CPP}/DsBackendStubs.cpp",
]
lines += ["        ${CMAKE_CURRENT_SOURCE_DIR}/ds-glue/%s.cpp" % t for t in tus]
lines += [
    "    )",
    "    target_include_directories(melonDS-android-frontend PRIVATE",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/ds-glue)",
    "endif ()",
    "",
]
s = s.rstrip("\n") + NL + NL.join(lines) + NL
open(p, "w", encoding="utf-8").write(s)
print("     wired the DS host into the frontend target")
PY
fi

# ---------------------------------------------------------------------------
# 9b. PlatformAndroid guards for the DS host (pokecore).
#
# pokecore drives its melonDS::NDS directly; it never runs the
# frontend's own emulator setup, so two of that setup's globals stay
# empty for the DS host's whole life — and PlatformAndroid.cpp
# dereferences both, and the NDS instance's userdata, without
# checking:
#   - Platform::SignalStop calls MelonDSAndroid::eventMessenger->
#     onEmulatorStop(...). The core calls SignalStop from NDS::Stop,
#     which pokecore's poke_stop reaches on EVERY DS quit or load —
#     an empty-shared_ptr dereference, i.e. a crash on every stop.
#   - The MP_*/Net_* callbacks (and WriteNDSSave / WriteGBASave /
#     WriteFirmware) cast the instance userdata to MelonInstance*
#     and dereference it; pokecore's NDS carries no such userdata,
#     so a guest touching local Wi-Fi would crash the same way.
#   - Addon_Rumble* and Camera_* dereference eventMessenger /
#     cameraHandler the same way.
# Core/poke_platform.cpp — the platform layer pokecore was built
# against — shows the intended behaviour: SignalStop forwards to
# pokecore's poke_internal_signal_stop, and every one of those
# callbacks is a no-op for a hostless instance. This patch gives the
# Android file the same behaviour: guards that change nothing when
# the frontend's own emulator IS running (its messenger is set and
# its userdata is a real instance), plus the SignalStop forward —
# which is itself a no-op when no pokecore core exists. The forward
# is declared weak so the armeabi-v7a build, which compiles no DS
# glue (section 9), still links.
#
# ⛔ NOT a silent skip if the anchors move: the frontend is cloned
# unpinned, and a guard patch that quietly stops applying would
# ship the crash again. Missing anchors fail the build loudly.
# ---------------------------------------------------------------------------
say "guarding PlatformAndroid for the DS host"

PLATFORM_ANDROID="$FRONTEND/app/src/main/cpp/PlatformAndroid.cpp"
if [ ! -f "$PLATFORM_ANDROID" ]; then
  echo "   !! $PLATFORM_ANDROID not found; the DS host would crash on stop." >&2
  exit 1
fi
if grep -q 'OGA (section 9b)' "$PLATFORM_ANDROID"; then
  echo "     PlatformAndroid guards: already applied"
else
  python3 - "$PLATFORM_ANDROID" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
fails = []

def rep(old, new):
    global s
    if old in s:
        s = s.replace(old, new, 1)
    else:
        fails.append(old.split("\n")[0].strip())

# The stop sink's declaration, at global scope (weak: see the header note).
rep("using namespace melonDS;",
    "using namespace melonDS;\n\n"
    "// OGA (section 9b): pokecore's stop sink, defined by the DS host\n"
    "// glue (Core/pokecore.cpp) when that glue is compiled in. Weak,\n"
    "// so builds without the DS host (armeabi-v7a) link with the\n"
    "// symbol absent and SignalStop below skips the forward.\n"
    "__attribute__((weak)) void poke_internal_signal_stop(int stopReason);")

# SignalStop: guard the messenger; forward the stop to pokecore.
rep("""    void SignalStop(StopReason reason, void* userdata)
    {
        MelonDSAndroid::eventMessenger->onEmulatorStop(reason);
    }""",
    """    void SignalStop(StopReason reason, void* userdata)
    {
        // OGA (section 9b): the messenger exists only when the
        // frontend's own emulator set it up; the DS host never does.
        // pokecore's sink is forwarded every stop and ignores the
        // ones that are not its core's, so each host hears only its
        // own console stopping.
        if (MelonDSAndroid::eventMessenger)
            MelonDSAndroid::eventMessenger->onEmulatorStop(reason);
        if (&poke_internal_signal_stop)
            poke_internal_signal_stop((int) reason);
    }""")

# Every MelonInstance userdata cast: a hostless instance (the DS
# host's NDS) has no instance to serve it, and the reference
# platform layer (Core/poke_platform.cpp) no-ops exactly these.
# The guard's return spelling follows the enclosing function —
# `return;` in a void function, `return {};` (zero) elsewhere;
# one blanket spelling compiles in only one of the two worlds.
import re
cast = "        auto emulatorInstance = (MelonDSAndroid::MelonInstance*) userdata;"
count = 0
current_ret = None
out_lines = []
for line in s.split("\n"):
    m = re.match(r"    (\w+) (\w+)\(", line)
    if m:
        current_ret = m.group(1)
    out_lines.append(line)
    if line == cast:
        count += 1
        if current_ret is None:
            fails.append("MelonInstance userdata cast (no enclosing function)")
        elif current_ret == "void":
            out_lines.append("        if (!emulatorInstance) return; // OGA (section 9b)")
        else:
            out_lines.append("        if (!emulatorInstance) return {}; // OGA (section 9b)")
if count == 0:
    fails.append("MelonInstance userdata cast")
s = "\n".join(out_lines)

# Rumble and camera globals: same empty-global shape.
rep("        MelonDSAndroid::eventMessenger->onRumbleStart(len);",
    "        if (MelonDSAndroid::eventMessenger) MelonDSAndroid::eventMessenger->onRumbleStart(len); // OGA (section 9b)")
rep("        MelonDSAndroid::eventMessenger->onRumbleStop();",
    "        if (MelonDSAndroid::eventMessenger) MelonDSAndroid::eventMessenger->onRumbleStop(); // OGA (section 9b)")
rep("        MelonDSAndroid::cameraHandler->startCamera(num);",
    "        if (MelonDSAndroid::cameraHandler) MelonDSAndroid::cameraHandler->startCamera(num); // OGA (section 9b)")
rep("        MelonDSAndroid::cameraHandler->stopCamera(num);",
    "        if (MelonDSAndroid::cameraHandler) MelonDSAndroid::cameraHandler->stopCamera(num); // OGA (section 9b)")
rep("        MelonDSAndroid::cameraHandler->captureFrame(num, frame, width, height, yuv);",
    "        if (MelonDSAndroid::cameraHandler) MelonDSAndroid::cameraHandler->captureFrame(num, frame, width, height, yuv); // OGA (section 9b)")

if fails:
    print("   !! PlatformAndroid guard anchors not found: %s" % fails, file=sys.stderr)
    sys.exit(1)
open(p, "w", encoding="utf-8").write(s)
print("     PlatformAndroid guards applied (incl. %d userdata casts)" % count)
PY
fi

# ---------------------------------------------------------------------------
# 10. The Sega Genesis native path (Genesis Plus GX).
#
# Mirrors section 8's generator shape: parse the AUDITED list out of
# scripts/core-sources.sh (GPGX_ALL — the libretro wildcard set, the
# same 54 core translation units iOS builds), copy the pinned
# Genesis-Plus-GX tree in, and emit a CMake subproject building two
# static libraries:
#   oga-gpgx         the core itself, compiled AS C (its native
#                    language — as C++ it leans on implicit void*
#                    conversions and implicit declarations strict
#                    clang refuses, and m68k.h's `#define uint`
#                    poisons C++ standard headers; the iOS build
#                    learned this first, see build-core.sh's gpgx case)
#   oga-genesis-glue Core/genesis_core.cpp (the wrapper) and
#                    Core/genesis_adapter.cpp (the registry's console
#                    row), compiled as C++ against the core through
#                    the extern "C" surface in genesis_core.h.
#
# ⛔ THE GLUE IS ITS OWN LIBRARY, NOT FRONTEND-TARGET SOURCES (the
# section 9 shape does NOT transfer): the DS glue rides the frontend
# target because pokecore shares melonDS C++ class ABIs with the
# core. The Genesis glue shares nothing with melonDS — the backend
# contract pokecore sees is a plain function-pointer struct, the
# genesis_* surface is extern "C", and oga_genesis_core()'s mangled
# name is produced by the same toolchain on both sides. What the glue
# MUST NOT share is an include path: Genesis Plus GX's headers use
# generic names (shared.h, system.h, state.h, genesis.h) that would
# shadow — and be shadowed by — the frontend core's own headers if
# the two include sets ever mixed. Its own library keeps the core's
# include roots exactly as contained as the iOS build keeps them.
# The one piece that DOES ride the frontend target is
# GenesisSessionJNI.cpp (section 1 copied it there), listed below
# beside the DS host's JNI: it speaks only the poke_* C ABI, whose
# header the DS glue already puts on that target's include path.
#
# ⛔ THE TWIN RENAMES. Genesis Plus GX exports two aggressively
# generic globals — `config` (its settings struct) and `crc32` (its
# own implementation) — plus a vendored blip_buf, the third copy of
# that library in this link (melonDS's core and mGBA vendor the other
# two). On iOS the same three collided at app link (config with
# PPSSPP's sceUsbCam, crc32 with mGBA's vendored copy) and are
# renamed on the GPGX side by scripts/gpgx-localize-twins.sh to
# oga_gpgx_config / oga_gpgx_crc32 / oga_gpgx_blip_*. The Android link
# contains the same other parties (the frontend core lib, oga-mgba,
# the PPSSPP subset), so the same renames run here, as POST_BUILD
# steps over BOTH Genesis archives — the definitions live in the glue
# (genesis_core.cpp defines config and crc32) while the core's own
# translation units hold the references, so renaming only one side
# would break the pair apart. The rename set is the iOS-measured one;
# gen/oga_rename_gpgx_twins.py is its Android form.
#
# Per-ABI gating happens INSIDE the generated CMake, where
# CMAKE_ANDROID_ARCH_ABI is known (section 8's shape): the 64-bit
# ABIs build the real libraries; armeabi-v7a — whose Genesis build
# was never measured — builds EMPTY ones, the frontend link line
# stays unconditional, the JNI surface is not compiled, and the
# Kotlin availability probe reads the absence as "not in this
# build". Genesis is absent there, honestly, like PSP and DS.
#
# THE SESSION ITSELF. GenesisGameSession drives the backend through
# pokecore (section 9's host routes .md/.gen/.smd/.bin loads to it),
# so this section adds no host of its own — only the core, its glue,
# and the JNI surface. There is NO Genesis reader: the registry row
# refuses to attach, and the session ships pad, states, battery
# saves, audio and the game picture only.
# ---------------------------------------------------------------------------
say "building the Genesis core (Genesis Plus GX) for Android"

GPGX_SRC="${GPGX_SRC:-$HOME/src/genesis-plus-gx}"

if [ ! -d "$GPGX_SRC/core" ]; then
  # Same rule as sections 7/8: a silent skip is how a core goes missing.
  if [ "${OGA_ALLOW_NO_GENESIS:-0}" = "1" ]; then
    echo "   Genesis core SKIPPED (OGA_ALLOW_NO_GENESIS=1)"
  else
    echo "   !! no Genesis Plus GX source at $GPGX_SRC, so the Genesis core would NOT be built." >&2
    echo "      Run scripts/bootstrap-deps.sh, or set GPGX_SRC, or set" >&2
    echo "      OGA_ALLOW_NO_GENESIS=1 to build without Genesis on purpose." >&2
    exit 1
  fi
else
  mkdir -p "$FRONTEND/app/genesis"
  rm -rf "$FRONTEND/app/genesis/src"
  cp -r "$GPGX_SRC" "$FRONTEND/app/genesis/src"
  # The checkout's .git is not build input (same rule as the mGBA copy).
  rm -rf "$FRONTEND/app/genesis/src/.git"
  if [ ! -f "$FRONTEND/app/genesis/src/core/genesis.c" ]; then
    echo "   !! the copied Genesis Plus GX tree is not usable (no core/)" >&2; exit 1
  fi
  # ⛔ NO GAME IMAGES RIDE ALONG. The pinned tree ships none; if one
  # ever appears in it, the build stops here rather than shipping a
  # ROM inside the APK (the repo guard scans the build tree for the
  # same extensions). .md is deliberately NOT in this list: it is the
  # tree's documentation extension as much as a cartridge one, and a
  # Genesis cartridge file in the tree would be caught by the .gen /
  # .smd / .bin arms or the CI scan, not by eating the READMEs.
  if find "$FRONTEND/app/genesis/src" -type f \( -iname '*.gen' -o -iname '*.smd' -o -iname '*.bin' -o -iname '*.sav' -o -iname '*.sms' -o -iname '*.gg' -o -iname '*.sg' \) 2>/dev/null | grep -q .; then
    echo "   !! game image files survived in the copied Genesis Plus GX tree" >&2; exit 1
  fi
  echo "     Genesis Plus GX source copied (pinned $(git -C "$GPGX_SRC" rev-parse --short HEAD 2>/dev/null || echo unknown)); ROM check asserted"

  # The glue: OGA code, copied in like section 8's psp_core glue.
  # glue/gpgx holds ONLY Core/gpgx/osd.h — the frontend shim every
  # core TU includes as "osd.h" — in its own directory, FIRST on the
  # core's include path, exactly the iOS arrangement (build-core.sh's
  # GPGX_INC): the shim must win over nothing and be found before
  # anything else named osd.h could be.
  mkdir -p "$FRONTEND/app/genesis/glue/gpgx" "$FRONTEND/app/genesis/gen"
  cp "$ROOT/Core/genesis_core.cpp" "$ROOT/Core/genesis_adapter.cpp" \
     "$FRONTEND/app/genesis/glue/"
  cp "$ROOT/Core/gpgx/osd.h" "$FRONTEND/app/genesis/glue/gpgx/osd.h"
  cp "$ROOT"/Core/*.h "$FRONTEND/app/genesis/glue/"
  cp "$ROOT/Sources/CPokeCore/include/pokecore.h" "$FRONTEND/app/genesis/glue/"
  echo "     added the genesis_core glue + the osd.h frontend shim"

  # The v7a stub TU (see the header comment): a real file, so the
  # generated CMake can name it unconditionally per-ABI.
  printf '// Generated by scripts/android-apply-overlay.sh.\n// armeabi-v7a gets empty Genesis libraries: the Genesis Plus GX\n// Android build was never measured for 32-bit ARM. Genesis is\n// absent there, and the Kotlin probe says so.\n' \
    > "$FRONTEND/app/genesis/gen/oga_genesis_empty.cpp"

  # The twin rename script (see the generated CMakeLists): it runs as
  # a POST_BUILD step on each Genesis archive.
  cat > "$FRONTEND/app/genesis/gen/oga_rename_gpgx_twins.py" <<'PYEOF'
#!/usr/bin/env python3
# oga_rename_gpgx_twins.py -- generated by scripts/android-apply-overlay.sh.
#
# Rename the twin symbols in the Genesis archives:
#
#   * `config` -- Genesis Plus GX's settings struct, defined by the
#     glue (genesis_core.cpp's frontend shim) and referenced by every
#     core TU. PPSSPP's subset defines its own `config` (sceUsbCam).
#   * `crc32` -- the shim's own implementation. mGBA vendors another.
#   * `blip_*` -- the core's vendored blip_buf: the THIRD copy of
#     that library in this .so (melonDS's core and mGBA vendor the
#     other two).
#
# Every matching symbol in each file given -- definitions and
# references alike -- gets the oga_gpgx_ prefix, so Genesis code
# binds Genesis's copies and the other cores keep theirs. Runs as a
# POST_BUILD step on each Genesis archive, each file self-contained,
# so incremental rebuilds stay renamed. Idempotent: prefixed names
# no longer match. The set is the one scripts/gpgx-localize-twins.sh
# measured for the iOS link; the other first-wins twins inside the
# core are its own upstream code and collide with nobody.
import os
import subprocess
import sys

EXACT = ("config", "crc32")
PREFIXES = ("blip_",)


def rename_in(path, objcopy, nm):
    if not os.path.exists(path):
        print("oga-genesis: rename target does not exist: %s" % path)
        sys.exit(1)
    proc = subprocess.run([nm, path], capture_output=True, text=True)
    if proc.returncode != 0:
        print("oga-genesis: nm failed on %s: %s" % (path, proc.stderr.strip()))
        sys.exit(1)
    names = set()
    for line in proc.stdout.splitlines():
        parts = line.split()
        if parts and (parts[-1] in EXACT or parts[-1].startswith(PREFIXES)):
            names.add(parts[-1])
    if not names:
        print("oga-genesis: no twin symbols in %s" % os.path.basename(path))
        return
    map_path = path + ".renames"
    with open(map_path, "w", encoding="utf-8") as fh:
        for n in sorted(names):
            fh.write("%s oga_gpgx_%s\n" % (n, n))
    subprocess.run([objcopy, "--redefine-syms", map_path, path], check=True)
    print("oga-genesis: renamed %d twin symbols in %s"
          % (len(names), os.path.basename(path)))


def main():
    objcopy, args = sys.argv[1], sys.argv[2:]
    nm = os.path.join(os.path.dirname(objcopy), "llvm-nm")
    for path in args:
        rename_in(path, objcopy, nm)


main()
PYEOF
  echo "     generated the twin-symbol rename script"

  python3 - "$ROOT/scripts/core-sources.sh" "$FRONTEND/app/genesis/CMakeLists.txt" "$FRONTEND/app/genesis" <<'PY'
import os, re, sys
srcs = open(sys.argv[1], encoding="utf-8").read()
out_path, genesis_dir = sys.argv[2], sys.argv[3]

m = re.search(r'^GPGX_ALL="(.*?)"\n', srcs, re.S | re.M)
assert m, "no GPGX_ALL list in " + sys.argv[1]
files = [f for f in m.group(1).split() if f.endswith('.c')]
assert files, "GPGX_ALL came back empty -- refusing to generate a broken CMakeLists"

# Fail at generation time, not at some later link: every named file
# must exist in the copied tree.
tree = os.path.join(genesis_dir, "src")
for f in files:
    assert os.path.isfile(os.path.join(tree, f)), "missing from the Genesis Plus GX tree: " + f

NL = chr(10)
with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("""# Generated by scripts/android-apply-overlay.sh -- do not edit.
#
# Genesis Plus GX (the Sega Genesis / Mega Drive core) plus the OGA
# glue for it, built from the AUDITED source list in
# scripts/core-sources.sh (GPGX_ALL -- the libretro wildcard set, the
# same 54 core translation units the iOS build compiles). Upstream's
# own build files are not used: the libretro Makefile targets a
# shared object with its own frontend, and this app needs the bare
# core behind Core/genesis_core.cpp's wrapper.
#
# The core compiles AS C (its native language; see the overlay's
# section 10 for what C++ does to it) with the defines the iOS build
# uses. The glue compiles as C++ and speaks to the C objects through
# genesis_core.h's extern "C" declarations, with the standard headers
# included before the core's (m68k.h's `#define uint` poisons them
# otherwise -- genesis_core.cpp already observes that order).
#
# Per-ABI gating (section 8's shape): armeabi-v7a builds EMPTY
# libraries -- the Genesis Android build was never measured for
# 32-bit ARM -- so the frontend link line stays unconditional and the
# Kotlin availability probe answers "not in this build" there.

cmake_minimum_required(VERSION 3.22)
project(oga-genesis C CXX)

if (CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    add_library(oga-gpgx STATIC gen/oga_genesis_empty.cpp)
    add_library(oga-genesis-glue STATIC gen/oga_genesis_empty.cpp)
else ()
    set(GPGX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

    set(GPGX_SOURCES
""")
    for f in files:
        fh.write("        ${GPGX_SRC_DIR}/%s\n" % f)
    fh.write("""    )

    # The include roots are the iOS build's (build-core.sh's GPGX_INC),
    # with the glue's gpgx/ directory -- holding ONLY our osd.h, the
    # frontend shim every core TU includes -- FIRST: no other header
    # of ours may be visible to the core, and no core header may leak
    # to the frontend target (the isolation section 10 exists for).
    set(OGA_GPGX_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/glue/gpgx
        ${GPGX_SRC_DIR}/core
        ${GPGX_SRC_DIR}/core/z80
        ${GPGX_SRC_DIR}/core/m68k
        ${GPGX_SRC_DIR}/core/ntsc
        ${GPGX_SRC_DIR}/core/sound
        ${GPGX_SRC_DIR}/core/sound/minimp3
        ${GPGX_SRC_DIR}/core/input_hw
        ${GPGX_SRC_DIR}/core/cd_hw
        ${GPGX_SRC_DIR}/core/cart_hw
        ${GPGX_SRC_DIR}/core/cart_hw/svp
    )
    set(OGA_GPGX_DEFS LSB_FIRST BYTE_ORDER=LITTLE_ENDIAN USE_32BPP_RENDERING)

    add_library(oga-gpgx STATIC ${GPGX_SOURCES})
    set_target_properties(oga-gpgx PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_include_directories(oga-gpgx PRIVATE ${OGA_GPGX_INC})
    target_compile_definitions(oga-gpgx PRIVATE ${OGA_GPGX_DEFS})
    target_compile_options(oga-gpgx PRIVATE -w)

    add_library(oga-genesis-glue STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/glue/genesis_core.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/glue/genesis_adapter.cpp
    )
    target_include_directories(oga-genesis-glue PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/glue
        ${OGA_GPGX_INC}
    )
    target_compile_definitions(oga-genesis-glue PRIVATE ${OGA_GPGX_DEFS})
    target_compile_options(oga-genesis-glue PRIVATE -w)

    # The twin renames (see gen/oga_rename_gpgx_twins.py): POST_BUILD
    # on EACH archive -- the definitions live in the glue, the
    # references in the core, so both sides move together.
    add_custom_command(TARGET oga-gpgx POST_BUILD
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/oga_rename_gpgx_twins.py
                ${CMAKE_OBJCOPY} $<TARGET_FILE:oga-gpgx>)
    add_custom_command(TARGET oga-genesis-glue POST_BUILD
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/oga_rename_gpgx_twins.py
                ${CMAKE_OBJCOPY} $<TARGET_FILE:oga-genesis-glue>)
endif ()
""")
print("     generated the Genesis build (%d audited core sources + glue)" % len(files))
PY

  # ---- wire it into the frontend ----
  python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

if "OGA_GENESIS" in s:
    print("     already wired; nothing to do")
    sys.exit(0)

NL = chr(10)

# 1. build the Genesis subproject, before the core's add_subdirectory
#    (the order sections 7/8 use; what matters is that it precedes the
#    frontend target's link).
anchor = "add_subdirectory(${CORE-LIB} ./melonDS-android-lib)"
if anchor not in s:
    print("     !! core add_subdirectory anchor not found"); sys.exit(1)
intro = NL.join([
    "# The Genesis core (Genesis Plus GX). Built from the audited source",
    "# list, not upstream's libretro build -- see",
    "# scripts/android-apply-overlay.sh section 10 for why.",
    "add_subdirectory(genesis ./oga-genesis)",
    "",
])
s = s.replace(anchor, intro + anchor, 1)

# 2. link the archives, glue first (its genesis_* references resolve
#    out of the core archive behind it). The link line's exact tail
#    depends on which earlier sections ran (mGBA always; the PSP
#    subset unless it was skipped on purpose), so the pair is hung on
#    the line's closing paren rather than a fixed spelling.
lines = s.split(NL)
for i, line in enumerate(lines):
    if line.startswith("target_link_libraries(melonDS-android-frontend"):
        assert line.rstrip().endswith(")"), "frontend link line not recognised"
        lines[i] = line.rstrip()[:-1] + " oga-genesis-glue oga-gpgx)"
        break
else:
    print("     !! frontend link line not found"); sys.exit(1)
s = NL.join(lines)

# 3. the JNI surface rides the frontend target (section 9's shape for
#    the DS host's JNI): it speaks only the poke_* C ABI, whose header
#    section 9's include line already provides. 64-bit only, like the
#    libraries themselves.
block = NL.join([
    "",
    "# ---------------------------------------------------------------------------",
    "# OGA_GENESIS -- the Genesis session's JNI surface (GenesisSessionJNI.cpp),",
    "# appended by scripts/android-apply-overlay.sh section 10. It rides the",
    "# frontend target like the DS host's JNI (section 9): the poke_* ABI it",
    "# speaks is compiled into this same target by the DS glue. Skipped on",
    "# armeabi-v7a, mirroring the libraries' own gating; the Kotlin",
    "# availability probe reads the absence as 'not in this build'.",
    'if (NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")',
    "    target_sources(melonDS-android-frontend PRIVATE",
    "        ${ANDROID-CPP}/GenesisSessionJNI.cpp",
    "    )",
    "endif ()",
    "",
])
s = s.rstrip("\n") + NL + block + NL
open(p, "w", encoding="utf-8").write(s)
print("     wired the Genesis core into the frontend (archives linked; JNI in the target)")
PY

fi
# ---------------------------------------------------------------------------
# 11. The MesenCE cores (NES / SNES) as a subproject, plus the session
#     surface's wiring.
#
# WHAT THIS BUILDS. Core/pokecore.cpp — the shared multi-console host
# the DS section compiles into the frontend target — routes NES and
# SNES files to Core/mesen_core.cpp and Core/snes_core.cpp, the
# MesenCE glue iOS ships. Until this section, Android linked those
# branches against honest stubs (DsBackendStubs.cpp) because nothing
# compiled the MesenCE tree; the stubs are gone now, and this section
# builds the real thing:
#
#   - the WHOLE Mesen core (MESEN_CORE_ALL from scripts/core-sources.sh
#     — Mesen's own Core.vcxproj list), because Shared/Emulator.cpp
#     constructs every console Mesen supports with no per-console
#     guard: a NES-only list cannot LINK, however cleanly it compiles
#     (the core-sources.sh note tells the whole story). Plus
#     MESEN_UTILS and MESEN_C (spng).
#   - the glue (mesen_core.cpp, snes_core.cpp) with the host build's
#     flags (scripts/build-core.sh's mesen case): the tree root, Core
#     and Utilities on the include path, pch.h forced into every C++
#     TU — Mesen builds every Core TU that way, so a build that omits
#     it compiles a different program — and -fwrapv /
#     -fno-strict-aliasing, which Mesen requires. NO melonDS include
#     dirs: its src/CRC32.h would shadow Mesen's own CRC32.h (the host
#     build's warning, kept).
#   - the app's ONE Lua: Mesen's interpreter in this embedding is the
#     shared lua-5.4.7 (its objects ride the oga-mgba archive), and
#     Core/mesen_lua_extras.c satisfies the four symbols Mesen's Lua
#     fork adds — compiled as its OWN object library because its
#     include set is the exception: Mesen's fork headers must WIN
#     there, so it carries exactly its includes and no others (the
#     host build's mesenlua case, which likewise stands alone).
#   - MESEN_SEVENZIP_C as an OBJECT library: the audited difference
#     set of Mesen's 7-Zip SDK (the files the other vendored copies
#     lack), whose 7zStream.o is post-processed before the link by
#     scripts/android-mesen-7z-dedupe.sh — Android links TWO other
#     vendored 7-Zip copies (mGBA's lzma, PPSSPP's lzma-sdk), and the
#     dedupe localizes in Mesen's object exactly the extern symbols
#     the union of the others defines, keeping LookToRead_* exported
#     (SZReader's only source for them). See that script's header.
#
# A SUBPROJECT LIBRARY, NOT FRONTEND-TARGET SOURCES (the mGBA/PPSSPP
# shape, not section 9's): unlike pokecore, the Mesen glue shares no
# C++ class ABI with the melonDS core — it is a self-contained C ABI
# over the Mesen tree — so it gets its own target with its own flags,
# and the ABI-skew risk section 9 avoids does not arise. The wiring
# block at the end links it into the frontend, compiles
# MesenSessionJNI.cpp there (the JNI surface is a frontend-target
# citizen like the other sessions'), and puts the glue headers on the
# frontend's include path, where the DS-staged oga_core.cpp reaches
# mesen_core.h / snes_core.h.
#
# Skipped on armeabi-v7a like sections 8 and 9; the Kotlin probe reads
# the absence as "not in this build".
# ---------------------------------------------------------------------------
say "building the MesenCE cores (NES / SNES) for Android"

MESEN_SRC="${MESEN_SRC:-}"
if [ -z "$MESEN_SRC" ]; then
  # ⛔ NOT a silent skip — the shape section 7 established: a build
  # without the Mesen cores would ship NES/SNES pages that lie. CI
  # always sets it (bootstrap-deps.sh fetches the pinned tree). Set
  # OGA_ALLOW_NO_MESEN=1 to build without them deliberately.
  if [ "${OGA_ALLOW_NO_MESEN:-0}" = "1" ]; then
    echo "   MesenCE cores SKIPPED (OGA_ALLOW_NO_MESEN=1)"
  else
    echo "   !! MESEN_SRC not set, so the NES/SNES cores would NOT be built." >&2
    echo "      Set MESEN_SRC (scripts/bootstrap-deps.sh fetches the pinned MesenCE tree)," >&2
    echo "      or set OGA_ALLOW_NO_MESEN=1 to build without them on purpose." >&2
    exit 1
  fi
else
  [ -d "$MESEN_SRC/Core" ] || { echo "   !! no MesenCE source at $MESEN_SRC" >&2; exit 1; }

  # The source tree must be reachable from the Gradle/CMake build, so
  # copy it in, exactly as sections 7 and 8 copy theirs (minus .git).
  mkdir -p "$FRONTEND/app/mesen"
  rm -rf "$FRONTEND/app/mesen/src" "$FRONTEND/app/mesen/glue"
  cp -r "$MESEN_SRC" "$FRONTEND/app/mesen/src"
  rm -rf "$FRONTEND/app/mesen/src/.git"
  # The glue and the Core headers it (and the frontend's DS-staged
  # oga_core.cpp) include.
  mkdir -p "$FRONTEND/app/mesen/glue"
  cp "$ROOT/Core/mesen_core.cpp" "$ROOT/Core/snes_core.cpp" \
     "$ROOT/Core/mesen_lua_extras.c" "$FRONTEND/app/mesen/glue/"
  cp "$ROOT"/Core/*.h "$FRONTEND/app/mesen/glue/"
  # The 7z dedupe rides along: the generated CMakeLists runs it at
  # build time, and the frontend tree is all the build can see.
  cp "$ROOT/scripts/android-mesen-7z-dedupe.sh" "$FRONTEND/app/mesen/"
  echo "     MesenCE source copied; glue and headers staged"

  python3 - "$ROOT/scripts/core-sources.sh" "$FRONTEND/app/mesen/CMakeLists.txt" <<'PY'
import re, sys
srcs = open(sys.argv[1], encoding="utf-8").read()

# ⛔ THE MESEN LISTS ARE READ FROM scripts/core-sources.sh, NOT WRITTEN
# HERE — the section 7 rule, for the section 7 reason: iOS builds these
# exact lists, and a hand copy here would drift into a different
# emulator (or, worse, a subset that compiles and cannot link — see
# the MESEN_CORE_ALL note in core-sources.sh).
def _oga_list(name, suffix):
    m = re.search(r'^' + name + r'="(.*?)"\n', srcs, re.S | re.M)
    assert m, "no " + name + " list in " + sys.argv[1]
    return [f for f in m.group(1).split() if f.endswith(suffix)]

CORE_CPP = _oga_list("MESEN_CORE_ALL", ".cpp")
UTILS_CPP = _oga_list("MESEN_UTILS", ".cpp")
MESEN_C = _oga_list("MESEN_C", ".c")
SEVENZIP_C = _oga_list("MESEN_SEVENZIP_C", ".c")
assert CORE_CPP and UTILS_CPP and MESEN_C and SEVENZIP_C, \
    "a Mesen list came back empty -- refusing to generate a broken CMakeLists"
assert "SevenZip/7zStream.c" in SEVENZIP_C, \
    "7zStream.c missing from MESEN_SEVENZIP_C -- SZReader would lose LookToRead_*"
assert "Utilities/spng.c" in MESEN_C, "spng.c missing from MESEN_C"

L = []
L.append("# Generated by scripts/android-apply-overlay.sh section 11 -- do not edit.")
L.append("#")
L.append("# The MesenCE cores for Android: the WHOLE Mesen core (the audited")
L.append("# MESEN_CORE_ALL list from scripts/core-sources.sh — Mesen's own")
L.append("# Core.vcxproj list, because Shared/Emulator.cpp constructs every")
L.append("# console and a subset cannot link), its Utilities, spng, the 7-Zip")
L.append("# difference set, and the OGA glue (Core/mesen_core.cpp,")
L.append("# Core/snes_core.cpp, Core/mesen_lua_extras.c). The flags are the")
L.append("# host build's (scripts/build-core.sh's mesen cases).")
L.append("")
L.append("cmake_minimum_required(VERSION 3.22)")
L.append("project(oga-mesen C CXX)")
L.append("")
L.append("set(CMAKE_C_STANDARD 11)")
L.append("set(CMAKE_CXX_STANDARD 17)")
L.append("set(CMAKE_CXX_STANDARD_REQUIRED ON)")
L.append("")
L.append("set(MESEN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)")
L.append("set(MESEN_GLUE ${CMAKE_CURRENT_SOURCE_DIR}/glue)")
L.append("# The app's ONE Lua (5.4.7), staged by section 7 beside the mGBA")
L.append("# tree: Mesen's interpreter in this embedding IS that Lua — the")
L.append("# glue links its symbols from the oga-mgba archive — and")
L.append("# mesen_lua_extras.c satisfies the fork-only symbols. Mesen's own")
L.append("# Lua fork contributes headers to exactly one object library")
L.append("# (oga-mesen-lua-extras below).")
L.append("set(OGA_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../mgba/lua/src)")
L.append("")
L.append("if (NOT CMAKE_NM)")
L.append("    find_program(CMAKE_NM llvm-nm)")
L.append("endif ()")
L.append("if (NOT CMAKE_OBJCOPY)")
L.append("    find_program(CMAKE_OBJCOPY llvm-objcopy)")
L.append("endif ()")
L.append("")
L.append("# Mesen's 7-Zip SDK difference set (MESEN_SEVENZIP_C). An OBJECT")
L.append("# library, never archived: the dedupe step below rewrites its")
L.append("# 7zStream.c.o in place before the frontend links.")
L.append("add_library(oga-mesen-sevenzip OBJECT")
for f in SEVENZIP_C:
    L.append("    ${MESEN_SRC}/" + f)
L.append(")")
L.append("target_include_directories(oga-mesen-sevenzip PRIVATE")
L.append("    ${MESEN_SRC} ${MESEN_SRC}/Utilities)")
L.append("")
L.append("# The 7-Zip dedupe: localize in Mesen's 7zStream.o the extern")
L.append("# symbols the OTHER vendored copies (mGBA's lzma, PPSSPP's")
L.append("# lzma-sdk) also define, keeping LookToRead_* exported. Runs")
L.append("# after every object it reads exists; the frontend target is")
L.append("# made to depend on it by the wiring block section 11 appends")
L.append("# to the frontend's CMakeLists, so the link sees the rewritten")
L.append("# object. See scripts/android-mesen-7z-dedupe.sh.")
L.append("add_custom_target(oga-mesen-7z-dedupe")
L.append("    COMMAND ${CMAKE_COMMAND} -E env \"OGA_NM=${CMAKE_NM}\" \"OGA_OBJCOPY=${CMAKE_OBJCOPY}\"")
L.append("            bash ${CMAKE_CURRENT_SOURCE_DIR}/android-mesen-7z-dedupe.sh ${CMAKE_BINARY_DIR}")
L.append("    DEPENDS oga-mesen-sevenzip")
L.append("    VERBATIM)")
L.append("if (TARGET oga-mgba)")
L.append("    add_dependencies(oga-mesen-7z-dedupe oga-mgba)")
L.append("endif ()")
L.append("if (TARGET oga-ppsspp)")
L.append("    add_dependencies(oga-mesen-7z-dedupe oga-ppsspp)")
L.append("endif ()")
L.append("")
L.append("# The four symbols Mesen's Lua fork adds over the app's Lua,")
L.append("# satisfied without a second interpreter (Core/mesen_lua_extras.c).")
L.append("# Its OWN object library because its include set is the exception:")
L.append("# Mesen's fork headers must WIN here (lua_WatchDogHook and the")
L.append("# sandbox global live in the fork), so this target carries exactly")
L.append("# its includes and no others — the host build's mesenlua case,")
L.append("# which likewise gives it no shared include set.")
L.append("add_library(oga-mesen-lua-extras OBJECT ${MESEN_GLUE}/mesen_lua_extras.c)")
L.append("target_include_directories(oga-mesen-lua-extras PRIVATE")
L.append("    ${MESEN_SRC}/Lua ${MESEN_SRC}/Core ${OGA_LUA_SRC} ${MESEN_GLUE})")
L.append("target_compile_definitions(oga-mesen-lua-extras PRIVATE LUA_USE_POSIX)")
L.append("")
L.append("add_library(oga-mesen STATIC")
for f in CORE_CPP + UTILS_CPP:
    L.append("    ${MESEN_SRC}/" + f)
for f in MESEN_C:
    L.append("    ${MESEN_SRC}/" + f)
L.append("    ${MESEN_GLUE}/mesen_core.cpp")
L.append("    ${MESEN_GLUE}/snes_core.cpp")
L.append(")")
L.append("# Mesen's own build shape (the host build's mesen case): the tree")
L.append("# root, Core and Utilities on the path, pch.h forced into every")
L.append("# C++ TU, the app Lua's headers, and the -fwrapv /")
L.append("# -fno-strict-aliasing Mesen requires. NO melonDS include dirs:")
L.append("# its src/CRC32.h would shadow Mesen's own CRC32.h.")
L.append("target_include_directories(oga-mesen PRIVATE")
L.append("    ${MESEN_SRC} ${MESEN_SRC}/Core ${MESEN_SRC}/Utilities")
L.append("    ${OGA_LUA_SRC} ${MESEN_GLUE})")
L.append("target_compile_options(oga-mesen PRIVATE")
L.append("    $<$<COMPILE_LANGUAGE:CXX>:-include${MESEN_SRC}/Core/pch.h>")
L.append("    $<$<COMPILE_LANGUAGE:CXX>:-fwrapv>")
L.append("    $<$<COMPILE_LANGUAGE:CXX>:-fno-strict-aliasing>)")
L.append("target_link_libraries(oga-mesen PUBLIC oga-mesen-sevenzip oga-mesen-lua-extras)")
L.append("")
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(L) + "\n")
print("     generated the oga-mesen CMakeLists (%d core, %d utils, %d C, %d sevenzip)"
      % (len(CORE_CPP), len(UTILS_CPP), len(MESEN_C), len(SEVENZIP_C)))
PY

  CMAKE="$FRONTEND/app/CMakeLists.txt"
  if grep -q 'OGA_MESEN' "$CMAKE"; then
    echo "     already wired; nothing to do"
  else
    python3 - "$CMAKE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
NL = chr(10)
lines = [
    "",
    "# ---------------------------------------------------------------------------",
    "# OGA_MESEN -- the MesenCE cores (NES / SNES), appended by",
    "# scripts/android-apply-overlay.sh section 11. A subproject library",
    "# (add_subdirectory), NOT frontend-target sources: the Mesen glue is a",
    "# self-contained C ABI over the Mesen tree and shares no C++ class ABI",
    "# with the melonDS core, so section 9's ride-the-frontend-target rule",
    "# does not apply. The JNI surface rides the frontend target like the",
    "# other sessions' surfaces, and the glue headers go on its include",
    "# path for the DS-staged oga_core.cpp. The frontend depends on the",
    "# 7-Zip dedupe so the link sees the rewritten 7zStream object.",
    "# Skipped on armeabi-v7a, mirroring sections 8 and 9; the Kotlin",
    "# availability probe reads the absence as 'not in this build'.",
    'if (NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")',
    "    add_subdirectory(mesen ./oga-mesen)",
    "    target_link_libraries(melonDS-android-frontend oga-mesen)",
    "    add_dependencies(melonDS-android-frontend oga-mesen-7z-dedupe)",
    "    target_sources(melonDS-android-frontend PRIVATE",
    "        ${ANDROID-CPP}/MesenSessionJNI.cpp)",
    "    target_include_directories(melonDS-android-frontend PRIVATE",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/mesen/glue)",
    "endif ()",
    "",
]
s = s.rstrip("\n") + NL + NL.join(lines) + NL
open(p, "w", encoding="utf-8").write(s)
print("     wired the MesenCE cores into the frontend target")
PY
  fi
fi

say "overlay applied"
