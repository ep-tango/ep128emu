# Build native Windows PE executable from a fresh public source checkout.
# This does not build an NSIS installer or redistribute any ROM.
param(
 [Parameter(Mandatory=$true)]
 [ValidateSet('win32','win64')][string]$Arch,
 [string]$MsysRoot='C:\msys64',
 [ValidateRange(1,32)][int]$Jobs=3
)
$ErrorActionPreference='Stop'
$sourceRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!(Test-Path (Join-Path $sourceRoot 'SConstruct'))){throw 'SConstruct not found'}
if(Test-Path (Join-Path $sourceRoot 'ep128emu.exe')){
 throw 'Existing executable: use a fresh checkout for each architecture'
}
$bash=Join-Path $MsysRoot 'usr\bin\bash.exe'
$cygpath=Join-Path $MsysRoot 'usr\bin\cygpath.exe'
if(!(Test-Path $bash) -or !(Test-Path $cygpath)){
 throw 'MSYS2 installation missing (bash/cygpath)'
}
$unixSource=(& $cygpath -u $sourceRoot).Trim()
if($LASTEXITCODE -ne 0 -or $unixSource.Contains("'")){throw 'Unsafe or invalid source directory'}
$archDir=if($Arch -eq 'win32'){'mingw32'}else{'ucrt64'}
$prefix=(& $cygpath -m (Join-Path $MsysRoot $archDir)).Trim()
$bin=(& $cygpath -u (Join-Path $MsysRoot ($archDir+'\bin'))).Trim()
$hostTools=(& $cygpath -u (Join-Path $MsysRoot 'ucrt64\bin')).Trim()
if($LASTEXITCODE -ne 0){throw 'Failed to find MSYS2 compiler paths'}
$script="set -e; export PATH='$bin':'$hostTools':/usr/bin:/bin; export EP128EMU_MINGW_PREFIX='$prefix'; cd '$unixSource'; scons -Q -j$Jobs $Arch=1 utils=0"
& $bash -lc $script
if($LASTEXITCODE -ne 0){throw "Native Windows source build failed: $Arch"}
$exe=Join-Path $sourceRoot 'ep128emu.exe'
if(!(Test-Path $exe) -or (Get-Item $exe).Length -lt 100000){
 throw 'The expected Windows PE executable was not generated'
}
Write-Host "PUBLIC_WINDOWS_$Arch_SOURCE_BUILD_PASS"
Write-Host ((Get-FileHash $exe -Algorithm SHA256).Hash)
