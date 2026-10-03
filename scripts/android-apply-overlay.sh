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
say "forcing the core's JIT on for 64-bit ABIs"
GRADLE="$FRONTEND/app/build.gradle.kts"
if grep -q 'ogaJitArchitecture\|ARCHITECTURE=' "$GRADLE"; then
  echo "   already set; nothing to do"
else
  python3 - "$GRADLE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = '''        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17 -Wno-write-strings")
            }
        }'''
new = '''        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17 -Wno-write-strings")
                // JIT IS ON BY DEFAULT FOR arm64, BUT ONLY IF THE CORE KNOWS IT
                // IS arm64. melonDS derives ARCHITECTURE from a compiler-macro
                // probe (check_symbol_exists on __aarch64__), which is unreliable
                // when cross-compiling; if it comes back empty, ENABLE_JIT
                // silently falls to OFF and the emulator runs the interpreter.
                // Pass it explicitly, and require the JIT. Measured on melonDS's
                // own option: ARM64 -> ON/ARMJIT_A64, ARM -> OFF.
                arguments.apply {
                    add("-DCMAKE_BUILD_TYPE=Release")
                    add("-DENABLE_JIT=ON")
                    val abi = (findProperty("ogaJitArchitecture") as String?)
                        ?: "ARM64"
                    add("-DARCHITECTURE=$abi")
                }
            }
        }'''
if old not in s:
    print("   !! could not find the cppFlags-only externalNativeBuild block")
    sys.exit(1)
open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))
print("   added CMAKE_BUILD_TYPE=Release, ENABLE_JIT=ON and ARCHITECTURE")
PY
fi

# ---------------------------------------------------------------------------
# 6. androidx.webkit: our MainActivity uses WebViewAssetLoader, which upstream
#    does not depend on. Without it the Kotlin step fails with
#    "Unresolved reference 'webkit'".
# ---------------------------------------------------------------------------
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

say "overlay applied"
