#!/bin/sh
set -eu

APP=${1:?usage: bundle-macos-deps.sh /path/to/app}
APP=$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# Every signed/bundled app must contain the machine-specific Dock artwork.
# This also covers release builds copied from an older qualified .app stage.
mkdir -p "$APP/Contents/Resources"
for machine in zx128emu cpc464emu tvc64emu; do
  cp -pf "$ROOT/resource/$machine.png" "$APP/Contents/Resources/$machine.png"
done
FW="$APP/Contents/Frameworks"
MACOS="$APP/Contents/MacOS"
DEFAULT_FLTK_PREFIX="$ROOT/../third_party/fltk-1.4.5"
EXTRA_PREFIX=${EP128EMU_FLTK_PREFIX:-$DEFAULT_FLTK_PREFIX}
# Explicit isolated Universal trial: never collect ARM-only Homebrew codecs.
UNIVERSAL_ROOT=${EP128EMU_UNIVERSAL_DEPS_ROOT:-}
if [ -n "$UNIVERSAL_ROOT" ]; then
  [ -d "$UNIVERSAL_ROOT" ] || { echo 'UNIVERSAL_ROOT_MISSING' >&2; exit 1; }
  UNIVERSAL_ROOT=$(cd "$UNIVERSAL_ROOT" && pwd -P)
  [ "$UNIVERSAL_ROOT" = "$ROOT/build/universal-deps" ] || {
    echo 'UNIVERSAL_ROOT_OUTSIDE_THIS_WORKTREE' >&2; exit 1;
  }
fi

[ -d "$EXTRA_PREFIX/lib" ] || {
  echo "Missing pinned FLTK runtime prefix: $EXTRA_PREFIX" >&2
  exit 1
}

# Always rebuild Frameworks from the executable dependency graph. Reusing an
# existing directory can preserve an older ABI-compatible filename with an
# incompatible implementation (for example FLTK 1.4.0 vs 1.4.5).
rm -rf "$FW"
mkdir -p "$FW"

# Fat Mach-O otool prints a separate non-indented header per architecture.
# Consider only indented load commands, never architecture headings as deps.
otool_deps() {
  otool -L "$1" | awk '/^[[:space:]]/ {print $1}'
}

