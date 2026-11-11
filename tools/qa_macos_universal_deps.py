#!/usr/bin/env python3
"""Fail closed when Mac build dependencies do not contain both architecture slices.

This is a preflight only. It cannot qualify final bundles, signing or runtime.
"""
from pathlib import Path
import argparse
import subprocess
import sys

TARGET = {'arm64', 'x86_64'}
ROOT = Path.home() / 'Developer/EP128/EP128EMU'
FLTK = ROOT / 'third_party/fltk-1.4.5/lib'
BREW = Path('/opt/homebrew/lib')
LIBRARIES = [
    FLTK / 'libfltk.dylib', FLTK / 'libfltk_gl.dylib',
    FLTK / 'libfltk_images.dylib', BREW / 'libportaudio.dylib',
    BREW / 'libsndfile.dylib', BREW / 'libFLAC.dylib',
    BREW / 'libogg.dylib', BREW / 'libvorbis.dylib',
    BREW / 'libmp3lame.dylib', BREW / 'liblua.dylib',
]

def main():
    parser = argparse.ArgumentParser(description='macOS Universal dependency slice audit')
    parser.add_argument('--local-fat-fltk', action='store_true',
                        help='inspect the isolated project-local 2.2 FLTK build')
    parser.add_argument('--local-fat-portaudio', action='store_true',
                        help='inspect isolated project-local PortAudio 19.7.0 build')
    parser.add_argument('--local-universal-deps', action='store_true',
                        help='inspect all isolated FLTK, PortAudio and codec trial dependencies')
    args = parser.parse_args()
    libraries = list(LIBRARIES)
    if args.local_fat_fltk:
        base = Path(__file__).resolve().parent.parent / 'build/universal-deps/fltk-1.4.5-fat/lib'
        libraries[:3] = [base / p.name for p in LIBRARIES[:3]]
    if args.local_fat_portaudio:
        base = Path(__file__).resolve().parent.parent / 'build/universal-deps/portaudio-fat/lib'
        libraries[3] = base / 'libportaudio.dylib'
    if args.local_universal_deps:
        base = Path(__file__).resolve().parent.parent / 'build/universal-deps'
        names = ['fltk-1.4.5-fat/lib/libfltk.dylib',
                 'fltk-1.4.5-fat/lib/libfltk_gl.dylib',
                 'fltk-1.4.5-fat/lib/libfltk_images.dylib',
                 'portaudio-fat/lib/libportaudio.dylib',
                 'libsndfile-fat/lib/libsndfile.dylib',
                 'flac-fat/lib/libFLAC.dylib',
                 'libogg-fat/lib/libogg.dylib',
                 'libvorbis-fat/lib/libvorbis.dylib',
                 'lame-fat/lib/libmp3lame.dylib',
                 'lua-fat/lib/liblua.dylib']
        libraries = [base / name for name in names]
    if sys.platform != 'darwin':
        print('UNIVERSAL_DEP_GATE_FAIL: macOS required')
        return 2
    failed = []
    for library in libraries:
        if not library.is_file():
            failed.append((library, 'MISSING'))
            continue
        try:
            result = subprocess.run(['lipo', '-archs', str(library)],
                                    check=True, capture_output=True, text=True)
            missing = TARGET - set(result.stdout.strip().split())
            if missing:
                failed.append((library, 'MISSING ARCH ' + ','.join(sorted(missing))))
        except (OSError, subprocess.CalledProcessError):
            failed.append((library, 'LIPO ERROR'))
    for library, problem in failed:
        print('UNIVERSAL_DEP_FAIL:', library, problem)
    if failed:
        print('UNIVERSAL_DEP_GATE_NOT_READY: %d/%d required inputs failed' %
              (len(failed), len(libraries)))
        return 2
    print('UNIVERSAL_DEP_GATE_PASS: %d/%d inputs arm64+x86_64' %
          (len(libraries), len(libraries)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
