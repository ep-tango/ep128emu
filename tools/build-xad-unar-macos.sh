#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRCROOT="$ROOT/third_party/t081-xad"
OUT="${1:-$ROOT/ep128emu.app/Contents/MacOS/ep128emu-unar}"
TMPROOT="${TMPDIR:-/tmp}"
STAGE="$(mktemp -d "$TMPROOT/ep128emu-unar-build.XXXXXX")"
RESULT=1

finish()
{
  rm -rf "$STAGE"
  if [ "$RESULT" -eq 0 ]; then
    echo "PASS: ep128emu-unar source build"
  else
    echo "FAIL: ep128emu-unar source build" >&2
  fi
}
trap finish EXIT HUP INT TERM

[ -f "$SRCROOT/XADMaster/Makefile.ep128emu-arm64" ] || {
  echo "Missing vendored XADMaster source." >&2
  exit 1
}
[ -f "$SRCROOT/UniversalDetector/Makefile.ep128emu-arm64" ] || {
  echo "Missing vendored UniversalDetector source." >&2
  exit 1
}

mkdir -p "$STAGE/src"
cp -R "$SRCROOT/XADMaster" "$STAGE/src/XADMaster"
cp -R "$SRCROOT/UniversalDetector" "$STAGE/src/UniversalDetector"

JOBS="${EP128EMU_UNAR_BUILD_JOBS:-2}"
/usr/bin/make -C "$STAGE/src/XADMaster" \
  -f Makefile.ep128emu-arm64 -j"$JOBS" >/dev/null

BIN="$STAGE/src/XADMaster/unar"
file "$BIN" | grep -q 'Mach-O 64-bit executable arm64' || {
  echo "Unexpected ep128emu-unar architecture." >&2
  file "$BIN" >&2
  exit 1
}

if otool -L "$BIN" | tail -n +2 | awk '{print $1}' | \
    grep -E '^(/opt/homebrew|/usr/local|/opt/local|/Users)/' >/dev/null; then
  echo "External dependency in ep128emu-unar:" >&2
  otool -L "$BIN" >&2
  exit 1
fi

mkdir -p "$(dirname -- "$OUT")"
cp -p "$BIN" "$OUT"
chmod 755 "$OUT"

"$OUT" -v
shasum -a 256 "$OUT"
RESULT=0
