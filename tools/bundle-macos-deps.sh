#!/bin/sh
set -eu

APP=${1:?usage: bundle-macos-deps.sh /path/to/app}
APP=$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FW="$APP/Contents/Frameworks"
MACOS="$APP/Contents/MacOS"
DEFAULT_FLTK_PREFIX="$ROOT/../third_party/fltk-1.4.5"
EXTRA_PREFIX=${EP128EMU_FLTK_PREFIX:-$DEFAULT_FLTK_PREFIX}

[ -d "$EXTRA_PREFIX/lib" ] || {
  echo "Missing pinned FLTK runtime prefix: $EXTRA_PREFIX" >&2
  exit 1
}

# Always rebuild Frameworks from the executable dependency graph. Reusing an
# existing directory can preserve an older ABI-compatible filename with an
# incompatible implementation (for example FLTK 1.4.0 vs 1.4.5).
rm -rf "$FW"
mkdir -p "$FW"

is_system_dep() {
  case "$1" in
    /System/Library/*|/usr/lib/*) return 0 ;;
    *) return 1 ;;
  esac
}

resolve_dep() {
  dep=$1
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
if [ -e /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib ]; then
  cp -L /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib "$FW/libSDL2-2.0.0.dylib"
  chmod u+w "$FW/libSDL2-2.0.0.dylib"
  printf '%s\n' "$FW/libSDL2-2.0.0.dylib" >> "$queue"
fi
if [ -e /opt/homebrew/opt/sdl3/lib/libSDL3.dylib ]; then
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
  otool -L "$obj" | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
    is_system_dep "$dep" && continue
    src=$(resolve_dep "$dep" || true)
    [ -n "$src" ] || continue
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
  otool -L "$obj" | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
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
  otool -l "$obj" | awk '/cmd LC_RPATH/{getline; getline; print $2}' | while IFS= read -r rp; do
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
  if otool -L "$obj" | tail -n +2 | awk '{print $1}' | grep -E '^(/opt/homebrew|/usr/local|/opt/local|/Users)/' >/dev/null; then
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

# install_name_tool invalidates signatures; apply reproducible ad-hoc signatures.
for dylib in "$FW"/*; do
  [ -f "$dylib" ] || continue
  file "$dylib" | grep -q 'Mach-O' || continue
  codesign --force --sign - --timestamp=none "$dylib" >/dev/null
done
for exe in "$MACOS"/*; do
  [ -f "$exe" ] || continue
  file "$exe" | grep -q 'Mach-O' || continue
  codesign --force --sign - --timestamp=none "$exe" >/dev/null
done
codesign --force --deep --sign - --timestamp=none "$APP" >/dev/null
codesign --verify --deep --strict "$APP"

echo "Bundled $(find "$FW" -type f -maxdepth 1 | wc -l | tr -d ' ') libraries into $FW"
