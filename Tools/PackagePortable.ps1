param(
    [string]$Source = (Join-Path $PSScriptRoot '../Builds/DigItUpVisual_20260909'),
    [string]$Destination = (Join-Path $PSScriptRoot '../Releases/DigItUp_Windows_USB_20260909/DigItUp'),
    [string]$Redist = 'D:/visual studio 2026/VC/Redist/MSVC/14.51.36231',
    [string]$Engine = 'D:/UE_5.8'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $Source).Path
$destinationRoot = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationRoot) { throw "Destination exists; refusing to overwrite: $destinationRoot" }
$exeRelative = 'SandExcavator/Binaries/Win64/SandExcavator.exe'
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot $exeRelative))) { throw 'Packaged game executable missing.' }
$crt = Join-Path $Redist 'x64/Microsoft.VC145.CRT'
$installer = Join-Path $Redist 'vc_redist.x64.exe'
if ((Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'Valid') { throw 'Runtime installer signature is not valid.' }
New-Item -ItemType Directory -Path $destinationRoot | Out-Null
# Keep runtime trees in full. Exclude only generated test/user data and debug symbols.
foreach ($tree in @('Engine','SandExcavator')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot $tree) -Recurse -File) {
        $relative = $file.FullName.Substring($sourceRoot.Length + 1)
        if ($relative -match '(^|[\\/])(Saved|Artifacts|Intermediate)([\\/]|$)' -or $file.Extension -in @('.pdb','.log')) { continue }
        $target = Join-Path $destinationRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'NOTICES.txt') -Destination $destinationRoot
$gameBin = Join-Path $destinationRoot 'SandExcavator/Binaries/Win64'
Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $gameBin
$prerequisites = Join-Path $destinationRoot 'Prerequisites'
New-Item -ItemType Directory -Path $prerequisites | Out-Null
Copy-Item -LiteralPath $installer -Destination $prerequisites
Copy-Item -LiteralPath (Join-Path $Engine 'Engine/Extras/Redist/en-us/GameInputRedist.msi') -Destination $prerequisites
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'PortableAssets') -File | Copy-Item -Destination $destinationRoot
# Normalize Windows text launchers for Explorer/cmd and Windows PowerShell 5.1.
Get-ChildItem -LiteralPath $destinationRoot -Filter '*.cmd' | ForEach-Object {
    $content = [IO.File]::ReadAllText($_.FullName) -replace '\r?\n', "`r`n"
    [IO.File]::WriteAllText($_.FullName,$content,[Text.Encoding]::ASCII)
}
$readme = Join-Path $destinationRoot '开始前请读我.txt'
$readmeText = [IO.File]::ReadAllText($readme) -replace '\r?\n', "`r`n"
[IO.File]::WriteAllText($readme,$readmeText,[Text.UTF8Encoding]::new($true))
$records = foreach ($file in Get-ChildItem -LiteralPath $destinationRoot -Recurse -File | Sort-Object FullName) {
    [PSCustomObject]@{
        Path = $file.FullName.Substring($destinationRoot.Length+1)
        Bytes = $file.Length
        SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$records | Export-Csv -LiteralPath (Join-Path $destinationRoot 'SHA256.csv') -NoTypeInformation -Encoding UTF8
$zipPath = (Split-Path -Parent $destinationRoot) + '.zip'
if (Test-Path -LiteralPath $zipPath) { throw "Archive exists; refusing to overwrite: $zipPath" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($destinationRoot,$zipPath,[IO.Compression.CompressionLevel]::Optimal,$true)
$zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
[PSCustomObject]@{Folder=$destinationRoot;FileCount=$records.Count;UncompressedMiB=[math]::Round(($records | Measure-Object Bytes -Sum).Sum/1MB,1);Archive=$zipPath;ZipMiB=[math]::Round((Get-Item -LiteralPath $zipPath).Length/1MB,1);ZipSHA256=$zipHash.Hash} | ConvertTo-Json
