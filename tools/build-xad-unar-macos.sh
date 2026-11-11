#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRCROOT="$ROOT/third_party/t081-xad"
OUT="${1:-$ROOT/ep128emu.app/Contents/MacOS/ep128emu-unar}"
UNIVERSAL=${EP128EMU_UNAR_UNIVERSAL:-0}
if [ "$UNIVERSAL" = 1 ]; then
  mkdir -p "$ROOT/build"
  TMPROOT="$ROOT/build"
else
  TMPROOT="${TMPDIR:-/tmp}"
fi
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
if [ "$UNIVERSAL" = 1 ]; then
  # Staging uses source makefiles; command-line variables propagate to the
  # recursive UniversalDetector build without altering the pinned sources.
  /usr/bin/make -C "$STAGE/src/XADMaster" \
    -f Makefile.ep128emu-arm64 -j"$JOBS" \
    GCC_OPTS='-O2 -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 -Wno-import -Wno-multichar -Wno-deprecated-declarations' \
    ALL_LDFLAGS='-arch arm64 -arch x86_64 -mmacosx-version-min=11.0 -ObjC' >/dev/null
else
  /usr/bin/make -C "$STAGE/src/XADMaster" \
    -f Makefile.ep128emu-arm64 -j"$JOBS" >/dev/null
fi

BIN="$STAGE/src/XADMaster/unar"
if [ "$UNIVERSAL" = 1 ]; then
  archs=$(lipo -archs "$BIN")
  sorted_archs=$(printf '%s\n' $archs | LC_ALL=C sort | tr '\n' ' ')
  [ "$sorted_archs" = "arm64 x86_64 " ] || {
    echo "UNAR_UNIVERSAL_ARCH_FAIL: $archs" >&2; exit 1
  }
else
  file "$BIN" | grep -q 'Mach-O 64-bit executable arm64' || {
    echo "Unexpected ep128emu-unar architecture." >&2
    file "$BIN" >&2
    exit 1
  }
fi

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
if [ "$UNIVERSAL" = 1 ]; then
  arch -x86_64 "$OUT" -v
  echo UNAR_UNIVERSAL_NATIVE_ROSETTA_PASS
fi
shasum -a 256 "$OUT"
RESULT=0
