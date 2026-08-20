<#
.SYNOPSIS
Compile a sketch from src/ for the Teensy 4.1.

.DESCRIPTION
Wraps the arduino-cli bundled with Arduino IDE 2.x. The USB Type comes
from the table in etc/common.ps1 unless you override it, so each sketch
builds the way it is meant to without anyone remembering a menu setting.

.EXAMPLE
etc\build.ps1 uart_echo

.EXAMPLE
etc\build.ps1 -All
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Sketch,

    [ValidateSet('hid', 'serial', 'serialhid', 'keyboard', 'touch', 'hidtouch')]
    [string]$UsbType,

    [switch]$All,
    [switch]$Clean,
    [switch]$List,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if ($List) { Get-SketchNames; return }

if (-not $Sketch -and -not $All) {
    Write-Host "Usage: etc\build.ps1 <sketch> [-UsbType hid|serial|...] [-Clean] [-DryRun]"
    Write-Host "       etc\build.ps1 -All"
    Write-Host ""
    Write-Host "Sketches in src/:"
    foreach ($n in Get-SketchNames) {
        "  {0,-20} usb={1}" -f $n, (Resolve-UsbType -Sketch $n) | Write-Host
    }
    return
}

$targets = if ($All) { Get-SketchNames } else { @($Sketch) }

foreach ($name in $targets) {
    $dir       = Resolve-Sketch $name
    $usb       = Resolve-UsbType -Sketch $name -Override $UsbType
    $fqbn      = Get-Fqbn $usb
    $buildPath = Join-Path $RepoRoot ".build\$name"

    if ($Clean -and (Test-Path -LiteralPath $buildPath)) {
        Remove-Item -LiteralPath $buildPath -Recurse -Force
    }

    Write-Host "==> $name (usb=$usb)" -ForegroundColor Cyan

    $cliArgs = @(
        'compile'
        '--fqbn', $fqbn
        '--build-path', $buildPath
        $dir
    )
    if ($VerbosePreference -eq 'Continue') { $cliArgs += '--verbose' }

    Invoke-ArduinoCli -Arguments $cliArgs -DryRun:$DryRun
}
