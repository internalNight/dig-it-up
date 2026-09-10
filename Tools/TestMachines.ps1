param([string]$EngineRoot='D:\UE_5.8',[switch]$TransportOnly)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$resultRoot=Join-Path $projectRoot 'Saved\MachineAcceptance'
if($TransportOnly) { $resultRoot=Join-Path $projectRoot 'Saved\TransportFixAcceptance' }
New-Item -ItemType Directory -Path $resultRoot -Force | Out-Null
$cases=@(
    @{Name='Powered'; Flags='-SandRoadheaderBench -SandRoadheaderTest -SandRoadheaderInside -SandMachineCapture'; Image='Saved\RoadheaderInside.png'},
    @{Name='Stopped'; Flags='-SandRoadheaderBench -SandRoadheaderTest -SandRoadheaderStopped'; Image='Saved\RoadheaderStopped.png'},
    @{Name='Fine'; Flags='-SandRoadheader -SandQuality=Fine -SandCaptureSurfacePreview -SandCaptureDelaySeconds=7'; Image='Artifacts\SandSurfaceUEPreview.png'},
    @{Name='Excavator'; Flags='-SandExcavator -SandAutopilot -SandQuality=Legacy -SandCaptureSurfacePreview -SandCaptureDelaySeconds=8'; Image='Artifacts\SandSurfaceUEPreview.png'},
    @{Name='Selection'; Flags='-SandCaptureSurfacePreview -SandCaptureDelaySeconds=2'; Image='Artifacts\SandSurfaceUEPreview.png'}
)
if($TransportOnly) {
    $cases=@(
        @{Name='Powered'; Flags='-SandRoadheaderBench -SandRoadheaderTest -SandRoadheaderInside -SandMachineCapture'; Image='Saved\RoadheaderInside.png'},
        @{Name='ChainStopped'; Flags='-SandRoadheaderBench -SandRoadheaderTest -SandChainStopped'; Image='Saved\RoadheaderBench.png'},
        @{Name='Stopped'; Flags='-SandRoadheaderBench -SandRoadheaderTest -SandRoadheaderStopped'; Image='Saved\RoadheaderStopped.png'},
        @{Name='FullCut'; Flags='-SandRoadheader -SandRoadheaderTest -SandCutTest'; Image='Saved\RoadheaderBench.png'}
    )
}
$results=@()
foreach($case in $cases) {
    $started=Get-Date
    $logName='MachineAcceptance_'+$case.Name+'.log'
    $args='"'+(Join-Path $projectRoot 'SandExcavator.uproject')+'" -game -d3d12 -sm6 -windowed -ResX=1280 -ResY=720 -unattended -nosound -log='+$logName+' '+$case.Flags
    $proc=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $args -PassThru -WindowStyle Hidden
    $done=$proc.WaitForExit(60000)
    if(!$done) { $done=$proc.WaitForExit(60000) }
    if(!$done) { Stop-Process -Id $proc.Id; throw "Case $($case.Name) exceeded 120 seconds." }
    $logPath=Join-Path $projectRoot ('Saved\Logs\'+$logName)
    $log=Get-Content -LiteralPath $logPath -Raw
    Copy-Item -LiteralPath $logPath -Destination $resultRoot
    $image=Join-Path $projectRoot $case.Image
    if(!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh screenshot for $($case.Name)" }
    if($proc.ExitCode -ne 0) { throw "Nonzero process exit for $($case.Name): $($proc.ExitCode)" }
    Copy-Item -LiteralPath $image -Destination (Join-Path $resultRoot ($case.Name+'.png'))
    if($log -match 'Fatal error:|Assertion failed:') { throw "Fatal error in $($case.Name)" }
    if($case.Name -in @('Powered','Stopped','ChainStopped','FullCut')) {
        $rows=[regex]::Matches($log,'ROADHEADER t=([\d.]+).*?massError=([-\d.]+).*?delivered=([\d.]+) nonfinite=(\d+) carried=(\d+)')
        if($rows.Count -eq 0) { throw "No physical diagnostics for $($case.Name)" }
        foreach($row in $rows) {
            if([math]::Abs([double]$row.Groups[2].Value) -gt 0.001 -or [int]$row.Groups[4].Value -ne 0 -or [int]$row.Groups[5].Value -ne 0) { throw "Conservation/finite/carrier check failed in $($case.Name)" }
        }
        $last=$rows[$rows.Count-1]
        if([double]$last.Groups[1].Value -lt 16) { throw 'Physical run was incomplete.' }
        $results+=@{case=$case.Name;seconds=[double]$last.Groups[1].Value;deliveredKg=[double]$last.Groups[3].Value;massConserved=$true;finite=$true;carried=0}
    } else {
        if($case.Name -eq 'Fine' -and $log -match 'red floor exposed') { throw 'Idle fine sandbox incorrectly triggered victory.' }
        $results+=@{case=$case.Name;exitCode=$proc.ExitCode;image=(Test-Path -LiteralPath $image)}
    }
    Write-Output ('Completed '+$case.Name)
}
$powered=($results|Where-Object case -eq 'Powered').deliveredKg
$stopped=($results|Where-Object case -eq 'Stopped').deliveredKg
if($powered -le $stopped+1.0) { throw "Powered transport did not exceed stopped control by 1 kg: $powered / $stopped" }
if($TransportOnly) {
    $chainStopped=($results|Where-Object case -eq 'ChainStopped').deliveredKg
    if($powered -le $chainStopped+1.0) { throw "Conveyor-on did not exceed conveyor-off control: $powered / $chainStopped" }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $resultRoot 'results.json') -Encoding utf8
Write-Output ('Acceptance passed. Results: '+$resultRoot)
