param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [string]$GameExecutable,
    [string]$CaptureRoot,
    [string]$Label = 'Desktop',
    [ValidateRange(15,30)][int]$DurationSeconds = 25,
    [switch]$Smooth,
    [switch]$Dig,
    [switch]$WindowShift,
    [switch]$TerrainAcceptance,
    [switch]$DriveTrace,
    [switch]$AsyncSurface
)
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Label must use letters, digits, underscores or hyphens.' }
if ($Dig -and $WindowShift) { throw 'Run digging and window-shift fixtures separately.' }
$fixtureCount = @(@($Dig,$WindowShift,$TerrainAcceptance,$DriveTrace) | Where-Object { [bool]$_ }).Count
if ($fixtureCount -gt 1) { throw 'Select only one interaction fixture per run.' }
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'Saved\DesktopPerformance'
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach ($extension in @('json','png')) {
    $previousResult = Join-Path $output "$Label.$extension"
    if (Test-Path -LiteralPath $previousResult) { Remove-Item -LiteralPath $previousResult -Force }
}
$log = Join-Path $output "$Label.log"
$executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$launchArguments = '"'+(Join-Path $root 'SandExcavator.uproject')+'" -game'
$mode = 'Editor development game'
if ($GameExecutable) {
    $executable = (Resolve-Path -LiteralPath $GameExecutable).Path
    $launchArguments = ''
    $mode = 'Packaged development game'
    if (!$CaptureRoot) { throw 'Supply CaptureRoot (the packaged SandExcavator directory).' }
}
if (!$CaptureRoot) { $CaptureRoot = $root }
$launchArguments += ' -windowed -ResX=1920 -ResY=1080 -d3d12 -sm6 -graphicsadapter=0 -SandExcavator -SandPerfHud -SandCaptureSurfacePreview -SandCaptureDelaySeconds='+$DurationSeconds+' -unattended -nosound -abslog="'+$log+'"'
if ($Smooth) { $launchArguments += ' -SandQuality=DesktopSmooth -SandInternalHz=240' }
else { $launchArguments += ' -SandInternalHz=180' }
if ($Dig) { $launchArguments += ' -SandAutopilot -SandInteractionDiagnostics' }
if ($WindowShift) { $launchArguments += ' -SandWindowTest' }
if ($TerrainAcceptance) { $launchArguments += ' -SandTerrainAcceptance' }
if ($DriveTrace) { $launchArguments += ' -SandDriveTraceTest' }
if ($AsyncSurface) { $launchArguments += ' -SandAsyncSurfaceReadback' }
$launchArguments += ' -SandFrameTiming'
$started = Get-Date
$p = Start-Process -FilePath $executable -ArgumentList $launchArguments -PassThru -WindowStyle Hidden
$finished = $p.WaitForExit(180000)
if (!$finished) { Stop-Process -Id $p.Id; throw 'Desktop run timed out.' }
if ($p.ExitCode -ne 0) { throw "Desktop process exited with $($p.ExitCode). See $log" }
if (!(Test-Path -LiteralPath $log) -or (Get-Item -LiteralPath $log).LastWriteTime -lt $started) { throw 'No fresh log.' }
$lines = Get-Content -LiteralPath $log
if ($lines -match 'Fatal error:|Assertion failed:|Ensure condition failed:') { throw "Runtime error in $log" }
if ($Dig) {
    $carrying = @($lines | Select-String 'carried [1-9][0-9]*$')
    $dumped = @($lines | Select-String 'retention 0,.*carried 0$')
    if (!$carrying.Count -or !$dumped.Count -or $dumped[-1].LineNumber -le $carrying[0].LineNumber) {
        throw 'Scripted run did not log carrying followed by release.'
    }
}
if ($WindowShift) {
    $shifts = @($lines | Select-String 'LUNAR_WINDOW_SHIFT index=')
    if (!($lines -match 'LUNAR_WINDOW_TEST teleport=4 ') -or $shifts.Count -lt 4) {
        throw 'Window fixture did not complete four moves and corresponding streaming shifts.'
    }
}
if ($TerrainAcceptance) {
    if (!($lines -match 'LUNAR_WINDOW_TEST teleport=5 ') -or !($lines -match 'LUNAR_FIELD_BOUNDARY ')) {
        throw 'Large-field/pit/boundary fixture did not complete.'
    }
    if ($lines -match 'chassis \(X,Z\)=\([^,]+,-[0-9]') { throw 'Vehicle fell below the physical floor.' }
}
if ($DriveTrace -and !($lines -match 'LUNAR_LIVE_TRACK vertices=[1-9]')) { throw 'No live track depression was rendered.' }
$samples = @($lines | ForEach-Object {
    if ($_ -match 'MOBILE_PERF renderFps=([\d.]+) simHz=([\d.]+) surfaceHz=([\d.]+) solverReadbackMs=([\d.]+) surfaceBuildMs=([\d.]+)') {
        [PSCustomObject]@{GameFps=[double]$Matches[1];SandHz=[double]$Matches[2];SurfaceHz=[double]$Matches[3];SolverReadbackMs=[double]$Matches[4];SurfaceBuildMs=[double]$Matches[5]}
    }
})
if ($samples.Count -lt 8) { throw 'Too few performance samples.' }
# Drop six seconds of warm-up and the final screenshot window. These are
# two-second HUD averages, not per-frame percentiles or a GPU-only timer.
$warm = @($samples | Select-Object -Skip 3 | Select-Object -SkipLast 1)
$means = [ordered]@{}
foreach ($metric in @('GameFps','SandHz','SurfaceHz','SolverReadbackMs','SurfaceBuildMs')) {
    $means[$metric] = [math]::Round(($warm | Measure-Object -Property $metric -Average).Average,2)
}
$supports = @($lines | Select-String 'supports ([0-4])/4' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$capture = Join-Path $CaptureRoot 'Artifacts\SandSurfaceUEPreview.png'
$frameTimes = @($lines | ForEach-Object {
    if ($_ -match 'DESKTOP_FRAME elapsed=([\d.]+) deltaMs=([\d.]+)' -and
        [double]$Matches[1] -ge 6 -and [double]$Matches[1] -le ($DurationSeconds - 1)) { [double]$Matches[2] }
} | Sort-Object)
$framePercentiles = if ($frameTimes.Count) {
    [ordered]@{Count=$frameTimes.Count;P50ms=$frameTimes[[int][math]::Floor(0.50*($frameTimes.Count-1))];P95ms=$frameTimes[[int][math]::Floor(0.95*($frameTimes.Count-1))];P99ms=$frameTimes[[int][math]::Floor(0.99*($frameTimes.Count-1))]}
} else { $null }
if (!(Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).LastWriteTime -lt $started) { throw 'No fresh screenshot.' }
Copy-Item -LiteralPath $capture -Destination (Join-Path $output "$Label.png")
$result = [ordered]@{Label=$Label;Date=$started.ToString('o');Resolution='1920x1080';Mode=$mode;Smooth=[bool]$Smooth;Dig=[bool]$Dig;WindowShift=[bool]$WindowShift;TerrainAcceptance=[bool]$TerrainAcceptance;DriveTrace=[bool]$DriveTrace;FrameTimes=$framePercentiles;WarmSampleCount=$warm.Count;Mean=$means;MinimumLoggedSupports=($supports | Measure-Object -Minimum).Minimum;Samples=$samples;Log=$log;Screenshot=(Join-Path $output "$Label.png")}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output "$Label.json") -Encoding UTF8
$result | ConvertTo-Json -Depth 6
