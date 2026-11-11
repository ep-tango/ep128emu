#!/usr/bin/env python3
"""Fail-closed, no-window runtime SDL2/SDL3 initializer smoke in both Mac archs.

The app is NOT launched: a self-contained tiny loader checks the exact dylib
constructor chain from the delivered app. Hard timeout prevents hanging tests.
"""
import argparse
import subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('app',type=Path)
p.add_argument('--arm64-only',action='store_true',help='only ARM64 SDL2-compat runtime')
a=p.parse_args()
app=a.app.resolve()
fw=app/'Contents'/'Frameworks'
alias=fw/'libSDL3.dylib'
if not alias.is_symlink() or alias.readlink()!=Path('libSDL3.0.dylib'):
 raise SystemExit('SDL3_DYNAMIC_LOADER_ALIAS_MISSING')
if not (fw/'libSDL3.0.dylib').is_file():
 raise SystemExit('SDL3_DYNAMIC_LOADER_TARGET_MISSING')
src=Path(__file__).with_name('qa_macos_sdl_loader_probe.c')
bin=Path('build/qa_macos_sdl_loader_probe')
bin.parent.mkdir(parents=True,exist_ok=True)
arches=('arm64',) if a.arm64_only else ('arm64','x86_64')
clang=['/usr/bin/clang']
for arch in arches: clang+=['-arch',arch]
subprocess.run(clang+['-mmacosx-version-min=11.0',str(src),'-o',str(bin)],check=True)
for arch in arches:
 try:
  result=subprocess.run(['/usr/bin/arch','-'+arch,str(bin.resolve()),
                str((fw/'libSDL2-2.0.0.dylib').resolve())],
                capture_output=True,text=True,timeout=12)
 except subprocess.TimeoutExpired:
  raise SystemExit(f'SDL2_COMPAT_DYLD_INITIALIZER_DEADLOCK_{arch}')
 if result.returncode or 'SDL2_SDL3_COMPAT_INITIALIZER_PASS' not in result.stdout:
  raise SystemExit(f'SDL2_COMPAT_RUNTIME_FAIL_{arch}: {result.stderr}')
 print(f'SDL2_COMPAT_DLOPEN_NO_GUI_{arch}_PASS')
print('ARM64_SDL2_SDL3_DYNAMIC_INITIALIZER_PASS' if a.arm64_only else 'DUALARCH_SDL2_SDL3_DYNAMIC_INITIALIZER_PASS')
