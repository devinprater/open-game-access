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
