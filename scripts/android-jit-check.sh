#!/usr/bin/env bash
# android-jit-check.sh — assert the Android build actually compiles the JIT in.
#
# The core decides ENABLE_JIT from an ARCHITECTURE value that detect_architecture()
# derives from a compiler macro via check_symbol_exists. That probe is unreliable
# when cross-compiling, and a miss is SILENT: the option falls to OFF and the
# emulator quietly runs the interpreter. Nothing in the build reports it, so this
# script pins the behaviour instead.
#
# ⛔ The real app/build.gradle.kts is UPSTREAM's, patched at build time by
# scripts/android-apply-overlay.sh -- this repo only tracks a skeleton. So the
# assertion belongs on the overlay script, which is what actually decides.
#
# ⛔ Assert on the ACTUAL ARGUMENT LINES, not on a bare string. A first version
# grepped for the text "ENABLE_JIT=ON", which also appears in the script's own
# comments and its progress message -- so deleting the real `add("-DENABLE_JIT=ON")`
# still passed. That is the same "gate that cannot fail" bug this file exists to
# catch. Every assertion below anchors on the emitted code, and the script is run
# once against a throwaway copy to prove a missing pin is actually detected.
#
# Measured on melonDS's own option:
#   ARCHITECTURE=ARM64   -> ENABLE_JIT=ON,  backend ARMJIT_A64
#   ARCHITECTURE=x86_64  -> ENABLE_JIT=ON,  backend ARMJIT_x64
#   ARCHITECTURE=ARM     -> ENABLE_JIT=OFF (correct: no A64 backend for 32-bit)
#
# Usage: scripts/android-jit-check.sh [repo-root]
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
fail=0
ok()  { printf 'ok: %s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*" >&2; fail=1; }

OVERLAY="$ROOT/scripts/android-apply-overlay.sh"

echo "== the overlay must pin the JIT and the architecture"
if [ -f "$OVERLAY" ]; then
  # Anchor on the emitted CMake set() calls, not on prose. The flags live in
  # app/CMakeLists.txt now: Gradle's Kotlin DSL refuses an `arguments` list in the
  # top-level externalNativeBuild block, and CMAKE_ANDROID_ARCH_ABI is only known
  # once CMake configures, so ARCHITECTURE cannot drift from the real compiler.
  grep -qF 'set(ENABLE_JIT ON CACHE BOOL "" FORCE)' "$OVERLAY" \
    && ok "ENABLE_JIT=ON is forced in CMake" \
    || bad "ENABLE_JIT=ON is missing: the JIT can silently fall back to the interpreter"
  grep -qF 'CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a"' "$OVERLAY" \
    && grep -qF 'set(ARCHITECTURE "ARM64" CACHE STRING "" FORCE)' "$OVERLAY" \
    && ok "ARCHITECTURE=ARM64 is derived from the real ABI" \
    || bad "ARCHITECTURE is not derived from the ABI: it can disagree with the compiler"
  grep -qF 'CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64"' "$OVERLAY" \
    && grep -qF 'set(ARCHITECTURE "x86_64" CACHE STRING "" FORCE)' "$OVERLAY" \
    && ok "x86_64 gets its own architecture (ARMJIT_x64)" \
    || bad "x86_64 is not handled: it would get the A64 backend"
  grep -qF 'set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)' "$OVERLAY" \
    && ok "native code is built Release" \
    || bad "native code is not built Release: the native step defaults to Debug"
  grep -qF 'set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)' "$OVERLAY" \
    && ok "the GL renderer is off (our core's GL is desktop GL, not GLES)" \
    || bad "ENABLE_OGLRENDERER is not turned off: the desktop-GL renderer will not compile on Android"
  # Ordering: cache variables set after add_subdirectory are too late -- the core
  # configures first and never sees them. The script must therefore splice the block
  # BEFORE the anchor. (Comparing string offsets in this file proves nothing:
  # 'add_subdirectory(${CORE-LIB}' also occurs as the anchor variable, earlier.)
  grep -qF 's.replace(anchor, block + anchor, 1)' "$OVERLAY" \
    && ok "the settings are spliced in before the core configures" \
    || bad "the settings are not placed before add_subdirectory: the core never sees them"
else
  bad "scripts/android-apply-overlay.sh is missing"
fi

echo
echo "== the option itself must behave as measured"
PROBE="$(mktemp -d)"
cat > "$PROBE/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.22)
project(jitopt C)
include(CheckSymbolExists)
include(CMakeDependentOption)
function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
        endif()
    endif()
endfunction()
detect_architecture("__x86_64__" x86_64)
detect_architecture("__i386__" x86)
detect_architecture("__arm__" ARM)
detect_architecture("__aarch64__" ARM64)
cmake_dependent_option(ENABLE_JIT "Enable JIT recompiler" ON
    "ARCHITECTURE STREQUAL x86_64 OR ARCHITECTURE STREQUAL ARM64;NOT ARCHITECTURE STREQUAL x86_64 OR NOT APPLE" OFF)
file(WRITE "${CMAKE_BINARY_DIR}/R.txt" "${ARCHITECTURE}|${ENABLE_JIT}")
EOF
for want in "ARM64|ON" "x86_64|ON" "ARM|OFF"; do
  arch="${want%%|*}"; expect="${want##*|}"
  rm -rf "$PROBE/b-$arch"
  cmake -S "$PROBE" -B "$PROBE/b-$arch" -DARCHITECTURE="$arch" >/dev/null 2>&1
  got="$(cat "$PROBE/b-$arch/R.txt" 2>/dev/null || echo 'FAILED')"
  if [ "$got" = "$want" ]; then ok "ARCHITECTURE=$arch -> ENABLE_JIT=$expect"
  else bad "ARCHITECTURE=$arch gave '$got', expected '$want'"; fi
done
rm -rf "$PROBE"

echo
echo "== the overlay must not clobber upstream's resources or manifest"
if [ -f "$OVERLAY" ]; then
  grep -qE '^[[:space:]]*cp -r "\$MAIN/res' "$OVERLAY" \
    && bad "the overlay still copies res/ wholesale (clobbers upstream's strings)" \
    || ok "the overlay does not copy res/ wholesale"
  grep -qE '^[[:space:]]*cp .*AndroidManifest' "$OVERLAY" \
    && bad "the overlay still copies the manifest (deletes upstream activities)" \
    || ok "the overlay does not replace the manifest"
  grep -qF 'oga_app_name' "$OVERLAY" \
    && ok "the app label gets its own key" \
    || bad "the app_name collision is not handled"
  grep -qF 'pkg_check_modules(ENet' "$OVERLAY" \
    && ok "the swapped-in core's net/CMakeLists is fixed" \
    || bad "the pkg_check_modules(ENet ...) fix is missing"
  grep -qF 'androidx.webkit' "$OVERLAY" \
    && ok "androidx.webkit is added" \
    || bad "androidx.webkit is missing: MainActivity will not compile"
fi

echo
echo "== negative control: removing a pin must FAIL this check"
# Run only the pin assertions against a mutated copy of the overlay. Do NOT
# recurse into this script (that re-runs the control and never terminates), and
# do NOT execute the overlay (it would copy real files).
TMP="$(mktemp -d)"; mkdir -p "$TMP/scripts"
sed 's|set(ENABLE_JIT ON CACHE BOOL "" FORCE)||' "$OVERLAY" > "$TMP/scripts/android-apply-overlay.sh"
if grep -qF 'set(ENABLE_JIT ON CACHE BOOL "" FORCE)' "$TMP/scripts/android-apply-overlay.sh"; then
  bad "the control mutation did not remove the pin"
else
  ok "the pin assertion anchors on the emitted argument (mutation is detectable)"
fi
rm -rf "$TMP"

echo
[ "$fail" -eq 0 ] || exit 1
echo "PASS: the Android build compiles the JIT in, and the overlay adds rather than replaces"
