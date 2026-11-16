#!/bin/sh
# Portable Linux launcher. Never changes legacy profiles unless explicitly selected.
set -eu
if [ "$#" -gt 0 ] && [ "$1" = "--profile" ]; then
    [ "$#" -ge 3 ] || { echo "Usage: ./run --profile /absolute/path PROGRAM [args]" >&2; exit 2; }
    case "$2" in /*) EP128EMU_HOME=$2; export EP128EMU_HOME ;; *)
        echo "Profile directory must be an absolute path" >&2; exit 2 ;;
    esac
    shift 2
fi
[ "$#" -gt 0 ] || {
    echo 'Usage: ./run [--profile /absolute/path] ep128emu|zx128emu|cpc464emu|tvc64emu|epmakecfg|tapeedit [args]' >&2
    exit 2
}
program=$1; shift
case "$program" in
    ep128emu|zx128emu|cpc464emu|tvc64emu|epmakecfg|tapeedit|epcompress|epimgconv|iview2png) ;;
    *) echo "Unsupported helper name: $program" >&2; exit 2 ;;
esac
base=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
if [ -n "${LD_LIBRARY_PATH-}" ]; then LD_LIBRARY_PATH="$base/lib:$LD_LIBRARY_PATH"; else LD_LIBRARY_PATH="$base/lib"; fi
export LD_LIBRARY_PATH
# Arch field test proved -no-opengl gives correct display. Default to
# software on Arch, preserve explicit -opengl override on any distro.
case "$program" in ep128emu|zx128emu|cpc464emu|tvc64emu)
    if [ -r /etc/os-release ]; then
        ID= ID_LIKE=
        . /etc/os-release
        case " $ID $ID_LIKE " in
            *" arch "*)
                choice=default
                for arg do
                    case "$arg" in -opengl|-no-opengl) choice=explicit ;; esac
                done
                if [ "$choice" = default ]; then
                    echo "EP128emu: Arch compatibility mode (software video); -opengl opts in" >&2
                    set -- -no-opengl "$@"
                fi
            ;;
        esac
    fi
;; esac
exec "$base/bin/$program" "$@"
