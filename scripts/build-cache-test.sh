#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build-cache.sh"

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

TMP_BASE="${TMPDIR:-$HOME/.cache}"
mkdir -p "$TMP_BASE"
TMP="$(mktemp -d "$TMP_BASE/oga-build-cache.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

SRC="$TMP/source.cpp"
HEADER="$TMP/mgba/flags.h"
SDK_DIR="$TMP/sdk"
BUILD_SCRIPT="$TMP/build.sh"
SOURCE_LIST="$TMP/sources.sh"
OBJ="$TMP/source.o"
DEP="$TMP/source.d"
SIG="$TMP/source.sig"

mkdir -p "$(dirname "$HEADER")" "$SDK_DIR"
printf 'int value(void);\n' > "$SRC"
printf '#define VALUE 1\n' > "$HEADER"
printf 'SDK v1\n' > "$SDK_DIR/SDKSettings.plist"
printf '# build script\n' > "$BUILD_SCRIPT"
printf '# source list\n' > "$SOURCE_LIST"
sleep 1
printf 'object bytes\n' > "$OBJ"
printf '%s: %s %s\n' "$OBJ" "$SRC" "$HEADER" > "$DEP"

SDK_ID="$(oga_cache_sdk_id "$SDK_DIR")"
KEY="$(oga_cache_fingerprint clang-v1 '-O2 -Iinclude' "$SDK_ID" arm64-target)"
oga_cache_write_fingerprint "$SIG" "$KEY"

oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$KEY" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST" \
  || fail 'an unchanged object with current headers and compiler inputs was not reusable'

CHANGED_FLAGS="$(oga_cache_fingerprint clang-v1 '-O0 -Iinclude' "$SDK_ID" arm64-target)"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$CHANGED_FLAGS" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'changed compiler flags reused a stale object'
fi
CHANGED_COMPILER="$(oga_cache_fingerprint clang-v2 '-O2 -Iinclude' "$SDK_ID" arm64-target)"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$CHANGED_COMPILER" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'changed compiler/toolchain reused a stale object'
fi
CHANGED_TARGET="$(oga_cache_fingerprint clang-v1 '-O2 -Iinclude' "$SDK_ID" x86-target)"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$CHANGED_TARGET" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'changed target reused a stale object'
fi
printf 'SDK v2\n' > "$SDK_DIR/SDKSettings.plist"
CHANGED_SDK_ID="$(oga_cache_sdk_id "$SDK_DIR")"
CHANGED_SDK="$(oga_cache_fingerprint clang-v1 '-O2 -Iinclude' "$CHANGED_SDK_ID" arm64-target)"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$CHANGED_SDK" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'changed SDK settings reused a stale object'
fi

sleep 1
touch "$HEADER"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$KEY" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'regenerated mGBA flags.h reused a stale object'
fi

sleep 1
printf 'object rebuilt\n' > "$OBJ"
oga_cache_write_fingerprint "$SIG" "$KEY"
oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$KEY" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST" \
  || fail 'a rebuilt object with updated dependency metadata was not reusable'

sleep 1
touch "$BUILD_SCRIPT"
if oga_cache_is_valid "$OBJ" "$DEP" "$SIG" "$KEY" "$SRC" "$BUILD_SCRIPT" "$SOURCE_LIST"; then
  fail 'a changed build script reused a stale object'
fi

printf 'PASS: build-cache invalidates on compiler, flags, target, SDK, generated headers, and build-script changes\n'
