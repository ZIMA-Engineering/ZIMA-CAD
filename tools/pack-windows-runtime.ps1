param(
    [string]$CondaBat = "C:\Users\vladi\miniforge3\condabin\conda.bat",
    [string]$EnvironmentName = "zima-cad"
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runtimeDir = Join-Path $projectRoot "runtime\windows"
$archivePath = Join-Path $runtimeDir "zima-cad-runtime-windows.zip"

New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null

& $CondaBat run -n base conda-pack `
    -n $EnvironmentName `
    -o $archivePath `
    --force

Write-Host "Windows runtime archive written to:"
Write-Host $archivePath
