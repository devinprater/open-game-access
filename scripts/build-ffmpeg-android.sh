#!/usr/bin/env bash
# build-ffmpeg-android.sh — minimal static ffmpeg (upstream ppsspp-ffmpeg
# fork) for an Android ABI: arm64-v8a or x86_64.
#
# This is the Android twin of scripts/build-ffmpeg.sh (the iOS slices).
# The decoder/demuxer set is COPIED VERBATIM from that script — the same
# minimal set proven on the host proof and on iOS (h263/h263p/h264/
# mpeg2video/rawvideo video; aac/atrac3/atrac3p/mp3/pcm audio; mpegps/
# mpegvideo/aac/oma demuxers): it is what PPSSPP's sceMpeg/sceVideocodec
# PMF playback actually asks for, and --disable-everything keeps the
# archive small. Do not widen or narrow the set here without changing
# build-ffmpeg.sh too — the platforms must not drift apart.
#
# Source + pin: PPSSPP's vendored ffmpeg fork, checked out as the
# `ffmpeg` submodule of the pinned PPSSPP tree (PPSSPP_REV in
# scripts/bootstrap-deps.sh: f293b10fb2d9dc0c2bc10281444ee3d3e932e6ad;
# the submodule commit at that pin is b87f7c6d522d1edba77cfc4fac96ce48a236f806,
# an FFmpeg 3.0.2 base with PPSSPP's Atrac/PMF patches). FFMPEG_SRC
# overrides the location; default $HOME/src/ppsspp/ffmpeg, the same
# default build-ffmpeg.sh uses.
#
# Toolchain: the Android NDK's LLVM (NDK 28.0.13004108 is what CI and
# the app build use — see .github/workflows/android-apk.yml for why the
# frontend pins 28). Discovery order: $ANDROID_NDK_HOME,
# $ANDROID_NDK_ROOT, $ANDROID_NDK, then $ANDROID_HOME/ndk/<highest>,
# then $HOME/Android/Sdk/ndk/<highest>. API level 24 = the frontend's
# minSdk.
#
# Platform shape follows the fork's own ffmpeg/android_arm64-v8a.sh /
# android_x86_64.sh (target-os=linux + -DANDROID defines, NDK tools
# named explicitly), NOT the fork's component set (theirs carries
# encoders/muxers/mpeg4/mjpeg for the full PPSSPP app; OGA's set is the
# iOS one above). --disable-asm matches the iOS build (and the fork's
# x86_64 script, which disables asm for the same PIC-relocation reason).
#
# Stamp-gated exactly like build-ffmpeg.sh: a current build is a no-op.
#
# Usage: build-ffmpeg-android.sh <arm64-v8a|x86_64>
set -uo pipefail

ABI="${1:-}"
case "$ABI" in
  arm64-v8a) FFARCH=aarch64; TRIPLE=aarch64-linux-android ;;
  x86_64)    FFARCH=x86_64;  TRIPLE=x86_64-linux-android ;;
  *) echo "usage: $0 <arm64-v8a|x86_64>" >&2; exit 1 ;;
esac
API="${FFMPEG_ANDROID_API:-24}"

FF_SRC="${FFMPEG_SRC:-$HOME/src/ppsspp/ffmpeg}"
FF_OUT="${FFMPEG_ANDROID_OUT:-$HOME/ffmpeg-android}/$ABI"
[ -d "$FF_SRC/libavcodec" ] || { echo "!! no ffmpeg source at $FF_SRC" >&2; exit 1; }

# ---- NDK discovery -------------------------------------------------------
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
if [ -z "$NDK" ]; then
  for base in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Android/Sdk"; do
    [ -n "$base" ] && [ -d "$base/ndk" ] || continue
    cand="$(ls -d "$base"/ndk/*/ 2>/dev/null | sort -V | tail -1)"
    [ -n "$cand" ] && NDK="${cand%/}" && break
  done
