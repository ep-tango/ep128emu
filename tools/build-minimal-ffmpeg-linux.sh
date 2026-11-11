#!/usr/bin/env bash
# Pinned, offline Linux AMD64 FFmpeg/x264 source-only helper build.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
[[ "$(uname -s)" = Linux && "$(uname -m)" = x86_64 ]] || { echo LINUX_AMD64_REQUIRED >&2; exit 11; }
for command_name in cc gcc make tar sha256sum pkg-config yasm nasm readelf python3; do
  command -v "$command_name" >/dev/null || { echo "LINUX_BUILD_TOOL_MISSING: $command_name" >&2; exit 12; }
done
FFMPEG_ARCHIVE="${EP128EMU_FFMPEG_SOURCE_ARCHIVE:-$ROOT/build/universal-deps/source-cache/ffmpeg-8.1.2.tar.xz}"
X264_ARCHIVE="${EP128EMU_X264_SOURCE_ARCHIVE:-$ROOT/build/universal-deps/source-cache/ep128emu-x264-r3222.tar.gz}"
[[ -f "$FFMPEG_ARCHIVE" && -f "$X264_ARCHIVE" ]] || { echo PINNED_SOURCE_INPUT_MISSING >&2; exit 13; }
echo '464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c  '"$FFMPEG_ARCHIVE" | sha256sum -c -
echo 'cd71a7515b0e9a012e1ac9b1f8415bebcaf6fc97d4db32286642ac4c0fbe24f9  '"$X264_ARCHIVE" | sha256sum -c -
BASE="$ROOT/build/linux-helpers/x86_64"
WORK="$BASE/ffmpeg-8.1.2-src"
OUT="$BASE/ep128emu-ffmpeg"
[[ ! -e "$WORK" && ! -e "$OUT" ]] || { echo LINUX_HELPER_WORK_OR_OUTPUT_ALREADY_EXISTS >&2; exit 14; }
mkdir -p "$WORK/x264-src" "$WORK/ffmpeg-build" "$WORK/x264-install"
tar -xf "$FFMPEG_ARCHIVE" -C "$WORK"
tar -xf "$X264_ARCHIVE" -C "$WORK/x264-src" --strip-components=1
JOBS="${EP128EMU_FFMPEG_JOBS:-2}"
cd "$WORK/x264-src"
./configure --prefix="$WORK/x264-install" --enable-static --disable-shared --disable-cli --disable-opencl --disable-lsmash --disable-swscale --disable-ffms
make -j"$JOBS"
make install
[[ -s "$WORK/x264-install/lib/libx264.a" ]] || { echo X264_SOURCE_BUILD_FAILED >&2; exit 15; }
cd "$WORK/ffmpeg-build"
export PKG_CONFIG_PATH="$WORK/x264-install/lib/pkgconfig"
"$WORK/ffmpeg-8.1.2/configure" \
  --prefix=/usr/local/ep128emu-ffmpeg-minimal \
  --cc=gcc --disable-shared --enable-static --disable-doc --disable-debug \
  --disable-autodetect --disable-network --disable-everything \
  --disable-ffplay --disable-ffprobe --enable-ffmpeg \
  --enable-gpl --enable-libx264 \
  --enable-avcodec --enable-avformat --enable-avfilter --enable-avutil \
  --enable-swscale --enable-swresample \
  --enable-protocol=file --enable-demuxer=avi \
  --enable-decoder=msrle --enable-decoder=pcm_s16le \
  --enable-encoder=libx264 --enable-encoder=aac --enable-muxer=mp4 \
  --enable-filter=scale --enable-filter=format \
  --enable-filter=aresample --enable-filter=anull --enable-filter=null \
  --pkg-config-flags=--static
# Do not put the absolute build path in FFmpeg's public configure metadata.
# Insert the compiler-only path mapping in the generated build flags instead.
python3 - "$WORK/ffmpeg-build/ffbuild/config.mak" "$WORK" <<'PYMAP'
from pathlib import Path
import sys
p=Path(sys.argv[1]); work=sys.argv[2]
s=p.read_text(); flag=f'-ffile-prefix-map={work}=/ep128emu-ffmpeg-src'
lines=s.splitlines(keepends=True)
assert sum(line.startswith('CFLAGS=') for line in lines)==1
lines=[line.rstrip('\n') + (' '+flag if flag not in line else '')+'\n'
       if line.startswith('CFLAGS=') else line for line in lines]
p.write_text(''.join(lines))
PYMAP
make -j"$JOBS" ffmpeg
[[ -x ffmpeg ]] || { echo FFMPEG_SOURCE_BUILD_FAILED >&2; exit 16; }
strip ffmpeg
install -m755 ffmpeg "$OUT"
"$OUT" -hide_banner -version | head -n 4
"$OUT" -hide_banner -encoders | grep -E 'libx264|aac'
"$OUT" -hide_banner -decoders | grep -E 'msrle|pcm_s16le'
readelf -h "$OUT" | grep -q "Machine:.*Advanced Micro Devices X86-64" || { echo LINUX_HELPER_NOT_ELF64 >&2; exit 17; }
if ldd "$OUT" | grep -q 'not found'; then echo LINUX_HELPER_MISSING_LIBRARY >&2; exit 18; fi
python3 - "$OUT" "$ROOT" <<'PYPRIVACY'
from pathlib import Path
import sys
binary=Path(sys.argv[1]).read_bytes(); root=sys.argv[2].encode()
assert root not in binary, 'LINUX_FFMPEG_PRIVATE_BUILD_PATH_LEAK'
print('LINUX_FFMPEG_PRIVATE_PATH_SCAN_PASS')
PYPRIVACY
(cd "$BASE" && sha256sum ep128emu-ffmpeg > ep128emu-ffmpeg.sha256)
{ printf 'FFmpeg=8.1.2\nx264=b35605ace3ddf7c1a5d67a2eb553f034aef41d55\n'; printf 'ffmpeg_source_sha256='; sha256sum "$FFMPEG_ARCHIVE" | cut -d' ' -f1; printf 'x264_source_sha256='; sha256sum "$X264_ARCHIVE" | cut -d' ' -f1; gcc --version | head -1; ldd "$OUT"; } > "$BASE/helper-provenance.txt"
echo LINUX_SOURCE_BUILT_FFMPEG_HELPER_PASS
