; EP128EMU 2.2.1 public source installer entrypoint.
; Exact same component list, directory selector and upgrade-cleanup policy
; as the audited native 2.2.1 NSIS build. Never include local build paths.
; Example (Windows cmd):
;   makensis /DWIN64 /DEP128EMU_STAGE=C:\stage\ep128emu /DEP128EMU_OUTFILE=C:\output\ep128emu-2.2.1-win64.exe ep128emu.nsi
; WIN32 uses /DWIN32 and an x86 stage.
!ifndef EP128EMU_STAGE
  !error "Supply /DEP128EMU_STAGE=<directory of staged payload>."
!endif
!ifndef EP128EMU_OUTFILE
  !error "Supply /DEP128EMU_OUTFILE=<output EXE filename>."
!endif
!ifdef WIN64
  !include "${__FILEDIR__}\ep128emu-win64.nsi"
!else
  !ifdef WIN32
    !include "${__FILEDIR__}\ep128emu-win32.nsi"
  !else
    !error "Supply exactly one of /DWIN64 or /DWIN32."
  !endif
!endif
