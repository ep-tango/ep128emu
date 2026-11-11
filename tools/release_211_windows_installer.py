#!/usr/bin/env python3
"""Generate PUBLIC 2.1.1 NSIS scripts that upgrade the original v2.1.

Preserve v2.1 installed identity, ROMs, user data and install path.
Compiling this script does not launch or upgrade installed software.
"""
from pathlib import Path
import argparse
import re

p=argparse.ArgumentParser(description="2.1.1 official Windows NSIS release installer")
p.add_argument('arch',choices=('win64','win32'))
a=p.parse_args()
arch=a.arch
qa=False
side=False
root=Path(r'C:\Users\Public\Developer\EP128EMU\build\release211-'+arch)
stage=root/'stage'
# Backslashes represent native Windows paths even on the macOS generator host.
def w(p):return str(p).replace("/",chr(92))
stage_mac=Path('build/installers/2.1.1')/(arch+'-stage')
files=sorted(p.relative_to(stage_mac).as_posix() for p in stage_mac.rglob('*') if p.is_file())
assert len(files)>=115 and all('..' not in Path(n).parts for n in files)
assert {'ep128emu.exe','makecfg.exe','tapeedit.exe','COPYING','config/ep_keys.cfg'}.issubset(files)
assert 'SETUP-CONFIG.cmd' not in files
catalog=Path('gui/rom_archive_catalog.hpp').read_text()
standard72=re.findall(r'^\s+"([^"\\]+\.rom)",$',catalog,re.M)
assert len(standard72)==72 and len(set(standard72))==72
assert 'exos24uk.rom' in standard72 and 'tvc22_sys.rom' in standard72
archlabel='x64' if arch=='win64' else 'x86'
install_id=f'ep128emu-custom-{archlabel}'
menuname=f'EP128emu {archlabel}'
productname=f'EP128emu 2.1.1 {archlabel}'
programfiles='$PROGRAMFILES64' if arch=='win64' else '$PROGRAMFILES'
regview='64' if arch=='win64' else '32'
# Retain the existing uninstall identity even for a user-selected install path.
key='Software'+chr(92)+'Microsoft'+chr(92)+'Windows'+chr(92)+'CurrentVersion'+chr(92)+'Uninstall'+chr(92)+install_id
out=root/(f'ep128emu-2.1.1-{arch}.exe')  # exact concise public filename
lines=[
    'Unicode True',
    'Target x86-unicode' if arch == 'win32' else 'Target amd64-unicode',
    f'Name "{productname}"',f'OutFile "{w(out)}"',
    f'InstallDir "{programfiles}\\{install_id}"',
    'RequestExecutionLevel admin', 'SetCompressor /SOLID lzma',
    '!include "MUI2.nsh"', '!define MUI_ABORTWARNING',
    '!insertmacro MUI_PAGE_WELCOME',
    f'!insertmacro MUI_PAGE_LICENSE "{w(stage/"COPYING")}"',
    '!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave',
    '!insertmacro MUI_PAGE_DIRECTORY',
    '!insertmacro MUI_PAGE_INSTFILES', '!insertmacro MUI_PAGE_FINISH',
    '!insertmacro MUI_UNPAGE_CONFIRM', '!insertmacro MUI_UNPAGE_INSTFILES',
    '!insertmacro MUI_LANGUAGE "English"',
    'Var HadExe', 'Var SetupExit', 'Var PreviousInstallDir',
    'Function .onInit',
    f'  SetRegView {regview}',
    '  StrCpy $PreviousInstallDir ""',
    f'  ReadRegStr $0 HKLM "{key}" "InstallLocation"',
    '  StrCmp $0 "" noPreviousInstallation',
    '  IfFileExists "$0\\ep128emu.exe" 0 noPreviousInstallation',
    '  StrCpy $PreviousInstallDir "$0"',
    '  StrCpy $INSTDIR "$0"',
    '  noPreviousInstallation:',
    'FunctionEnd',
    'Function DirectoryLeave',
    '  StrCmp $PreviousInstallDir "" directoryAccepted',
    '  StrCmp $INSTDIR $PreviousInstallDir directoryAccepted',
    '  MessageBox MB_ICONEXCLAMATION|MB_OK "This installation identity is already registered at $PreviousInstallDir.$\\r$\\nUpgrade this instance in its original folder. A separate installation requires a different installed identity. Do not uninstall to update."',
    '  Abort',
    '  directoryAccepted:',
    'FunctionEnd',
    'Section "EP128emu, ROMs and tools" SecMain',
    '  SectionIn RO', '  SetShellVarContext all',f'  SetRegView {regview}',
    '  StrCpy $HadExe "0"',
    '  IfFileExists "$INSTDIR\\ep128emu.exe" 0 +2',
    '  StrCpy $HadExe "1"',
    '  CreateDirectory "$INSTDIR\\config"',
    *[f'  CreateDirectory "$INSTDIR\\config\\{x}"' for x in ('cpc','ep128brd','ep128esp','ep128hun','ep128uk','ep64','tvc','zx')],
    *[f'  CreateDirectory "$INSTDIR\\{x}"' for x in ('demo','disk','files','roms','snapshot','tape')],
]
prevdir=None;prev_immutable=None
for rel in files:
    parts=rel.split('/')
    dirname='/'.join(parts[:-1]);filename=parts[-1]
    immutable=parts[0] in ('config','roms','disk','demo','files','snapshot','tape')
    # Install program binaries on update; never overwrite even known-name user material.
    if immutable!=prev_immutable:
        lines.append('  SetOverwrite '+('off' if immutable else 'on'))
        prev_immutable=immutable
    if dirname!=prevdir:
        lines.append('  SetOutPath "$INSTDIR'+('\\'+dirname.replace('/','\\') if dirname else '')+'"')
        prevdir=dirname
    lines.append(f'  File /oname={filename} "{w(stage/rel)}"')
