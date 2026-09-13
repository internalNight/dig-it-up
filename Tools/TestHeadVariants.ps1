param([string]$EngineRoot='D:\UE_5.8',[string[]]$Only=@(),[switch]$Inspect)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$out=Join-Path $projectRoot 'Saved\HeadVariants'
if($Inspect) { $out=Join-Path $projectRoot 'Saved\HeadInspection' }
New-Item -ItemType Directory -Path $out -Force | Out-Null
$results=@()
$cases=@(
    @{Name='Paddle';Flags='-SandHead=Paddle -SandRoadheaderBench'},
    @{Name='Chevron';Flags='-SandHead=Chevron -SandRoadheaderBench'},
    @{Name='Spoke';Flags='-SandHead=Spoke -SandRoadheaderBench'},
    @{Name='Helix';Flags='-SandHead=Helix -SandRoadheaderBench'},
    @{Name='SpokeStopped';Flags='-SandHead=Spoke -SandRoadheaderBench -SandRoadheaderStopped'},
    @{Name='DryPaddle';Flags='-SandHead=Paddle -SandRoadheaderBench -SandPhi=30 -SandCohesionPa=0'},
    @{Name='FullCut';Flags='-SandHead=Paddle -SandRoadheader -SandCutTest'}
)
foreach($case in $cases) {
    if($Only.Count -gt 0 -and $case.Name -notin $Only) { continue }
    $started=Get-Date
    $logName='HeadVariants_'+$case.Name+'.log'
    $arguments='"'+(Join-Path $projectRoot 'SandExcavator.uproject')+'" -game -d3d12 -sm6 -windowed -ResX=1280 -ResY=720 -unattended -nosound -SandRoadheaderTest -log='+$logName+' '+$case.Flags
    if($Inspect) { $arguments+=' -SandHeadInspect' }
    $proc=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if(!$proc.WaitForExit(180000)) { Stop-Process -Id $proc.Id; throw "Timed out: $($case.Name)" }
    if($proc.ExitCode -ne 0) { throw "Exit code $($proc.ExitCode): $($case.Name)" }
    $logPath=Join-Path $projectRoot ('Saved\Logs\'+$logName)
    $log=Get-Content -LiteralPath $logPath -Raw
    if($log -match 'Fatal error:|Assertion failed:') { throw 'Runtime failure' }
    $rows=[regex]::Matches($log,'ROADHEADER t=([\d.]+).*?massError=([-\d.]+).*?delivered=([\d.]+) nonfinite=(\d+) carried=(\d+)')
    if(!$rows.Count) { throw 'No physical results' }
    foreach($row in $rows) {
        if([math]::Abs([double]$row.Groups[2].Value) -gt .001 -or [int]$row.Groups[4].Value -ne 0 -or [int]$row.Groups[5].Value -ne 0) { throw 'Mass/finite/contact-only check failed' }
    }
    $last=$rows[$rows.Count-1]
    if([double]$last.Groups[1].Value -lt 16) { throw 'Incomplete physical run' }
    $imageName=if($case.Name -eq 'SpokeStopped') {'RoadheaderStopped.png'} else {'RoadheaderBench.png'}
    $image=Join-Path $projectRoot ('Saved\'+$imageName)
    if(!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw 'Missing fresh image' }
    Copy-Item -LiteralPath $image -Destination (Join-Path $out ($case.Name+'.png'))
    Copy-Item -LiteralPath $logPath -Destination $out
    $results+=@{case=$case.Name;seconds=[double]$last.Groups[1].Value;deliveredKg=[double]$last.Groups[3].Value;massConserved=$true;finite=$true;carried=0;scope='contact prototype smoke test; not calibrated efficiency ranking'}
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding utf8
    Write-Output ($case.Name+': '+$last.Value)
}
