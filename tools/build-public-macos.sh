#!/bin/sh
# EP128Emu 2.1 portable source-only ARM64 build helper.
# Produces the executable, not a signed distributable app.
set -eu
cd "$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
: "${EP128EMU_FLTK_PREFIX:?Set EP128EMU_FLTK_PREFIX to your FLTK 1.4.5 prefix}"
test -x "$EP128EMU_FLTK_PREFIX/bin/fltk-config" || {
  echo "FLTK 1.4.5 fltk-config missing at the chosen prefix" >&2
  exit 2
}
test -x "$EP128EMU_FLTK_PREFIX/bin/fluid" || {
  echo "FLTK 1.4.5 fluid missing at the chosen prefix" >&2
  exit 2
}
export PATH="$EP128EMU_FLTK_PREFIX/bin:$PATH"
export EP128EMU_FLTK_CONFIG="$EP128EMU_FLTK_PREFIX/bin/fltk-config"
export EP128EMU_FLUID="$EP128EMU_FLTK_PREFIX/bin/fluid"
command -v scons >/dev/null || { echo "Install SCons first" >&2; exit 2; }
scons -Q fltkcheck=1
scons -Q -j 3 ep128emu.app/Contents/MacOS/ep128emu
test -s ep128emu.app/Contents/MacOS/ep128emu
echo PUBLIC_MAC_ARM64_SOURCE_BUILD_PASS