lines.extend([
    '  SetOutPath "$INSTDIR"',
    '  WriteUninstaller "$INSTDIR\\uninstall.exe"',
    f'  CreateDirectory "$SMPROGRAMS\\{menuname}"',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\EP128emu.lnk" "$INSTDIR\\ep128emu.exe"',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Spectrum.lnk" "$INSTDIR\\ep128emu.exe" "-zx" "$INSTDIR\\ep128emu.exe" 1',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Amstrad CPC.lnk" "$INSTDIR\\ep128emu.exe" "-cpc" "$INSTDIR\\ep128emu.exe" 2',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Videoton TVC.lnk" "$INSTDIR\\ep128emu.exe" "-tvc" "$INSTDIR\\ep128emu.exe" 3',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Tape editor.lnk" "$INSTDIR\\tapeedit.exe"',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Configuration utility.lnk" "$INSTDIR\\makecfg.exe"',
    f'  CreateShortcut "$SMPROGRAMS\\{menuname}\\Uninstall.lnk" "$INSTDIR\\uninstall.exe"',
    f'  WriteRegStr HKLM "{key}" "DisplayName" "{productname}"',
    f'  WriteRegStr HKLM "{key}" "UninstallString" "$\\\"$INSTDIR\\uninstall.exe$\\\""',
    f'  WriteRegStr HKLM "{key}" "InstallLocation" "$INSTDIR"',
    'SectionEnd',
    'Section "Create machine presets on first installation" SecConfig',
    '  SectionIn RO',
    '  StrCmp $HadExe "1" skipConfig',
    # A leftover config after uninstall may be user-owned: do not force-write it.
    *[f'  IfFileExists "$INSTDIR\\config\\{x}\\*.cfg" skipConfig' for x in ('cpc','ep128brd','ep128esp','ep128hun','ep128uk','ep64','tvc','zx')],
    '  ExecWait \'"$INSTDIR\\makecfg.exe" -p "$INSTDIR"\' $SetupExit',
    '  IntCmp $SetupExit 0 skipConfig configFail configFail',
    '  configFail:',
    '  MessageBox MB_ICONSTOP "Machine preset configuration failed (code $SetupExit)."',
    '  Abort "Machine presets were not generated"',
    '  skipConfig:',
    'SectionEnd',
    'Section "Install missing system ROMs (optional)" SecROM',
    '  SetOutPath "$INSTDIR"',
    *[f'  IfFileExists "$INSTDIR\\roms\\{n}" 0 missingROM' for n in standard72],
    '  Goto romDownloadDone',
    '  missingROM:',
    '  MessageBox MB_YESNO|MB_ICONQUESTION "The standard ROM archive is incomplete here. Download available firmware now? The original 72-ROM archive includes TVC and EXOS 2.4 ROMs. Existing ROMs and settings will be preserved." IDNO romDownloadDone',
    '  ExecWait \'"$INSTDIR\\makecfg.exe" -r "$INSTDIR"\' $SetupExit',
    '  IfFileExists "$INSTDIR\\roms\\*.rom" romDownloadDone',
    '  DetailPrint "ROM download was skipped or failed; recovery remains available in EP128emu Configuration menu."',
    '  romDownloadDone:',
    'SectionEnd',
    'Section "Uninstall"',
    '  SetShellVarContext all',f'  SetRegView {regview}',
    f'  Delete "$SMPROGRAMS\\{menuname}\\*.lnk"',
    f'  RMDir "$SMPROGRAMS\\{menuname}"',
    f'  DeleteRegKey HKLM "{key}"',
])
# Preserve all files in data directories, even if installed from our package.
# External user-created files are by construction never explicitly deleted.
protect={'config','disk','demo','files','roms','snapshot','tape'}
for rel in files:
    if rel.split('/')[0] in protect:continue
    lines.append(f'  Delete "$INSTDIR\\{rel.replace("/",chr(92))}"')
lines += ['  Delete "$INSTDIR\\uninstall.exe"',
          '  RMDir "$INSTDIR\\licenses"',
          '  RMDir "$INSTDIR\\BetaDisk"',
          '  RMDir "$INSTDIR\\SpeccyDOS"',
          '  RMDir "$INSTDIR\\resource"',
          '  RMDir "$INSTDIR"',
          'SectionEnd']
output=Path('build/installers/2.1.1')/(arch+'-public.nsi')
output.write_text('\n'.join(lines)+'\n')
assert 'makecfg.exe" -p "$INSTDIR"' in output.read_text()
assert 'SetOverwrite off' in output.read_text()
assert ('Target x86-unicode' if arch == 'win32' else 'Target amd64-unicode') in output.read_text()
print('NSIS_GENERATED_PASS',arch,'qa',qa,'side_by_side',side,'files',len(files),'out',output)
