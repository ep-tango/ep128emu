#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

# The same canonical pinned FLTK is used by all nested Git worktrees.
PINNED_FLTK="$HOME/Library/ep128emu/fltk-1.4.5"
[ -x "$PINNED_FLTK/bin/fltk-config" ] || {
  echo "Missing pinned FLTK 1.4.5 prefix: $PINNED_FLTK" >&2
  exit 1
}
[ -x "$PINNED_FLTK/bin/fluid" ] || {
  echo "Missing pinned FLTK 1.4.5 fluid: $PINNED_FLTK/bin/fluid" >&2
  exit 1
}

export EP128EMU_FLTK_PREFIX="$PINNED_FLTK"
export EP128EMU_FLTK_CONFIG="$PINNED_FLTK/bin/fltk-config"
export EP128EMU_FLUID="$PINNED_FLTK/bin/fluid"
export PATH="$ROOT/tools:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

scons -c >/dev/null 2>&1 || true
rm -f ep128emu tapeedit makecfg
rm -rf ep128emu.app/Contents/MacOS
scons -j"${EP128EMU_BUILD_JOBS:-2}" utils=0

# Build the bundled RAR extractor from the pinned LGPL source snapshot.
"$ROOT/tools/build-xad-unar-macos.sh" \
  "$ROOT/ep128emu.app/Contents/MacOS/ep128emu-unar"

# Keep the licenses and exact upstream/source provenance with the bundled helper.
XAD_LICENSE_DIR="$ROOT/ep128emu.app/Contents/Resources/XAD-Licenses"
rm -rf "$XAD_LICENSE_DIR"
mkdir -p "$XAD_LICENSE_DIR"
cp "$ROOT/third_party/t081-xad/XADMaster/LICENSE" \
  "$XAD_LICENSE_DIR/XADMaster-LGPL-2.1.txt"
cp "$ROOT/third_party/t081-xad/UniversalDetector/LICENSE" \
  "$XAD_LICENSE_DIR/UniversalDetector-LGPL-2.1.txt"
cp "$ROOT/third_party/t081-xad/UPSTREAM.md" \
  "$XAD_LICENSE_DIR/SOURCE-PROVENANCE.md"

# FLUID 1.4.5 emits trailing spaces in the one generated header tracked by
# this repository. Normalize them so a clean source build does not dirty Git.
if [ -f gui/gui_fl.hpp ]; then
  /usr/bin/perl -pi -e 's/[ \t]+$//' gui/gui_fl.hpp
fi
