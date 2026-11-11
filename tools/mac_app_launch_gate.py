#!/usr/bin/env python3
"""Noninteractive macOS package gate: ABI, dyld deps and codesign ONLY.

Never launch an ep128emu GUI from unattended package/CI verification. The
former four-second process-alive smoke could falsely PASS a modal error window
and exposed the logged-in desktop to stray GUI processes. Actual GUI behavior
requires an explicit supervised test and a separate user acceptance gate.
"""
import argparse
import re
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('app',type=Path)
parser.add_argument('--fltk-version',default='1.4.5')
parser.add_argument('--universal',action='store_true',help='require arm64+x86_64 closure')
a=parser.parse_args()
app=a.app.expanduser().resolve()
exe=app/'Contents'/'MacOS'/'ep128emu'
libs=app/'Contents'/'Frameworks'
if a.universal:
 loader_alias=libs/'libSDL3.dylib'
 loader_target=libs/'libSDL3.0.dylib'
 if not loader_alias.is_symlink() or loader_alias.readlink()!=Path('libSDL3.0.dylib') or not loader_target.is_file():
  raise RuntimeError(('SDL2_COMPAT_DLOPEN_SDL3_ALIAS_MISSING',loader_alias))
if not exe.is_file():
 raise RuntimeError(('MISSING_PACKAGED_EXECUTABLE',exe))
if not libs.is_dir():
 raise RuntimeError(('MISSING_FRAMEWORKS_DIRECTORY',libs))

def run(*args):
 return subprocess.run(list(args),capture_output=True,text=True,check=True).stdout

file_output=run('file','-b',str(exe))
if a.universal:
 if set(run('lipo','-archs',str(exe)).split()) != {'arm64','x86_64'}:
  raise RuntimeError(('NOT_UNIVERSAL_MAIN_EXECUTABLE',file_output))
elif 'arm64' not in run('lipo','-archs',str(exe)).split():
 raise RuntimeError(('NOT_MAC_ARM64',file_output))
for lib in ('fltk','fltk_images','fltk_jpeg','fltk_png','fltk_z','fltk_gl'):
 p=libs/('lib'+lib+'.1.4.dylib')
 if not p.is_file():
  raise RuntimeError(('MISSING_FLTK_LIBRARY',p))
 ls=run('otool','-L',str(p)).splitlines()
 # Universal Mach-O adds a separate architecture header before each slice.
 if not any('current version '+a.fltk_version in x for x in ls):
  raise RuntimeError(('FLTK_ABI_MISMATCH',p))
run('codesign','--verify','--deep','--strict',str(app))

# Read-only load command inspection, no process launch or Cocoa windows.
imported=0
binaries=([exe]+sorted(p for p in libs.glob('*.dylib') if not p.is_symlink()))
if a.universal:
 binaries+=sorted(p for p in (app/'Contents'/'MacOS').iterdir() if p.is_file() and p!=exe and 'Mach-O' in run('file','-b',str(p)))
for binary in binaries:
 if a.universal:
  archs=set(run('lipo','-archs',str(binary)).split())
  if archs!={'arm64','x86_64'}:
   raise RuntimeError(('UNIVERSAL_CLOSURE_ARCH_MISMATCH',binary,sorted(archs)))
  deployment=run('xcrun','vtool','-show-build',str(binary))
  minimums=re.findall(r'^\s*minos\s+(\d+(?:\.\d+)*)\s*$',deployment,re.MULTILINE)
  if len(minimums)!=2 or any(tuple(int(x) for x in m.split('.'))>(11,0) for m in minimums):
   raise RuntimeError(('UNIVERSAL_MACOS_11_DEPLOYMENT_MISMATCH',binary,minimums))
 entries=run('otool','-L',str(binary)).splitlines()[1:]
 for entry in entries:
  # Only a Mach-O load-command row is indented. Architecture headers
  # must never be misclassified as absolute build-machine paths.
  if not entry[:1].isspace():
   continue
  dep=entry.strip().split(' (',1)[0]
  if dep.startswith('@rpath/'):
   target=libs/dep[len('@rpath/'):]
   if not target.exists():
    raise RuntimeError(('UNBUNDLED_RPATH_DEPENDENCY',binary,dep))
  elif dep.startswith('@loader_path/'):
   target=binary.parent/dep[len('@loader_path/'):]
   if not target.exists():
    raise RuntimeError(('BROKEN_LOADER_PATH',binary,dep))
  elif dep.startswith('@executable_path/'):
   target=exe.parent/dep[len('@executable_path/'):]
   if not target.exists():
    raise RuntimeError(('BROKEN_EXECUTABLE_PATH',binary,dep))
  elif dep.startswith('/usr/lib/') or dep.startswith('/System/Library/'):
   pass
  else:
   raise RuntimeError(('NONPORTABLE_DYLD_DEPENDENCY',binary,dep))
  imported+=1

if a.universal:
 print('MAC_PACKAGED_STATIC_UNIVERSAL_FLTK_ABI_DYLD_CODESIGN_PASS',app,'binaries=',len(binaries),'deps=',imported)
else:
 print('MAC_PACKAGED_STATIC_ARM64_FLTK_ABI_DYLD_CODESIGN_PASS',app,'deps=',imported)
print('MAC_GUI_USER_ACCEPTANCE_NOT_TESTED_BY_STATIC_GATE')
