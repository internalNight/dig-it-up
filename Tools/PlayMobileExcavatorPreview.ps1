param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [switch]$Capture,
    [switch]$Performance
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "UnrealEditor.exe not found: $editor" }
$arguments = @(
    '"' + (Join-Path $projectRoot 'SandExcavator.uproject') + '"',
    '-game', '-d3d12', '-sm6', '-windowed', '-ResX=1600', '-ResY=900',
    '-SandTouchPreview', '-SandQuality=Mobile', '-faketouches',
    '-ini:Input:[/Script/Engine.InputSettings]:bUseMouseForTouch=True'
)
if ($Performance) { $arguments += '-SandPerfHud' }
if ($Capture) {
    $started = Get-Date
    $arguments += @('-SandCaptureSurfacePreview', '-SandCaptureDelaySeconds=4', '-nosound')
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (!$process.WaitForExit(120000)) {
        Stop-Process -Id $process.Id
        throw 'Mobile preview capture timed out.'
    }
    if ($process.ExitCode -ne 0) { throw "Mobile preview exited with $($process.ExitCode)." }
    $image = Join-Path $projectRoot 'Artifacts\SandSurfaceUEPreview.png'
    if (!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) {
        throw "Fresh screenshot missing: $image"
    }
    Write-Output $image
} else {
    Start-Process -FilePath $editor -ArgumentList $arguments
}
