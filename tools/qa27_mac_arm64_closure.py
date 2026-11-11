#!/usr/bin/env python3
"""Fail closed unless ALL bundled Mach-O binaries are single-slice ARM64."""
import subprocess
import sys
from pathlib import Path
app=Path(sys.argv[1]).resolve()
assert app.is_dir(), ('MISSING_APP',app)
macos=app/'Contents/MacOS'
frameworks=app/'Contents/Frameworks'
assert frameworks.is_dir()
needed={'ep128emu','epmakecfg','tapeedit','ep128emu-unar','ep128emu-ffmpeg'}
assert all((macos/n).is_file() for n in needed), 'MISSING_HELPER'
items=list(macos.iterdir())+list(frameworks.iterdir())
count=0
for p in items:
    if p.is_symlink():
        assert p.resolve().is_relative_to(app), ('EXTERNAL_SYMLINK',p)
        continue
    if not p.is_file():
        continue
    typ=subprocess.run(['file','-b',str(p)],capture_output=True,text=True,check=True).stdout
    if 'Mach-O' not in typ:
        continue
    arch=subprocess.run(['lipo','-archs',str(p)],capture_output=True,text=True,check=True).stdout.split()
    assert arch==['arm64'], ('NOT_PURE_ARM64',str(p),arch)
    count+=1
assert count>=len(needed)+5, ('INCOMPLETE_ARM64_CLOSURE',count)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
print('MAC_ARM64_ONLY_COMPLETE_DYLIB_AND_HELPER_CLOSURE_PASS',count)
