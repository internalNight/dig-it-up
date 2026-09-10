param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$templatePath = Join-Path $projectRoot 'Artifacts\column-collapse.template.html'
$dataPath = Join-Path $projectRoot 'Artifacts\column-collapse-data.json'
$template = [System.IO.File]::ReadAllText($templatePath)
$data = [System.IO.File]::ReadAllText($dataPath)
if (-not $template.Contains('__COLLAPSE_DATA__')) {
    throw 'Visualization template is missing its data placeholder.'
}
$outputDirectory = Split-Path -Parent $OutputPath
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
[System.IO.File]::WriteAllText(
    $OutputPath,
    $template.Replace('__COLLAPSE_DATA__', $data),
    [System.Text.UTF8Encoding]::new($false))
