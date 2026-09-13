param([ValidateSet('Soil','Sensitivity','Heads','Drive','Holding')][string]$Suite='Soil',[string[]]$Only=@(),[string]$EngineRoot='D:\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('Saved\ExcavationLab\'+$Suite)
New-Item -ItemType Directory -Path $out -Force | Out-Null
$cases=@()
$isSoil=$Suite -in @('Soil','Sensitivity')
if($Suite -eq 'Soil') {
    $cases=@(
        @{Name='Idle25';Flags='-SandSoilBench=Idle -SandQuality=Ultra'},
        @{Name='Idle50';Flags='-SandSoilBench=Idle -SandQuality=Balanced'},
        @{Name='Blade30';Flags='-SandSoilBench=Blade -SandBladeAngle=30'},
        @{Name='Blade60';Flags='-SandSoilBench=Blade -SandBladeAngle=60'},
        @{Name='Blade90';Flags='-SandSoilBench=Blade -SandBladeAngle=90'},
        @{Name='Blade60Fine';Flags='-SandSoilBench=Blade -SandBladeAngle=60 -SandQuality=Fine'},
        @{Name='Blade60Dilation';Flags='-SandSoilBench=Blade -SandBladeAngle=60 -SandDilation=5'}
    )
}
if($Suite -eq 'Sensitivity') {
    $cases=@(
        @{Name='Phi30';Flags='-SandSoilBench=Blade -SandPhi=30'},
        @{Name='Phi40';Flags='-SandSoilBench=Blade -SandPhi=40'},
        @{Name='Depth05';Flags='-SandSoilBench=Blade -SandBladeDepth=.05'},
        @{Name='Depth15';Flags='-SandSoilBench=Blade -SandBladeDepth=.15'},
        @{Name='TimeHalf';Flags='-SandSoilBench=Blade -SandSubsteps=2'},
        @{Name='Collapse';Flags='-SandSoilBench=Collapse'}
    )
}
if($Suite -eq 'Heads') {
    foreach($h in @('Paddle','Chevron','BucketWheel','Spoke','Helix')) {
        $cases+=@{Name=$h;Flags="-SandHead=$h"}
    }
    $cases+=@{Name='HelixStopped';Flags='-SandHead=Helix -SandRoadheaderStopped'}
    $cases+=@{Name='SpokeStopped';Flags='-SandHead=Spoke -SandRoadheaderStopped'}
}
if($Suite -eq 'Drive') {
    $cases=@(
        @{Name='Legacy';Flags='-SandHead=Paddle -SandRoadheader -SandCutTest'},
        @{Name='Limited';Flags='-SandHead=Paddle -SandRoadheader -SandCutTest -SandTraction'},
        @{Name='Controlled';Flags='-SandHead=Paddle -SandRoadheader -SandCutTest -SandTraction -SandFeedControl'},
        @{Name='ControlledDry';Flags='-SandHead=Paddle -SandRoadheader -SandCutTest -SandTraction -SandFeedControl -SandMaterial=DryCorotated'}
    )
}
if($Suite -eq 'Holding') {
    $cases=@(
        @{Name='HoldLegacy';Flags='-SandHead=Paddle -SandRoadheader -SandHoldTest -SandHeadPitch=-8'},
        @{Name='HoldLimited';Flags='-SandHead=Paddle -SandRoadheader -SandHoldTest -SandHeadPitch=-8 -SandTraction'},
        @{Name='HoldStopped';Flags='-SandHead=Paddle -SandRoadheader -SandHoldTest -SandHeadPitch=-8 -SandTraction -SandRoadheaderStopped'}
    )
}
$results=@()
$summaryPath=Join-Path $out 'results.json'
if($Only.Count -gt 0 -and (Test-Path -LiteralPath $summaryPath)) {
    $results=@(Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json | Where-Object { $_.case -notin $Only })
}
foreach($case in $cases) {
    if($Only.Count -gt 0 -and $case.Name -notin $Only) {continue}
    $started=Get-Date
    $logName='Lab_'+$Suite+'_'+$case.Name+'.log'
    $flags='-SandRoadheaderBench -SandMaterial=DryCorotated'
    if($Suite -in @('Drive','Holding')) {$flags=''}
    $args='"'+(Join-Path $root 'SandExcavator.uproject')+'" -game -d3d12 -sm6 -windowed -ResX=1280 -ResY=720 -unattended -nosound -SandRoadheaderTest -log='+$logName+' '+$flags+' '+$case.Flags
    $p=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $args -PassThru -WindowStyle Hidden
    $done=$false
    for($attempt=0;$attempt -lt 6 -and !$done;$attempt++) {$done=$p.WaitForExit(60000)}
    if(!$done) {Stop-Process -Id $p.Id;throw "Timeout $($case.Name)"}
    if($p.ExitCode -ne 0) {throw "Exit $($p.ExitCode): $($case.Name)"}
    $logPath=Join-Path $root ('Saved\Logs\'+$logName)
    $log=Get-Content -LiteralPath $logPath -Raw
    if($log -match 'Fatal error:|Assertion failed:') {throw 'Fatal error'}
    Copy-Item -LiteralPath $logPath -Destination $out
    $prefix=if($isSoil) {'SOIL_BENCH'} else {'ROADHEADER'}
    $diagnosticMatches=[regex]::Matches($log,$prefix+' [^\r\n]+')
    if(!$diagnosticMatches.Count) {throw 'No diagnostics'}
    $rows=@()
    foreach($m in $diagnosticMatches) {
        $row=[ordered]@{}
        foreach($pair in [regex]::Matches($m.Value,'(\w+)=([^ ]+)')) {$row[$pair.Groups[1].Value]=$pair.Groups[2].Value}
        if([math]::Abs([double]$row['massError']) -gt .001 -or [int]$row['nonfinite'] -ne 0) {throw "Conservation/finite failure $($case.Name)"}
        $rows+=[pscustomobject]$row
    }
    $rows | Export-Csv -LiteralPath (Join-Path $out ($case.Name+'.csv')) -NoTypeInformation -Encoding utf8
    foreach($diagnostic in @('HEAD_DIAGNOSTIC','FEED_CONTROL','CHASSIS_COUPLING')) {
        $extra=@()
        foreach($m in [regex]::Matches($log,$diagnostic+' [^\r\n]+')) {
            $r=[ordered]@{}
            foreach($pair in [regex]::Matches($m.Value,'(\w+)=([^ ]+)')) {$r[$pair.Groups[1].Value]=$pair.Groups[2].Value}
            $extra+=[pscustomobject]$r
        }
        if($extra.Count) { $extra | Export-Csv -LiteralPath (Join-Path $out ($case.Name+'_'+$diagnostic+'.csv')) -NoTypeInformation -Encoding utf8 }
    }
    $last=$rows[-1]
    $minimum=if($isSoil) {8} else {16}
    if([double]$last.t -lt $minimum) {throw 'Incomplete simulation'}
    $imageName=if($isSoil) {'SoilBench.png'} elseif($case.Flags -match '-SandRoadheaderStopped') {'RoadheaderStopped.png'} else {'RoadheaderBench.png'}
    $image=Join-Path $root ('Saved\'+$imageName)
    if(!(Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) {throw 'No fresh image'}
    Copy-Item -LiteralPath $image -Destination (Join-Path $out ($case.Name+'.png'))
    $results+=@{case=$case.Name;flags=($flags+' '+$case.Flags);last=$last;massFinitePassed=$true;physicalValidation='not calibrated';runUtc=$started.ToUniversalTime().ToString('o')}
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding utf8
    Write-Output ($case.Name+': '+$diagnosticMatches[-1].Value)
}
