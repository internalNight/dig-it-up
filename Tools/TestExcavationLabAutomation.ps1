param([string]$EngineRoot='D:\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$report=Join-Path $root 'Saved\ExcavationLab\Automation'
$args='"'+(Join-Path $root 'SandExcavator.uproject')+'" -unattended -nop4 -nosound -d3d12 -sm6 -ExecCmds="Automation RunTests SandSimulation" -TestExit="Automation Test Queue Empty" -ReportExportPath="'+$report+'" -log=ExcavationLabAutomation.log'
$started=Get-Date
$p=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $args -PassThru -WindowStyle Hidden
$done=$false
for($i=0;$i -lt 4 -and !$done;$i++) {$done=$p.WaitForExit(60000)}
if(!$done) {Stop-Process -Id $p.Id;throw 'Automation timeout'}
if($p.ExitCode -ne 0) {throw "Editor exit code $($p.ExitCode)"}
$index=Join-Path $report 'index.json'
if(!(Test-Path -LiteralPath $index) -or (Get-Item -LiteralPath $index).LastWriteTime -lt $started) {throw 'No fresh automation index'}
$r=Get-Content -LiteralPath $index -Raw | ConvertFrom-Json
if($r.failed -gt 0 -or $r.notRun -gt 0 -or $r.inProcess -gt 0 -or ($r.succeeded+$r.succeededWithWarnings) -lt 14) {
    $r.tests | Where-Object state -ne 'Success' | Select-Object fullTestPath,state,entries | ConvertTo-Json -Depth 7 | Write-Output
    throw "Automation incomplete or failed: $($r.failed) failed"
}
Write-Output "Automation: $($r.succeeded) passed, $($r.succeededWithWarnings) warnings, $($r.failed) failed"
