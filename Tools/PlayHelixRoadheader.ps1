param(
    [ValidateSet('Balanced','Fine')][string]$Quality='Balanced',
    [string]$EngineRoot='D:\UE_5.8',
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$project=Join-Path $root 'SandExcavator.uproject'
if(!(Test-Path -LiteralPath $editor)) {throw "UnrealEditor.exe not found: $editor"}
if(!(Test-Path -LiteralPath (Join-Path $root 'Plugins\SandSimulation\Binaries\Win64\UnrealEditor-SandSimulation.dll'))) {throw 'Build SandExcavatorEditor first.'}
$profile=Get-Content -LiteralPath (Join-Path $root 'Config\HelixRoadheader.json') -Raw | ConvertFrom-Json
$launchArgs=@(('"'+$project+'"'),'-game','-d3d12','-sm6','-windowed','-ResX=1600','-ResY=900')+@($profile.flags)
if($Quality -eq 'Fine') {$launchArgs+='-SandQuality=Fine'}
if($ValidateOnly) {Write-Output ($editor+' '+($launchArgs -join ' '));return}
Start-Process -FilePath $editor -ArgumentList $launchArgs
