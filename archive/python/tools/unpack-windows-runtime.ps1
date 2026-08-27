param(
    [switch]$Force,
    [string]$PythonCommand = "python"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "The Windows runtime must be unpacked on Windows."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runtimeDir = Join-Path $projectRoot "runtime\windows"
$archivePath = Join-Path $runtimeDir "zima-cad-runtime-windows.zip"
$targetDir = Join-Path $runtimeDir "python"
$candidateDir = Join-Path `
    $runtimeDir `
    (".python-candidate-" + [guid]::NewGuid().ToString("N"))
$backupDir = Join-Path `
    $runtimeDir `
    (".python-previous-" + [guid]::NewGuid().ToString("N"))
$validatorPath = Join-Path $projectRoot "tools\windows_package.py"

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "Runtime archive not found: $archivePath"
}
if ((Test-Path -LiteralPath $targetDir) -and -not $Force) {
    throw "Target already exists: $targetDir. Re-run with -Force to replace it."
}

$python = Get-Command $PythonCommand -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw (
        "Python is required for archive validation. Activate the zima-cad " +
        "Conda environment or pass -PythonCommand."
    )
}
& $python.Source $validatorPath $archivePath --kind runtime
if ($LASTEXITCODE -ne 0) {
    throw "Windows runtime archive failed validation and was not unpacked."
}

try {
    New-Item -ItemType Directory -Force -Path $candidateDir | Out-Null
    & tar.exe -xf $archivePath -C $candidateDir
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot extract the Windows runtime archive."
    }

    foreach ($relativePath in @(
        "python.exe",
        "Scripts\conda-unpack.exe",
        "Library\bin\openblas.dll",
        "Library\bin\libblas.dll",
        "Library\bin\libcblas.dll"
    )) {
        $requiredPath = Join-Path $candidateDir $relativePath
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Extracted runtime is missing: $relativePath"
        }
    }

    if (Test-Path -LiteralPath $targetDir) {
        Move-Item -LiteralPath $targetDir -Destination $backupDir
    }
    try {
        Move-Item -LiteralPath $candidateDir -Destination $targetDir
    }
    catch {
        if (Test-Path -LiteralPath $backupDir) {
            Move-Item -LiteralPath $backupDir -Destination $targetDir
        }
        throw
    }

    if (Test-Path -LiteralPath $backupDir) {
        Remove-Item -LiteralPath $backupDir -Recurse -Force
    }
}
finally {
    if (Test-Path -LiteralPath $candidateDir) {
        Remove-Item -LiteralPath $candidateDir -Recurse -Force
    }
}

Write-Host "Windows runtime unpacked and verified:"
Write-Host $targetDir
Write-Host (
    "Relocation is intentionally pending. zima-cad.bat runs conda-unpack " +
    "once, after the application reaches its final directory."
)