fi
[ -n "$NDK" ] && [ -d "$NDK" ] || { echo "!! no Android NDK found (set ANDROID_NDK_HOME)" >&2; exit 1; }
case "$(uname)" in
  Darwin) HOST_TAG=darwin-x86_64 ;;
  *)      HOST_TAG=linux-x86_64 ;;
esac
PREBUILT="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
CC="$PREBUILT/bin/clang"
[ -x "$CC" ] || { echo "!! no NDK clang at $CC" >&2; exit 1; }
TARGET="$TRIPLE$API"
SYSROOT="$PREBUILT/sysroot"

# ---- the component set: build-ffmpeg.sh's, verbatim ----------------------
DECODERS="--enable-decoder=h263 --enable-decoder=h263p --enable-decoder=h264 \
  --enable-decoder=mpeg2video --enable-decoder=rawvideo \
  --enable-decoder=aac --enable-decoder=atrac3 --enable-decoder=atrac3p \
  --enable-decoder=mp3 --enable-decoder=pcm_s16le --enable-decoder=pcm_s8"
DEMUXERS="--enable-demuxer=h264 --enable-demuxer=mpegps \
  --enable-demuxer=mpegvideo --enable-demuxer=aac --enable-demuxer=oma"

CCVER="$("$CC" --version | head -1)"
STAMP="abi=$ABI arch=$FFARCH target=$TARGET ndk=$NDK cc=$CCVER noasm=1 decoders=$DECODERS demuxers=$DEMUXERS src=$(git -C "$FF_SRC" rev-parse HEAD 2>/dev/null || echo unknown)"
if [ -f "$FF_OUT/lib/libavcodec.a" ] && [ -f "$FF_OUT/stamp" ] && \
   [ "$(cat "$FF_OUT/stamp")" = "$STAMP" ]; then
  echo "== ffmpeg android/$ABI up to date =="
  exit 0
fi

mkdir -p "$FF_OUT"
# A previous ABI's configure state lives in the source tree; without a
# clean, the second ABI silently reuses the first ABI's config.h (the
# same trap build-ffmpeg.sh documents for slices).
make -C "$FF_SRC" distclean > /dev/null 2>&1 || true
cd "$FF_SRC" || exit 1
# shellcheck disable=SC2086
./configure --prefix="$FF_OUT" --enable-cross-compile --arch="$FFARCH" \
  --target-os=linux --cpu=generic --enable-pic \
  --cc="$CC" --cxx="$PREBUILT/bin/clang++" \
  --ar="$PREBUILT/bin/llvm-ar" --ranlib="$PREBUILT/bin/llvm-ranlib" \
  --nm="$PREBUILT/bin/llvm-nm" --strip="$PREBUILT/bin/llvm-strip" \
  --sysroot="$SYSROOT" \
  --extra-cflags="--target=$TARGET -DANDROID -DANDROID_PLATFORM=android-$API -Dipv6mr_interface=ipv6mr_ifindex -fPIC -fno-short-enums -fno-strict-aliasing" \
  --extra-ldflags="--target=$TARGET" \
  --extra-libs="-latomic" \
  --disable-shared --enable-static --disable-asm \
  --disable-iconv \
  --enable-zlib --disable-lzma --disable-bzlib \
  --disable-programs --disable-doc --disable-network \
  --disable-encoders \
  --disable-everything \
  $DECODERS $DEMUXERS \
  --enable-parser=h264 --enable-parser=aac --enable-parser=mpegaudio \
  --enable-bsf=h264_mp4toannexb --enable-bsf=aac_adtstoasc \
  --enable-protocol=file --enable-swscale --enable-swresample \
  > "$FF_OUT/config.log" 2>&1 || { echo "!! ffmpeg configure failed (see $FF_OUT/config.log)" >&2; exit 1; }

make -j8 > "$FF_OUT/build.log" 2>&1 || { echo "!! ffmpeg build failed (see $FF_OUT/build.log)" >&2; exit 1; }
make install > /dev/null 2>&1
echo "$STAMP" > "$FF_OUT/stamp"
echo "== ffmpeg android/$ABI installed to $FF_OUT =="
ls "$FF_OUT/lib"
