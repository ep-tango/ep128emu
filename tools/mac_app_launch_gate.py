#!/usr/bin/env python3
"""Noninteractive macOS package gate: ABI, dyld deps and codesign ONLY.

Never launch an ep128emu GUI from unattended package/CI verification. The
former four-second process-alive smoke could falsely PASS a modal error window
and exposed the logged-in desktop to stray GUI processes. Actual GUI behavior
requires an explicit supervised test and a separate user acceptance gate.
"""
import argparse
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('app',type=Path)
parser.add_argument('--fltk-version',default='1.4.5')
a=parser.parse_args()
app=a.app.expanduser().resolve()
exe=app/'Contents'/'MacOS'/'ep128emu'
libs=app/'Contents'/'Frameworks'
if not exe.is_file():
 raise RuntimeError(('MISSING_PACKAGED_EXECUTABLE',exe))
if not libs.is_dir():
 raise RuntimeError(('MISSING_FRAMEWORKS_DIRECTORY',libs))

def run(*args):
 return subprocess.run(list(args),capture_output=True,text=True,check=True).stdout

file_output=run('file','-b',str(exe))
if 'Mach-O 64-bit executable arm64' not in file_output:
 raise RuntimeError(('NOT_MAC_ARM64',file_output))
for lib in ('fltk','fltk_images','fltk_jpeg','fltk_png','fltk_z','fltk_gl'):
 p=libs/('lib'+lib+'.1.4.dylib')
 if not p.is_file():
  raise RuntimeError(('MISSING_FLTK_LIBRARY',p))
 ls=run('otool','-L',str(p)).splitlines()
 if len(ls)<2 or 'current version '+a.fltk_version not in ls[1]:
  raise RuntimeError(('FLTK_ABI_MISMATCH',p))
run('codesign','--verify','--deep','--strict',str(app))

# Read-only load command inspection, no process launch or Cocoa windows.
imported=0
for binary in [exe]+sorted(libs.glob('*.dylib')):
 entries=run('otool','-L',str(binary)).splitlines()[1:]
 for entry in entries:
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

print('MAC_PACKAGED_STATIC_ARM64_FLTK_ABI_DYLD_CODESIGN_PASS',app,'deps=',imported)
print('MAC_GUI_USER_ACCEPTANCE_NOT_TESTED_BY_STATIC_GATE')
