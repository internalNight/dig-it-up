param(
    [string]$EngineRoot='D:\UE_5.8',
    [switch]$SkipFine,
    [string]$Prefix='Physical',
    [string]$Profile='RoadheaderTransport.json',
    [int]$MainDuration=60,
    [int]$SensitivityDuration=26,
    [float]$StopAt=18
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$profile=Get-Content -LiteralPath (Join-Path $root ('Config\'+$Profile)) -Raw | ConvertFrom-Json
$base=($profile.flags -join ' ')+' -SandCutTest -SandFeedEnd=50'
$cases=@(
    @{Name=$Prefix+'Run';Duration=$MainDuration;Extra=' -SandMachineCapture'},
    @{Name=$Prefix+'Repeat';Duration=$MainDuration;Extra=''},
    @{Name=$Prefix+'ChainOff';Duration=$MainDuration;Extra=' -SandChainStopped'},
    @{Name=$Prefix+'Stop18';Duration=$MainDuration;Extra=(' -SandStopAt='+$StopAt)},
    @{Name=$Prefix+'Clock30';Duration=$SensitivityDuration;Extra=' -SandCouplingHz=30'}
)
if(!$SkipFine) {$cases+=@{Name=$Prefix+'Fine';Duration=$SensitivityDuration;Extra=' -SandQuality=Fine'}}
foreach($case in $cases) {
    & (Join-Path $PSScriptRoot 'TestRoadheaderTransport.ps1') -Name $case.Name -Duration $case.Duration -Flags ($base+$case.Extra) -EngineRoot $EngineRoot
    if($case.Name -eq ($Prefix+'Run')) {
        $frames=Join-Path $root 'Saved\MachineFrames'
        $dest=Join-Path $root ('Saved\TransportV3\'+$Prefix+'Run\frames')
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        $started=[datetime](Get-Content (Join-Path $root ('Saved\TransportV3\'+$Prefix+'Run\run.json')) -Raw | ConvertFrom-Json).startedUtc
        Get-ChildItem -LiteralPath $frames -Filter '*.png' | Where-Object {$_.LastWriteTimeUtc -ge $started.ToUniversalTime()} | Copy-Item -Destination $dest
    }
}
