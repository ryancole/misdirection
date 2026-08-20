# Shared helpers for build.ps1 and upload.ps1. Dot-source, don't run.

Set-StrictMode -Version Latest

$script:RepoRoot = Split-Path $PSScriptRoot -Parent
$script:SrcRoot  = Join-Path $RepoRoot 'src'

# The USB Type each sketch needs, by sketch name. This is the setting
# most likely to be wrong and least likely to announce itself when it
# is: a sketch built as usb=serial links no Keyboard/Mouse, and one
# built as usb=hid gives the target PC no CDC port. Keeping it here
# means it travels with the repo instead of living in IDE menu state.
$script:SketchUsbType = @{
    'mind_control'      = 'hid'      # the real firmware
    'hid_smoke_test'    = 'hid'      # step 2 diagnostic
    'uart_echo'         = 'serial'   # UART only, no HID linked in
    'uart_to_hid_naive' = 'hid'      # naive UART -> HID bridge
}
$script:DefaultUsbType = 'hid'

function Get-ArduinoCli {
    # Arduino IDE 2.x bundles a full arduino-cli. Using it (rather than
    # a separate install) keeps VS Code, these scripts, and the IDE on
    # one toolchain and one package tree, so there is no version skew.
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe')
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    $onPath = Get-Command 'arduino-cli' -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    throw "arduino-cli not found. Looked in the Arduino IDE 2.x install and on PATH."
}

function Get-Fqbn {
    param([Parameter(Mandatory)][string]$UsbType)
    "teensy:avr:teensy41:usb=$UsbType,speed=600,opt=o2std,keys=en-us"
}

function Get-SketchNames {
    Get-ChildItem -LiteralPath $SrcRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "$($_.Name).ino") } |
        Select-Object -ExpandProperty Name |
        Sort-Object
}

function Resolve-Sketch {
    param([Parameter(Mandatory)][string]$Sketch)

    $dir = Join-Path $SrcRoot $Sketch
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        throw "No sketch '$Sketch' in src/. Available: $((Get-SketchNames) -join ', ')"
    }
    # Arduino requires the .ino to be named after its folder. Catch a
    # half-finished rename here rather than in a confusing compiler error.
    $ino = Join-Path $dir "$Sketch.ino"
    if (-not (Test-Path -LiteralPath $ino -PathType Leaf)) {
        throw "src/$Sketch/ has no $Sketch.ino. Arduino requires the sketch file to match its folder name."
    }
    $dir
}

function Resolve-UsbType {
    param([Parameter(Mandatory)][string]$Sketch, [string]$Override)

    if ($Override) { return $Override }
    if ($SketchUsbType.ContainsKey($Sketch)) { return $SketchUsbType[$Sketch] }

    Write-Warning "No USB Type recorded for '$Sketch'; defaulting to '$DefaultUsbType'. Add it to `$SketchUsbType in etc/common.ps1."
    $DefaultUsbType
}

function Invoke-ArduinoCli {
    param(
        [Parameter(Mandatory)][string[]]$Arguments,
        [switch]$DryRun
    )

    $cli = Get-ArduinoCli
    if ($DryRun) {
        Write-Host "DRY RUN: `"$cli`" $($Arguments -join ' ')" -ForegroundColor Yellow
        return
    }

    & $cli @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli exited with $LASTEXITCODE"
    }
}
