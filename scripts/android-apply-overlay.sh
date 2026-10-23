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
# No USE_FFMPEG yet: the subset compiles and boots without it (FFmpeg's
# Android slices are a later stage). -DMOBILE_DEVICE matches the iOS
# ppspp/ppsppc cases in build-core.sh.
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
    target_compile_definitions(oga-ppsspp-glue PRIVATE MOBILE_DEVICE)
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
# arithmetic flags the subset is proven with. No USE_FFMPEG yet.
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

say "overlay applied"
