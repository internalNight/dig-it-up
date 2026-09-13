param([string]$Name='Probe',[string]$Flags='',[float]$Duration=24,[string]$EngineRoot='D:\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('Saved\TransportV3\'+$Name)
New-Item -ItemType Directory -Path $out -Force | Out-Null
$logName='TransportV3_'+$Name+'.log'
$args='"'+(Join-Path $root 'SandExcavator.uproject')+'" -game -d3d12 -sm6 -windowed -ResX=1280 -ResY=720 -unattended -nosound -SandRoadheader -SandRoadheaderTest -SandMaterial=DryCorotated -SandTestDuration='+$Duration+' -log='+$logName+' '+$Flags
$started=Get-Date
@{name=$Name;arguments=$args;startedUtc=$started.ToUniversalTime().ToString('o');sourceHashes=@(Get-ChildItem -LiteralPath (Join-Path $root 'Plugins\SandSimulation\Source'),(Join-Path $root 'Plugins\SandSimulation\Shaders') -Recurse -File | Get-FileHash | Select-Object Path,Hash);binaryHash=(Get-FileHash -LiteralPath (Join-Path $root 'Plugins\SandSimulation\Binaries\Win64\UnrealEditor-SandSimulation.dll')).Hash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'run.json') -Encoding utf8
$p=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $args -PassThru -WindowStyle Hidden
$done=$false
for($attempt=0;$attempt -lt 24 -and !$done;$attempt++) {$done=$p.WaitForExit(50000)}
if(!$done) {Stop-Process -Id $p.Id;throw "Timeout $Name"}
$logPath=Join-Path $root ('Saved\Logs\'+$logName)
Copy-Item -LiteralPath $logPath -Destination $out
if($p.ExitCode -ne 0) {throw "Exit $($p.ExitCode): $Name"}
$log=Get-Content -LiteralPath $logPath -Raw
if($log -match 'Fatal error:|Assertion failed:') {throw 'Fatal error'}
foreach($prefix in @('ROADHEADER','MATERIAL_PATH','HEAD_DIAGNOSTIC','FEED_CONTROL','CHASSIS_COUPLING','TOOL_CONTROL')) {
    $rows=@()
    foreach($m in [regex]::Matches($log,$prefix+' [^\r\n]+')) {
        $r=[ordered]@{}
        foreach($pair in [regex]::Matches($m.Value,'(\w+)=([^ ]+)')) {$r[$pair.Groups[1].Value]=$pair.Groups[2].Value}
        $rows+=[pscustomobject]$r
    }
    if($rows.Count) {
        $rows | Export-Csv -LiteralPath (Join-Path $out ($prefix+'.csv')) -NoTypeInformation -Encoding utf8
        if($prefix -eq 'ROADHEADER') {
            foreach($r in $rows) {if([math]::Abs([double]$r.massError) -gt .001 -or [int]$r.nonfinite -ne 0 -or [int]$r.carried -ne 0) {throw 'Mass/finite/carry failure'}}
            if([double]$rows[-1].t -lt $Duration) {throw 'Incomplete run'}
        }
        if($prefix -in @('MATERIAL_PATH','HEAD_DIAGNOSTIC','ROADHEADER')) {Write-Output ($Name+' '+$prefix+' '+($rows[-1] | ConvertTo-Json -Compress))}
    } elseif($prefix -eq 'ROADHEADER') {throw 'Missing diagnostics'}
}
$imageName=if($Flags -match '-SandRoadheaderStopped') {'RoadheaderStopped.png'} else {'RoadheaderBench.png'}
$image=Join-Path $root ('Saved\'+$imageName)
if(!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) {throw 'Missing fresh screenshot'}
Copy-Item -LiteralPath $image -Destination (Join-Path $out 'final.png')

Copy-Item -LiteralPath (Join-Path $root 'Saved\TransportParticles.csv') -Destination $out

if($Flags -match "-SandGeometryAudit") {Copy-Item -LiteralPath (Join-Path $root "Saved\TransportGeometry.csv") -Destination $out}
