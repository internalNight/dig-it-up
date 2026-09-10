$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifest = Import-Csv -LiteralPath (Join-Path $root 'SHA256.csv')
$failures = 0
foreach ($entry in $manifest) {
    $path = Join-Path $root $entry.Path
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-Host "MISSING: $($entry.Path)" -ForegroundColor Red
        $failures++
    } elseif ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.SHA256) {
        Write-Host "CHANGED: $($entry.Path)" -ForegroundColor Red
        $failures++
    }
}
if ($failures) {
    Write-Host "$failures file(s) failed. Copy/extract the complete original package again." -ForegroundColor Red
    exit 1
}
Write-Host "PASS: all $($manifest.Count) packaged files match their SHA-256 hashes." -ForegroundColor Green
Write-Host 'This checks transfer integrity, not hardware compatibility or publisher identity.'
exit 0
