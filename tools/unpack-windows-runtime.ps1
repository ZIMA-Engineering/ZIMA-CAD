param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runtimeDir = Join-Path $projectRoot "runtime\windows"
$archivePath = Join-Path $runtimeDir "zima-cad-runtime-windows.zip"
$targetDir = Join-Path $runtimeDir "python"

if (-not (Test-Path -LiteralPath $archivePath)) {
    throw "Runtime archive not found: $archivePath"
}

if (Test-Path -LiteralPath $targetDir) {
    if (-not $Force) {
        throw "Target already exists: $targetDir. Re-run with -Force to replace it."
    }

    $resolvedTarget = Resolve-Path -LiteralPath $targetDir
    if (-not $resolvedTarget.Path.StartsWith($runtimeDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside runtime directory: $resolvedTarget"
    }

    Remove-Item -LiteralPath $resolvedTarget.Path -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
tar -xf $archivePath -C $targetDir

$unpack = Join-Path $targetDir "Scripts\conda-unpack.exe"
if (Test-Path -LiteralPath $unpack) {
    & $unpack
}

Write-Host "Windows runtime unpacked to:"
Write-Host $targetDir
