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
                  '    <string name="oga_app_name">Pokemon Access</string>\n</resources>')
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

# The audited list is GBA-only. These two sets complete it for this build.
GB_EXTRA = [
    "src/gb/cheats.c", "src/gb/core.c", "src/gb/gb.c", "src/gb/input.c",
    "src/gb/io.c", "src/gb/mbc.c", "src/gb/mbc/huc-3.c", "src/gb/mbc/licensed.c",
    "src/gb/mbc/mbc.c", "src/gb/mbc/pocket-cam.c", "src/gb/mbc/tama5.c",
    "src/gb/mbc/unlicensed.c", "src/gb/memory.c", "src/gb/overrides.c",
    "src/gb/serialize.c", "src/gb/renderers/cache-set.c",
    "src/gb/renderers/software.c", "src/gb/sio.c", "src/gb/timer.c",
    "src/gb/video.c", "src/gb/sio/lockstep.c", "src/gb/sio/printer.c",
    "src/gb/extra/proxy.c",
    # The GB debugger interface, pulled in by the core (ENABLE_DEBUGGERS). From
    # mGBA's src/gb/debugger/.
    "src/gb/debugger/cli.c", "src/gb/debugger/debugger.c",
    "src/gb/debugger/symbols.c",
]
# The SM83 (Game Boy CPU) core. From mGBA's src/sm83/CMakeLists.txt; only needed
# because M_CORE_GB is defined.
SM83_EXTRA = [
    "src/sm83/decoder.c", "src/sm83/isa-sm83.c", "src/sm83/sm83.c",
    "src/sm83/debugger/debugger.c", "src/sm83/debugger/cli-debugger.c",
    "src/sm83/debugger/memory-debugger.c",
]
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
        "# --- Game Boy core (mGBA's own src/gb/CMakeLists.txt list). I added",
        "#     M_CORE_GB, which registers the GB core; without these it is",
        "#     referenced but never defined. ---",
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

say "overlay applied"
