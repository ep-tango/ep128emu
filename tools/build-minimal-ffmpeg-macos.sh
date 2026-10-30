#!/bin/sh
set -eu
PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH

OUT=${1:?usage: build-minimal-ffmpeg-macos.sh /path/to/output-helper}
FFMPEG_VERSION=8.1.2
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
FFMPEG_SHA256="464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c"
FFMPEG_ARCHIVE="${EP128EMU_FFMPEG_SOURCE_ARCHIVE:-/tmp/ep128emu-ffmpeg-${FFMPEG_VERSION}.tar.xz}"

X264_COMMIT="b35605ace3ddf7c1a5d67a2eb553f034aef41d55"
X264_URL="https://code.videolan.org/videolan/x264/-/archive/${X264_COMMIT}/x264-${X264_COMMIT}.tar.gz"
X264_SHA256="cd71a7515b0e9a012e1ac9b1f8415bebcaf6fc97d4db32286642ac4c0fbe24f9"
X264_ARCHIVE="${EP128EMU_X264_SOURCE_ARCHIVE:-/tmp/ep128emu-x264-r3222.tar.gz}"

hash_file() {
  /usr/bin/shasum -a 256 "$1" | /usr/bin/awk '{print $1}'
}

verify_hash() {
  f=$1
  expected=$2
  [ -f "$f" ] || { echo "Missing pinned build input: $f" >&2; exit 1; }
  actual=$(hash_file "$f")
  [ "$actual" = "$expected" ] || {
    echo "Pinned build input hash mismatch: $f" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
  }
}

fetch_pinned() {
  f=$1
  url=$2
  expected=$3
  label=$4
  if [ ! -f "$f" ] || [ "$(hash_file "$f" 2>/dev/null || true)" != "$expected" ]; then
    rm -f "$f"
    echo "Fetching pinned $label source..."
    /usr/bin/curl -L --fail --retry 2 -o "$f" "$url"
  fi
  verify_hash "$f" "$expected"
}

fetch_pinned "$FFMPEG_ARCHIVE" "$FFMPEG_URL" "$FFMPEG_SHA256" "FFmpeg $FFMPEG_VERSION"
fetch_pinned "$X264_ARCHIVE" "$X264_URL" "$X264_SHA256" "x264 r3222"

WORK=$(mktemp -d /tmp/ep128emu-minffmpeg.XXXXXX)
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
FFSRC="$WORK/ffmpeg-$FFMPEG_VERSION"
XSRC="$WORK/x264-src"
XINST="$WORK/x264-install"
BUILD="$WORK/ffmpeg-build"
PCDIR="$WORK/pkgconfig"
mkdir "$XSRC" "$BUILD" "$PCDIR"
/usr/bin/tar -xf "$FFMPEG_ARCHIVE" -C "$WORK"
/usr/bin/tar -xzf "$X264_ARCHIVE" -C "$XSRC" --strip-components=1

cd "$XSRC"
./configure --prefix="$XINST" --disable-lsmash --disable-swscale --disable-ffms --enable-shared --enable-static --enable-strip >/dev/null
cat > x264_config.h <<'XCFG'
#define X264_GPL           1
#define X264_INTERLACED    1
#define X264_BIT_DEPTH     0
#define X264_CHROMA_FORMAT 0
#define X264_REV 3222
#define X264_REV_DIFF 0
#define X264_VERSION " r3222 b35605a"
#define X264_POINTVER "0.165.3222 b35605a"
XCFG
/usr/bin/make -j"$(/usr/sbin/sysctl -n hw.logicalcpu)" >/dev/null
/usr/bin/make install >/dev/null

cat > "$PCDIR/x264.pc" <<PC
prefix=$XINST
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include
Name: x264
Description: H.264 encoder static
Version: 0.165.3222
Libs: $XINST/lib/libx264.a
Libs.private: -lpthread -lm -ldl
Cflags: -I\${includedir}
PC

cd "$BUILD"
export PKG_CONFIG_PATH="$PCDIR"
"$FFSRC/configure" \
  --prefix=/usr/local/ep128emu-ffmpeg-minimal \
  --cc=/usr/bin/clang \
  --disable-shared --enable-static --disable-doc --disable-debug \
  --disable-autodetect --disable-network --disable-everything \
  --disable-ffplay --disable-ffprobe --enable-ffmpeg \
  --enable-gpl --enable-libx264 \
  --enable-avcodec --enable-avformat --enable-avfilter --enable-avutil \
  --enable-swscale --enable-swresample \
  --enable-protocol=file --enable-demuxer=avi \
  --enable-decoder=msrle --enable-decoder=pcm_s16le \
  --enable-encoder=libx264 --enable-encoder=aac \
  --enable-muxer=mp4 \
  --enable-filter=scale --enable-filter=format \
  --enable-filter=aresample --enable-filter=anull --enable-filter=null \
  --pkg-config-flags=--static >/dev/null

/usr/bin/make -j"$(/usr/sbin/sysctl -n hw.logicalcpu)" ffmpeg >/dev/null
/usr/bin/strip -x ffmpeg

if /usr/bin/otool -L ffmpeg | tail -n +2 | /usr/bin/awk '{print $1}' | \
   /usr/bin/grep -E '^(/opt/homebrew|/usr/local|/opt/local)/' >/dev/null; then
  echo "Minimal FFmpeg unexpectedly has an external local runtime dependency" >&2
  /usr/bin/otool -L ffmpeg >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
cp ffmpeg "$OUT"
chmod 755 "$OUT"

echo "Minimal source-built FFmpeg helper built:"
echo "  output: $OUT"
echo "  size: $(/usr/bin/stat -f %z "$OUT") bytes"
echo "  sha256: $(hash_file "$OUT")"
echo "=== MINIMAL SOURCE-BUILT FFMPEG SUCCESS ==="
