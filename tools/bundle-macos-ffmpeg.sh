#!/bin/sh
set -eu

APP=${1:?usage: bundle-macos-ffmpeg.sh /path/to/app}
APP=$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HELPER="$APP/Contents/MacOS/ep128emu-ffmpeg"
RES="$APP/Contents/Resources"
LICENSE_DIR="$RES/FFmpeg-Licenses"
FFMPEG_ARCHIVE="${EP128EMU_FFMPEG_SOURCE_ARCHIVE:-/tmp/ep128emu-ffmpeg-8.1.2.tar.xz}"
X264_ARCHIVE="${EP128EMU_X264_SOURCE_ARCHIVE:-/tmp/ep128emu-x264-r3222.tar.gz}"
X264_COMMIT="b35605ace3ddf7c1a5d67a2eb553f034aef41d55"

mkdir -p "$APP/Contents/MacOS" "$LICENSE_DIR"
"$ROOT/tools/build-minimal-ffmpeg-macos.sh" "$HELPER"

/usr/bin/tar -xOf "$FFMPEG_ARCHIVE" ffmpeg-8.1.2/COPYING.GPLv2 > "$LICENSE_DIR/FFmpeg-COPYING.GPLv2"
/usr/bin/tar -xzOf "$X264_ARCHIVE" "x264-${X264_COMMIT}/COPYING" > "$LICENSE_DIR/x264-COPYING"

HELPER_SHA=$(/usr/bin/shasum -a 256 "$HELPER" | /usr/bin/awk '{print $1}')
cat > "$RES/FFMPEG_BUILD_INFO.txt" <<INFO
EP128EMU minimal bundled video encoder helper
FFmpeg source: 8.1.2
FFmpeg source URL: https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz
FFmpeg source archive SHA-256: 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
x264 source: r3222 / commit b35605ace3ddf7c1a5d67a2eb553f034aef41d55
x264 source archive SHA-256: cd71a7515b0e9a012e1ac9b1f8415bebcaf6fc97d4db32286642ac4c0fbe24f9
x264 is statically linked into the helper.
Helper pre-codesign SHA-256: $HELPER_SHA
Enabled purpose: AVI/MSRLE+PCM input -> MP4/H.264+AAC output for EP128EMU video capture.
No Homebrew/local runtime dependency is permitted.
INFO

echo "Bundled minimal source-built FFmpeg helper: $HELPER"
