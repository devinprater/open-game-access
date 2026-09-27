#!/usr/bin/env bash
# build-ffmpeg.sh — minimal static ffmpeg (upstream ppsspp-ffmpeg fork) for an
# Apple slice: device arm64, sim arm64, or sim x86_64.
#
# The decoder/demuxer set is the same minimal set proven on the host proof
# (h264/aac/atrac3/mp3/pcm + mpegps/mpegvideo/aac/oma): video actually plays,
# and --disable-everything keeps the archive small.
#
# Cross-compiles from WSL with the Swift toolchain's clang + the Darwin SDK
# bundle (same scheme as build-core.sh), or natively on macOS with xcrun.
# Usage: build-ffmpeg.sh <device|sim> [aarch64|x86_64]
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SLICE="${1:-}"; ARCH="${2:-aarch64}"
[ "$SLICE" = device ] || [ "$SLICE" = sim ] || { echo "usage: $0 <device|sim> [arch]" >&2; exit 1; }

FF_SRC="${FFMPEG_SRC:-$HOME/src/ppsspp/ffmpeg}"
FF_OUT="${FFMPEG_OUT:-$HOME/ffmpeg-ios}/$SLICE"
[ -d "$FF_SRC/libavcodec" ] || { echo "!! no ffmpeg source at $FF_SRC" >&2; exit 1; }

DARWIN="$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms"
if [ "$SLICE" = device ]; then
  FFARCH=aarch64; TARGET=arm64-apple-ios17.0; PLATFORM=iOS
  SDK="${FFMPEG_SDK:-${SDKROOT:-$DARWIN/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.5.sdk}}"
  [ "$ARCH" = aarch64 ] || { echo "!! device slice is arm64-only" >&2; exit 1; }
else
  SDK="${FFMPEG_SDK:-${SDKROOT:-$DARWIN/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.5.sdk}}"
  PLATFORM=ios-simulator
  if [ "$ARCH" = x86_64 ]; then
    FFARCH=x86; TARGET=x86_64-apple-ios17.0-simulator
  else
    FFARCH=aarch64; TARGET=arm64-apple-ios17.0-simulator
  fi
fi
[ -d "$SDK" ] || { echo "!! no SDK at $SDK (FFMPEG_SDK to override)" >&2; exit 1; }

CC="${FFMPEG_CC:-/usr/local/swift/bin/clang}"
[ -x "$CC" ] || CC="$(command -v clang || echo clang)"

# Same minimal set as the host proof (config proven to link + play video).
# shellcheck disable=SC1091
. "$FF_SRC/shared_options.sh"
DECODERS="--enable-decoder=h263 --enable-decoder=h263p --enable-decoder=h264 \
  --enable-decoder=mpeg2video --enable-decoder=rawvideo \
  --enable-decoder=aac --enable-decoder=atrac3 --enable-decoder=atrac3p \
  --enable-decoder=mp3 --enable-decoder=pcm_s16le --enable-decoder=pcm_s8"
DEMUXERS="--enable-demuxer=h264 --enable-demuxer=mpegps \
  --enable-demuxer=mpegvideo --enable-demuxer=aac --enable-demuxer=oma"

STAMP="arch=$FFARCH target=$TARGET sdk=$SDK cc=$CC decoders=$DECODERS demuxers=$DEMUXERS"
if [ -f "$FF_OUT/lib/libavcodec.a" ] && [ -f "$FF_OUT/stamp" ] && \
   [ "$(cat "$FF_OUT/stamp")" = "$STAMP" ]; then
  echo "== ffmpeg $SLICE/$ARCH up to date =="
  exit 0
fi

mkdir -p "$FF_OUT/bin"
# A previous slice's configure state lives in the source tree; without a
# clean, the second slice silently reuses the first slice's config.h.
make -C "$FF_SRC" distclean > /dev/null 2>&1 || true
cp "$ROOT/scripts/ff-ld-wrap.sh" "$FF_OUT/bin/ff-ld-wrap"
chmod +x "$FF_OUT/bin/ff-ld-wrap"
# Link shim env (see scripts/ff-ld-wrap.sh): swift's Linux clang cannot drive
# ld64.lld for Apple targets on its own (no -platform_version), so configure's
# link tests go through the wrapper via --ld-path.
LDWRAP_FLAGS=""
if [ "$(uname)" != Darwin ]; then
  export FF_PLATFORM="$PLATFORM" FF_MIN=17.0 FF_SDKVER=26.5
  export FF_LD
  FF_LD="$(ls "$HOME"/.swiftpm/swift-sdks/darwin.artifactbundle/toolset/bin/ld64.lld 2>/dev/null || command -v ld64.lld)"
  LDWRAP_FLAGS="--ld-path=$FF_OUT/bin/ff-ld-wrap"
fi
cd "$FF_SRC" || exit 1
# shellcheck disable=SC2086
./configure --prefix="$FF_OUT" --enable-cross-compile --arch="$FFARCH" \
  --cc="$CC" --sysroot="$SDK" --target-os=darwin --cpu=generic --enable-pic \
  --extra-cflags="-target $TARGET -isysroot $SDK" \
  --extra-ldflags="-target $TARGET -isysroot $SDK $LDWRAP_FLAGS" \
  --disable-shared --enable-static \
  --disable-iconv --disable-videotoolbox --disable-vda \
  --enable-zlib --disable-lzma --disable-bzlib \
  --disable-filters --disable-programs --disable-network --disable-avfilter \
  --disable-postproc --disable-encoders --disable-doc \
  --disable-everything \
  $DECODERS $DEMUXERS \
  --enable-parser=h264 --enable-parser=aac --enable-parser=mpegaudio \
  --enable-bsf=h264_mp4toannexb --enable-bsf=aac_adtstoasc \
  --enable-protocol=file --enable-swscale --enable-swresample \
  > "$FF_OUT/config.log" 2>&1 || { echo "!! ffmpeg configure failed (see $FF_OUT/config.log)" >&2; exit 1; }

make -j8 > "$FF_OUT/build.log" 2>&1 || { echo "!! ffmpeg build failed (see $FF_OUT/build.log)" >&2; exit 1; }
make install > /dev/null 2>&1
echo "$STAMP" > "$FF_OUT/stamp"
echo "== ffmpeg $SLICE/$ARCH installed to $FF_OUT =="
ls "$FF_OUT/lib"
