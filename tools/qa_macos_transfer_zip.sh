#!/bin/zsh
# Checked macOS .app transport; call with APP ZIP [create|verify].
# ZIP must be created by ditto and preserve executable modes and symlink types.
set -euo pipefail
[[ $# -ge 2 && $# -le 4 ]] || { echo 'usage: APP ZIP [create|verify]' >&2; exit 2; }
APP="${1:A}"; ZIP="${2:A}"; MODE="${3:-verify}"; ARCH_MODE="${4:-universal}"
[[ "$ARCH_MODE" == universal || "$ARCH_MODE" == --arm64-only ]] || { echo BAD_ARCH_MODE >&2; exit 2; }
[[ -d "$APP" && "$APP" == *.app ]] || { echo APP_MISSING >&2; exit 2; }
[[ "$MODE" == create || "$MODE" == verify ]] || exit 2
[[ -L "$APP/Contents/Frameworks/libSDL3.dylib" ]] || { echo SOURCE_SDL_LINK_MISSING >&2; exit 2; }
[[ "$(readlink "$APP/Contents/Frameworks/libSDL3.dylib")" == 'libSDL3.0.dylib' ]] || exit 2
/usr/bin/codesign --verify --deep --strict "$APP"
mkdir -p build
TMP="$(mktemp -d "$PWD/build/qa_macos_zip.XXXXXX")"
trap 'rm -rf -- "$TMP"' EXIT
if [[ "$MODE" == create ]]; then
  [[ ! -e "$ZIP" ]] || { echo ARCHIVE_REFUSE_OVERWRITE >&2; exit 2; }
  /usr/bin/ditto -c -k --keepParent "$APP" "$TMP/$(basename "$ZIP")"
  INPUT="$TMP/$(basename "$ZIP")"
else
  [[ -f "$ZIP" ]] || { echo ARCHIVE_MISSING >&2; exit 2; }
  INPUT="$ZIP"
fi
/usr/bin/ditto -x -k "$INPUT" "$TMP"
DEST="$TMP/$(basename "$APP")"
[[ -L "$DEST/Contents/Frameworks/libSDL3.dylib" ]] || { echo EXTRACTED_SDL_LINK_MISSING >&2; exit 2; }
[[ "$(readlink "$DEST/Contents/Frameworks/libSDL3.dylib")" == 'libSDL3.0.dylib' ]] || exit 2
python3 - "$INPUT" "$(basename "$APP")" <<'PY'
import stat,sys,zipfile
p,name=sys.argv[1:]
with zipfile.ZipFile(p) as z:
 f=z.getinfo(name+'/Contents/Frameworks/libSDL3.dylib')
 assert stat.S_ISLNK(f.external_attr>>16), 'ZIP_LINK_IS_NOT_SYMLINK'
 assert z.read(f.filename)==b'libSDL3.0.dylib', 'ZIP_LINK_TARGET_WRONG'
 assert z.testzip() is None, 'ZIP_CRC_FAIL'
PY
/usr/bin/codesign --verify --deep --strict "$DEST"
CHANGES="$(rsync -rclpni --delete "$APP/" "$DEST/")"
[[ -z "$CHANGES" ]] || { echo "$CHANGES"; echo BUNDLE_TREE_MISMATCH >&2; exit 3; }
if [[ "$ARCH_MODE" == --arm64-only ]]; then
  python3 tools/qa27_mac_arm64_closure.py "$DEST"
  python3 tools/qa_macos_sdl_loader.py "$DEST" --arm64-only
else
  python3 tools/qa_macos_sdl_loader.py "$DEST"
fi
if [[ "$MODE" == create ]]; then
  cp -p "$INPUT" "$ZIP"
  cmp "$INPUT" "$ZIP" || { echo ARCHIVE_COPY_DIFFERENT >&2; exit 3; }
fi
shasum -a 256 "$ZIP"
if [[ "$ARCH_MODE" == --arm64-only ]]; then
  echo MAC_TRANSFER_ZIP_SYMLINK_TREE_SIGNATURE_ARM64_PASS
else
  echo MAC_TRANSFER_ZIP_SYMLINK_TREE_SIGNATURE_DUALARCH_PASS
fi
