#!/bin/sh
set -eu
PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH

OUT=${1:?usage: build-minimal-ffmpeg-macos.sh /path/to/output-helper}
# Explicit 2.2 Universal source build requires macOS 11.0 for both slices.
# Leave the original single-architecture builder unchanged by default.
if [ -n "${EP128EMU_FFMPEG_MINMACOS:-}" ]; then
  [ "$EP128EMU_FFMPEG_MINMACOS" = '11.0' ] || {
    echo 'FFMPEG_UNSUPPORTED_TRIAL_MINMACOS' >&2; exit 1;
  }
  export MACOSX_DEPLOYMENT_TARGET=11.0
fi
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

# Only the explicit Universal trial places temporary build trees inside
# the owning worktree. Default legacy invocation remains unchanged.
WORKROOT=${EP128EMU_FFMPEG_WORKROOT:-/tmp}
[ -d "$WORKROOT" ] || { echo "FFMPEG_WORKROOT_MISSING" >&2; exit 1; }
RESUME=${EP128EMU_FFMPEG_RESUME_WORK:-}
if [ -n "$RESUME" ]; then
  # Only resume a project-local trial created from the pinned sources.
  case "$RESUME" in
    "$WORKROOT"/ep128emu-minffmpeg.*) : ;;
    *) echo 'FFMPEG_RESUME_OUTSIDE_PROJECT_BUILD' >&2; exit 1 ;;
  esac
  [ -d "$RESUME/x264-src" ] && [ -f "$RESUME/ffmpeg-$FFMPEG_VERSION/configure" ] || {
    echo 'FFMPEG_RESUME_SOURCE_MISSING' >&2; exit 1;
  }
  WORK="$RESUME"
else
  WORK=$(mktemp -d "$WORKROOT/ep128emu-minffmpeg.XXXXXX")
  if [ "${EP128EMU_FFMPEG_PRESERVE_WORK:-0}" = 1 ]; then
    case "$WORKROOT" in
      "$PWD"/build/*) echo "FFMPEG_PROJECT_LOCAL_RESUMABLE_WORK=$WORK" ;;
      *) echo 'FFMPEG_PRESERVE_REQUIRES_PROJECT_LOCAL_WORKROOT' >&2; exit 1 ;;
    esac
  else
    trap 'rm -rf "$WORK"' EXIT HUP INT TERM
  fi
fi
JOBS=${EP128EMU_FFMPEG_JOBS:-$(/usr/sbin/sysctl -n hw.logicalcpu)}
# FFmpeg embeds its --cc configure value in the public -version output.
# A stable command name avoids leaking the developer's absolute home path.
FF_CROSS_CC_CMD=
if [ -n "${EP128EMU_FFMPEG_NATIVE_X86_CC:-}" ]; then
  [ -x "$EP128EMU_FFMPEG_NATIVE_X86_CC" ] || { echo 'FFMPEG_NATIVE_X86_COMPILER_MISSING' >&2; exit 1; }
  mkdir -p "$WORK/cc-bin"
  cp -p "$EP128EMU_FFMPEG_NATIVE_X86_CC" "$WORK/cc-bin/ep128-x86-clang"
  FF_CROSS_CC_CMD=ep128-x86-clang
  export MACOSX_DEPLOYMENT_TARGET="${EP128EMU_MACOS_MIN:-11.0}"
  PATH="$WORK/cc-bin:$PATH"
  export PATH
fi
FFSRC="$WORK/ffmpeg-$FFMPEG_VERSION"
XSRC="$WORK/x264-src"
XINST="$WORK/x264-install"
BUILD="$WORK/ffmpeg-build"
PCDIR="$WORK/pkgconfig"
if [ -z "$RESUME" ]; then
  mkdir "$XSRC" "$BUILD" "$PCDIR"
  /usr/bin/tar -xf "$FFMPEG_ARCHIVE" -C "$WORK"
  /usr/bin/tar -xzf "$X264_ARCHIVE" -C "$XSRC" --strip-components=1
fi

cd "$XSRC"
if [ -z "$RESUME" ]; then
  ./configure --prefix="$XINST" --disable-lsmash --disable-swscale --disable-ffms --enable-shared --enable-static --enable-strip >/dev/null
fi
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
# A timed-out source build may have already installed the pinned, valid
# x264 static library. Reuse it on the exact same guarded source worktree.
if [ -n "$RESUME" ] && [ -s "$XINST/lib/libx264.a" ] &&
   [ -s "$XINST/include/x264.h" ] && [ -s "$XINST/include/x264_config.h" ] &&
   [ -s "$XINST/lib/pkgconfig/x264.pc" ]; then
  echo X264_PINNED_SOURCE_INSTALL_RESUME_PASS
else
  if [ -n "$FF_CROSS_CC_CMD" ]; then
    /usr/bin/make -j"${JOBS}" CC="$FF_CROSS_CC_CMD" >/dev/null
  else
    /usr/bin/make -j"${JOBS}" >/dev/null
  fi
  /usr/bin/make install >/dev/null
fi

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
if [ ! -f config.h ]; then
"$FFSRC/configure" \
  --prefix=/usr/local/ep128emu-ffmpeg-minimal \
  --cc="${FF_CROSS_CC_CMD:-/usr/bin/clang}" \
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
fi

# Source-only, opt-in path privacy: map absolute pinned build source paths
# to a neutral marker in compiled __FILE__ strings. Modify only generated
# FFmpeg CFLAGS, not the published configure string or pinned source archive.
if [ "${EP128EMU_FFMPEG_PATH_PRIVACY:-0}" = 1 ]; then
  /usr/bin/python3 - "$BUILD/ffbuild/config.mak" "$WORK" <<'PYMAP'
from pathlib import Path
import sys
p=Path(sys.argv[1]); work=sys.argv[2]
flag=f'-ffile-prefix-map={work}=/ep128emu-ffmpeg-src'
s=p.read_text()
lines=s.splitlines(keepends=True)
assert sum(line.startswith('CFLAGS=') for line in lines)==1
lines=[line.rstrip('\n') + (' '+flag if flag not in line else '')+'\n' if line.startswith('CFLAGS=') else line for line in lines]
p.write_text(''.join(lines))
PYMAP
fi
/usr/bin/make -j"${JOBS}" ffmpeg >/dev/null
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
