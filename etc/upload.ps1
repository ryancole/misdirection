<#
.SYNOPSIS
Compile a sketch from src/ and flash it to the connected Teensy 4.1.

.DESCRIPTION
Same USB Type resolution as build.ps1, then hands off to the Teensy
Loader. Port discovery is left to arduino-cli via -l teensy, which
matches the protocol the teensy-discovery tool reports; pass -Port if
you need to name it explicitly.

Remember which end is which: the Teensy's micro-USB goes to the TARGET
PC, so flashing changes what the target sees. Keep mind_control handy --
reflashing it tells you in seconds whether a fault is HID or the link.

.EXAMPLE
etc\upload.ps1 uart_to_hid_naive

.EXAMPLE
etc\upload.ps1 -ListPorts
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Sketch,

    [ValidateSet('hid', 'serial', 'serialhid', 'keyboard', 'touch', 'hidtouch')]
    [string]$UsbType,

    [string]$Port,
    [switch]$Clean,
    [switch]$ListPorts,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if ($ListPorts) {
    Invoke-ArduinoCli -Arguments @('board', 'list')
    return
}

if (-not $Sketch) {
    Write-Host "Usage: etc\upload.ps1 <sketch> [-Port <addr>] [-UsbType ...] [-DryRun]"
    Write-Host "       etc\upload.ps1 -ListPorts"
    Write-Host ""
    Write-Host "Sketches in src/:"
    foreach ($n in Get-SketchNames) {
        "  {0,-20} usb={1}" -f $n, (Resolve-UsbType -Sketch $n) | Write-Host
    }
    return
}

$dir       = Resolve-Sketch $Sketch
$usb       = Resolve-UsbType -Sketch $Sketch -Override $UsbType
$fqbn      = Get-Fqbn $usb
$buildPath = Join-Path $RepoRoot ".build\$Sketch"

if ($Clean -and (Test-Path -LiteralPath $buildPath)) {
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}

Write-Host "==> $Sketch (usb=$usb) -> Teensy" -ForegroundColor Cyan

$cliArgs = @(
    'compile'
    '--upload'
    '--fqbn', $fqbn
    '--build-path', $buildPath
)
if ($Port) { $cliArgs += @('--port', $Port) } else { $cliArgs += @('--protocol', 'teensy') }
$cliArgs += $dir
if ($VerbosePreference -eq 'Continue') { $cliArgs += '--verbose' }

Invoke-ArduinoCli -Arguments $cliArgs -DryRun:$DryRun
