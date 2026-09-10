param(
    [string]$Archive = (Join-Path $PSScriptRoot '../Releases/DigItUp_Windows_USB_20260909.zip'),
    [string]$ReportDirectory = (Join-Path $PSScriptRoot '../Artifacts/PortableValidation')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$report = [IO.Path]::GetFullPath($ReportDirectory)
New-Item -ItemType Directory -Path $report -Force | Out-Null
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('Dig It Up USB 中文 ' + [guid]::NewGuid().ToString('N'))
[IO.Compression.ZipFile]::ExtractToDirectory([IO.Path]::GetFullPath($Archive),$testRoot)
$gameRoot = Join-Path $testRoot 'DigItUp'
$exe = Join-Path $gameRoot 'SandExcavator/Binaries/Win64/SandExcavator.exe'
$dataRoot = Join-Path $testRoot 'UserData'
$manifest = Import-Csv -LiteralPath (Join-Path $gameRoot 'SHA256.csv') -Encoding UTF8
foreach ($entry in $manifest) {
    if ((Get-FileHash -LiteralPath (Join-Path $gameRoot $entry.Path)).Hash -ne $entry.SHA256) { throw "Extracted file hash mismatch: $($entry.Path)" }
}
$modules = @{}
$runs = @()
foreach ($scenario in @('Normal','VictoryExit')) {
    $log = Join-Path $report "$scenario.log"
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    # A foreign working directory and stripped PATH expose accidental project/SDK dependencies.
    $info.WorkingDirectory = $env:SystemRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($arg in @('-RenderOffscreen','-ForceRes','-windowed','-ResX=1280','-ResY=720',
        '-unattended','-nosound','-d3d12','-sm6',"-UserDir=$dataRoot","-abslog=$log",
        '-SandCaptureSurfacePreview','-SandCaptureDelaySeconds=15')) {
        $info.ArgumentList.Add($arg)
    }
    if ($scenario -eq 'Normal') { $info.ArgumentList.Add('-SandAutopilot') }
    else {
        $info.ArgumentList.Add('-SandVictoryTest')
        $info.ArgumentList.Add('-SandVictoryExitTest')
    }
    $process = [Diagnostics.Process]::Start($info)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    [long]$peakPrivate = 0
    while (!$process.WaitForExit(500)) {
        $process.Refresh()
        $peakPrivate = [Math]::Max($peakPrivate,[long]$process.PrivateMemorySize64)
        try {
            foreach ($module in $process.Modules) { $modules[$module.FileName] = $module.ModuleName }
        } catch { Write-Verbose "Module enumeration raced process shutdown: $_" }
        if ($timer.Elapsed.TotalSeconds -gt 90) {
            # Only terminate the exact child created by this test if it hangs.
            $process.Kill()
            throw "Portable test timeout: $scenario"
        }
    }
    if ($process.ExitCode -ne 0) { throw "Portable $scenario exited with $($process.ExitCode)" }
    $logText = Get-Content -LiteralPath $log -Raw
    if ($logText -notmatch 'Runtime soil:' -or $logText -notmatch 'Sand surface frame') { throw "Simulation did not start: $scenario" }
    if ($scenario -eq 'Normal' -and $logText -match 'DIG IT UP: VICTORY') { throw 'Premature victory in normal sandbox.' }
    if ($scenario -eq 'VictoryExit' -and $logText -notmatch 'DIG IT UP: VICTORY') { throw 'Victory did not trigger.' }
    $runs += [PSCustomObject]@{Scenario=$scenario;ExitCode=$process.ExitCode;PeakPrivateMiB=[math]::Round($peakPrivate/1MB);Seconds=[math]::Round($timer.Elapsed.TotalSeconds,2)}
}
$moduleList = foreach ($path in $modules.Keys | Sort-Object) {
    [PSCustomObject]@{Name=$modules[$path];Path=$path}
}
$moduleList | Export-Csv -LiteralPath (Join-Path $report 'LoadedModules.csv') -Encoding UTF8 -NoTypeInformation
$developmentDependencies = @($moduleList | Where-Object { $_.Path -match '\\UE_5\.8\\|\\visual studio 2026\\|\\UEProjects\\' })
if ($developmentDependencies.Count) { throw 'Loaded libraries from a development installation.' }
foreach ($name in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    $expectedPath = Join-Path (Split-Path -Parent $exe) $name
    if (!$modules.ContainsKey($expectedPath)) { throw "Runtime was not observed loading from the portable package: $name" }
}
if (!(Test-Path -LiteralPath (Join-Path $dataRoot 'Saved/Config'))) { throw 'User settings were not redirected to the separate user directory.' }
foreach ($entry in $manifest) {
    if ((Get-FileHash -LiteralPath (Join-Path $gameRoot $entry.Path)).Hash -ne $entry.SHA256) { throw "Packaged file modified by play: $($entry.Path)" }
}
Get-ChildItem -LiteralPath (Join-Path $gameRoot 'SandExcavator/Artifacts') -Filter '*.png' | Copy-Item -Destination $report -Force
$result = [PSCustomObject]@{
    Status='PASS';ExtractedRoot=$testRoot;HashVerifiedFiles=$manifest.Count;WorkingDirectory=$env:SystemRoot;
    RestrictedPath=$true;AdapterIndexForced=$false;LocalCppRuntimeVerified=$true;
    DevelopmentDllDependencies=$developmentDependencies.Count;UserDataDirectory=$dataRoot;Runs=$runs;
    Limitation='Same host, relocated ZIP extraction; not a clean Windows install or a second physical PC. No USB speed or other GPU validation.'
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $report 'Result.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 5
