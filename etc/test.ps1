<#
.SYNOPSIS
Build and run the host-side firmware tests in test/.

.DESCRIPTION
Compiles test/protocol_test.cpp natively with MSVC. The test includes
src/misdirection/misdirection.ino directly over a stub Arduino core, so
the parser and dispatch code under test is the exact source that gets
flashed, with no Teensy attached.

Requires Visual Studio (any edition) with the "Desktop development with
C++" workload; the script locates it with vswhere.

.EXAMPLE
etc\test.ps1
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $repo '.build\test'
New-Item -ItemType Directory -Force $out | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found; install Visual Studio with the C++ workload."
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw "No Visual Studio install with the C++ toolset was found."
}
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$src = Join-Path $repo 'test\protocol_test.cpp'
$exe = Join-Path $out  'protocol_test.exe'

# cl.exe needs the vcvars environment; run it through cmd so the batch
# file can set up PATH/INCLUDE/LIB for that one process.
$cl = "cl /nologo /EHsc /W3 /std:c++17 /Fo`"$out\\`" /Fe`"$exe`" `"$src`""
cmd /c "`"$vcvars`" >nul 2>&1 && $cl"
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }

& $exe
exit $LASTEXITCODE
