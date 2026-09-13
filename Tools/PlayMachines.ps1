param(
    [ValidateSet('','Balanced','Legacy','Fine','Ultra')][string]$Quality='',
    [switch]$Bench,
    [switch]$SelectVehicle,
    [ValidateSet('Paddle','Chevron','BucketWheel','Spoke','Helix')][string]$Head='Paddle',
    [ValidateSet('Legacy','DryCorotated')][string]$Material='Legacy',
    [switch]$Traction,
    [switch]$FeedControl,
    [float]$HeadPitch=0,
    [Nullable[float]]$HeadHeightCm=$null,
    [switch]$InspectHead,
    [Nullable[float]]$Phi=$null,
    [Nullable[float]]$CohesionPa=$null,
    [Nullable[float]]$Dilation=$null,
    [string]$EngineRoot='D:\UE_5.8'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$projectFile=Join-Path $projectRoot 'SandExcavator.uproject'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if(!(Test-Path -LiteralPath $editor)) { throw "UnrealEditor.exe not found in $EngineRoot. Pass -EngineRoot with your installation path." }
$arguments=@(('"'+$projectFile+'"'),'-game','-d3d12','-sm6','-windowed','-ResX=1600','-ResY=900')
if(!$SelectVehicle) { $arguments+='-SandRoadheader' }
if($Quality) { $arguments+="-SandQuality=$Quality" }
if($Material -eq 'DryCorotated') { $arguments+='-SandMaterial=DryCorotated' }
if($Traction -or $FeedControl) { $arguments+='-SandTraction' }
if($FeedControl) { $arguments+='-SandFeedControl' }
if($Bench) { $arguments+= '-SandRoadheaderBench' }
$arguments+= "-SandHead=$Head"
$arguments+= "-SandHeadPitch=$HeadPitch"
if($null -ne $HeadHeightCm) { $arguments+= "-SandHeadHeightCm=$HeadHeightCm" }
if($InspectHead) { $arguments+= '-SandHeadInspect' }
if($null -ne $Phi) { $arguments+= "-SandPhi=$Phi" }
if($null -ne $CohesionPa) { $arguments+= "-SandCohesionPa=$CohesionPa" }
if($null -ne $Dilation) { $arguments+= "-SandDilation=$Dilation" }
# This is an interactive game window, intentionally visible to the player.
Start-Process -FilePath $editor -ArgumentList $arguments
