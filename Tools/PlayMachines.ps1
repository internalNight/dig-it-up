param(
    [ValidateSet('Balanced','Legacy','Fine','Ultra')][string]$Quality='Balanced',
    [switch]$Bench,
    [string]$EngineRoot='D:\UE_5.8'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$projectFile=Join-Path $projectRoot 'SandExcavator.uproject'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if(!(Test-Path -LiteralPath $editor)) { throw "UnrealEditor.exe not found in $EngineRoot. Pass -EngineRoot with your installation path." }
$arguments=@(('"'+$projectFile+'"'),'-game','-d3d12','-sm6','-windowed','-ResX=1600','-ResY=900',"-SandQuality=$Quality")
if($Bench) { $arguments+= '-SandRoadheaderBench' }
# This is an interactive game window, intentionally visible to the player.
Start-Process -FilePath $editor -ArgumentList $arguments
