param([string]$EngineRoot='D:\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$out=Join-Path $projectRoot 'Saved\ExcavationLab\HeadCutaways'
New-Item -ItemType Directory -Path $out -Force | Out-Null
foreach($head in @('Paddle','Chevron','BucketWheel','Spoke','Helix')) {
    $started=Get-Date
    $arguments='"'+(Join-Path $projectRoot 'SandExcavator.uproject')+'" -game -d3d12 -sm6 -windowed -ResX=1280 -ResY=720 -unattended -nosound -SandRoadheaderBench -SandHeadInspect -SandHead='+$head+' -SandCaptureSurfacePreview -SandCaptureDelaySeconds=2 -log=HeadCutaway_'+$head+'.log'
    $proc=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if(!$proc.WaitForExit(60000)) { Stop-Process -Id $proc.Id; throw 'Capture timed out' }
    if($proc.ExitCode -ne 0) { throw 'Capture process failed' }
    $image=Join-Path $projectRoot 'Artifacts\SandSurfaceUEPreview.png'
    if(!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw 'Missing fresh image' }
    Copy-Item -LiteralPath $image -Destination (Join-Path $out ($head+'.png'))
    Write-Output ('Captured '+$head)
}