is_system_dep() {
  case "$1" in
    /System/Library/*|/usr/lib/*) return 0 ;;
    *) return 1 ;;
  esac
}

resolve_dep() {
  dep=$1
  if [ -n "$UNIVERSAL_ROOT" ]; then
    case "$dep" in
      "$UNIVERSAL_ROOT"/*)
        [ -f "$dep" ] && { printf '%s\n' "$dep"; return 0; }
        return 1 ;;
      @rpath/*)
        base=${dep#@rpath/}
        for src in "$UNIVERSAL_ROOT"/*-fat/lib/"$base"; do
          [ -f "$src" ] && { printf '%s\n' "$src"; return 0; }
        done
        return 1 ;;
      /Users/*|/opt/homebrew/*|/usr/local/*) return 1 ;;
    esac
  fi
  if [ -n "$EXTRA_PREFIX" ]; then
    case "$dep" in
      "$EXTRA_PREFIX"/*) printf '%s\n' "$dep"; return 0 ;;
    esac
  fi
  case "$dep" in
    /opt/homebrew/*|/usr/local/*) printf '%s\n' "$dep" ;;
    @rpath/*)
      base=${dep#@rpath/}
      if [ -n "$EXTRA_PREFIX" ] && [ -e "$EXTRA_PREFIX/lib/$base" ]; then
        printf '%s\n' "$EXTRA_PREFIX/lib/$base"; return 0
      fi
      for p in /opt/homebrew/lib /opt/homebrew/opt/*/lib /usr/local/lib /usr/local/opt/*/lib; do
        [ -e "$p/$base" ] && { printf '%s\n' "$p/$base"; return 0; }
      done
      return 1
      ;;
    *) return 1 ;;
  esac
}

queue=/tmp/ep128emu-bundle-queue.$$
seen=/tmp/ep128emu-bundle-seen.$$
trap 'rm -f "$queue" "$seen"' EXIT HUP INT TERM
: > "$queue"
: > "$seen"

for exe in "$MACOS"/*; do
  [ -f "$exe" ] || continue
  file "$exe" | grep -q 'Mach-O' || continue
  printf '%s\n' "$exe" >> "$queue"
done

# sdl12-compat and sdl2-compat load the next compatibility layer with
# dlopen(), so these runtime dependencies are invisible to otool. Bundle
# them under the exact names searched through @loader_path.
if [ -n "$UNIVERSAL_ROOT" ]; then
  # SDL compatibility layers load each other via dlopen, not Mach-O LC_LOAD_DYLIB.
  for spec in sdl2compat-fat/lib/libSDL2-2.0.0.dylib sdl3-fat/lib/libSDL3.0.dylib; do
    src="$UNIVERSAL_ROOT/$spec"
    [ -f "$src" ] || { echo "UNIVERSAL_SDL_RUNTIME_MISSING: $src" >&2; exit 1; }
    dst="$FW/$(basename "$src")"
    cp -L "$src" "$dst"
    chmod u+w "$dst"
    printf '%s\n' "$dst" >> "$queue"
  done
  # SDL2-compat opens SDL3 through dlopen("@loader_path/libSDL3.dylib")
  # during a dyld initializer. The pinned SDL3 install library is named
  # libSDL3.0.dylib, which otool cannot identify as a dlopen requirement.
  # Provide the exact required SONAME as a relative alias, source-only
  # packaging. Omitting it triggers SDL2-compat's Cocoa error-dialog path
  # from within dyld and deadlocks the Finder launch before main().
  [ -s "$FW/libSDL3.0.dylib" ] || { echo SDL3_RUNTIME_TARGET_MISSING >&2; exit 1; }
  ln -s libSDL3.0.dylib "$FW/libSDL3.dylib"
elif [ -e /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib ]; then
  cp -L /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib "$FW/libSDL2-2.0.0.dylib"
  chmod u+w "$FW/libSDL2-2.0.0.dylib"
  printf '%s\n' "$FW/libSDL2-2.0.0.dylib" >> "$queue"
fi
if [ -z "$UNIVERSAL_ROOT" ] && [ -e /opt/homebrew/opt/sdl3/lib/libSDL3.dylib ]; then
  cp -L /opt/homebrew/opt/sdl3/lib/libSDL3.dylib "$FW/libSDL3.dylib"
  chmod u+w "$FW/libSDL3.dylib"
  printf '%s\n' "$FW/libSDL3.dylib" >> "$queue"
fi

i=1
while :; do
  obj=$(sed -n "${i}p" "$queue")
  [ -n "$obj" ] || break
  i=$((i + 1))
  grep -Fqx "$obj" "$seen" && continue
  printf '%s\n' "$obj" >> "$seen"
  otool_deps "$obj" | while IFS= read -r dep; do
    is_system_dep "$dep" && continue
    src=$(resolve_dep "$dep" || true)
    if [ -z "$src" ]; then
      [ -z "$UNIVERSAL_ROOT" ] || { echo "UNIVERSAL_DEP_UNRESOLVED: $dep" >&2; exit 1; }
      continue
    fi
    base=$(basename "$dep")
    dst="$FW/$base"
    if [ ! -e "$dst" ]; then
      cp -L "$src" "$dst"
      chmod u+w "$dst"
      printf '%s\n' "$dst" >> "$queue"
    fi
  done
done

# Rewrite every bundled Mach-O to use the app-local Frameworks directory.
for obj in "$MACOS"/* "$FW"/*; do
  [ -f "$obj" ] || continue
  file "$obj" | grep -q 'Mach-O' || continue
  chmod u+w "$obj"
  otool_deps "$obj" | while IFS= read -r dep; do
    is_system_dep "$dep" && continue
    base=$(basename "$dep")
    [ -e "$FW/$base" ] || continue
    install_name_tool -change "$dep" "@rpath/$base" "$obj"
  done
  case "$obj" in
    "$FW"/*) install_name_tool -id "@rpath/$(basename "$obj")" "$obj" ;;
  esac
done

# Remove build-machine run paths from every bundled Mach-O. This is important
# even after dependency names are rewritten: otherwise a machine that happens
# to have Homebrew installed could load external libraries before the bundle.
for obj in "$MACOS"/* "$FW"/*; do
  [ -f "$obj" ] || continue
  file "$obj" | grep -q 'Mach-O' || continue
  otool -l "$obj" | awk '/cmd LC_RPATH/{getline; getline; print $2}' | sort -u | while IFS= read -r rp; do
    case "$rp" in
      /opt/homebrew/*|/usr/local/*|/opt/local/*|/Users/*)
        install_name_tool -delete_rpath "$rp" "$obj"
        ;;
      *)
        if [ -n "$EXTRA_PREFIX" ]; then
          case "$rp" in "$EXTRA_PREFIX"/*) install_name_tool -delete_rpath "$rp" "$obj" ;; esac
        fi
        ;;
    esac
  done
done

# Executables provide the only application-local run-path used by bundled dylibs.
for exe in "$MACOS"/*; do
  [ -f "$exe" ] || continue
  file "$exe" | grep -q 'Mach-O' || continue
  if ! otool -l "$exe" | grep -A2 LC_RPATH | grep -q '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$exe"
  fi
done

# Verify that no Homebrew/MacPorts/local-library references remain.
bad=0
for obj in "$MACOS"/* "$FW"/*; do
  [ -f "$obj" ] || continue
  file "$obj" | grep -q 'Mach-O' || continue
  if otool_deps "$obj" | grep -E '^(/opt/homebrew|/usr/local|/opt/local|/Users)/' >/dev/null; then
    echo "External dependency remains: $obj" >&2
    otool -L "$obj" | grep -E '/opt/homebrew|/usr/local|/opt/local|/Users/' >&2 || true
    bad=1
  fi
done

# Absolute build-machine RPATHs are also forbidden.
for obj in "$MACOS"/* "$FW"/*; do
  [ -f "$obj" ] || continue
  file "$obj" | grep -q 'Mach-O' || continue
  if otool -l "$obj" | awk '/cmd LC_RPATH/{getline; getline; print $2}' |      grep -E '^(/opt/homebrew|/usr/local|/opt/local|/Users)/' >/dev/null; then
    echo "External RPATH remains: $obj" >&2
    otool -l "$obj" | awk '/cmd LC_RPATH/{getline; getline; print $2}' |       grep -E '^(/opt/homebrew|/usr/local|/opt/local|/Users)/' >&2 || true
    bad=1
  fi
done
[ "$bad" -eq 0 ] || exit 1

# In Universal mode, every bundled executable and dylib must remain fat.
if [ -n "$UNIVERSAL_ROOT" ]; then
  for obj in "$MACOS"/* "$FW"/*; do
    [ -f "$obj" ] || continue
    file "$obj" | grep -q 'Mach-O' || continue
    slices=$(lipo -archs "$obj")
    sorted_archs=$(printf '%s\n' $slices | LC_ALL=C sort | tr '\n' ' ')
    [ "$sorted_archs" = "arm64 x86_64 " ] || {
      echo "UNIVERSAL_BUNDLE_MISSING_ARCH: $obj $slices" >&2; exit 1
    }
  done
fi

# install_name_tool invalidates signatures; apply reproducible ad-hoc signatures.
for dylib in "$FW"/*; do
  [ -f "$dylib" ] || continue
  file "$dylib" | grep -q 'Mach-O' || continue
  codesign --force --sign - --timestamp=none "$dylib" >/dev/null
done
# Sign nested helper executables first. Signing the app's main executable
# before its nested helpers leaves an invalid bundle signature on fat builds.
for exe in "$MACOS"/*; do
  [ -f "$exe" ] || continue
  [ "$(basename "$exe")" = ep128emu ] && continue
  file "$exe" | grep -q 'Mach-O' || continue
  codesign --force --sign - --timestamp=none "$exe" >/dev/null
  codesign --verify --strict "$exe"
done
main_exe="$MACOS/ep128emu"
[ -f "$main_exe" ] || { echo 'MAC_MAIN_EXECUTABLE_MISSING' >&2; exit 1; }
codesign --force --sign - --timestamp=none "$main_exe" >/dev/null
codesign --force --deep --sign - --timestamp=none "$APP" >/dev/null
codesign --verify --deep --strict "$APP"

echo "Bundled $(find "$FW" -type f -maxdepth 1 | wc -l | tr -d ' ') libraries into $FW"
