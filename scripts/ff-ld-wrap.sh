#!/usr/bin/env bash
# ff-ld-wrap — link driver shim for cross ffmpeg: swift's Linux clang forwards
# flags ld64.lld does not understand (-sysroot, -ios_version_min) and omits
# -platform_version (which ld64.lld requires). Filter + inject here.
# Env: FF_PLATFORM (iOS|iOSSimulator), FF_MIN, FF_SDKVER, FF_LD.
set -uo pipefail
LD="${FF_LD:-/home/devin/.swiftpm/swift-sdks/darwin.artifactbundle/toolset/bin/ld64.lld}"
ARGS=(-platform_version "${FF_PLATFORM:?}" "${FF_MIN:?}" "${FF_SDKVER:?}")
SKIP_NEXT=0
for a in "$@"; do
  if [ "$SKIP_NEXT" = 1 ]; then SKIP_NEXT=0; continue; fi
  case "$a" in
    -sysroot|--sysroot) SKIP_NEXT=1 ;;
    -sysroot=*|--sysroot=*) ;;
    -ios_version_min) SKIP_NEXT=1 ;;
    -enable-*) ;;
    -disable-*) ;;
    -m*) ;;
    *) ARGS+=("$a") ;;
  esac
done
exec "$LD" "${ARGS[@]}"
