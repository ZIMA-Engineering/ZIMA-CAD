param(
    [string]$CondaBat = "",
    [string]$EnvironmentName = "zima-cad"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-CondaCommand {
    param([string]$RequestedCommand)

    if (-not [string]::IsNullOrWhiteSpace($RequestedCommand)) {
        $requested = Get-Command $RequestedCommand -ErrorAction SilentlyContinue
        if ($null -eq $requested) {
            throw "Conda command was not found: $RequestedCommand"
        }
        if ([string]::IsNullOrWhiteSpace($requested.Source)) {
            return $requested.Name
        }
        return $requested.Source
    }

    foreach ($candidate in @("conda.bat", "conda.exe", "conda")) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            if ([string]::IsNullOrWhiteSpace($command.Source)) {
                return $command.Name
            }
            return $command.Source
        }
    }
    throw "Conda was not found. Activate Miniforge/Conda or pass -CondaBat."
}

if ($env:OS -ne "Windows_NT") {
    throw "The Windows runtime must be built on Windows."
}

$CondaBat = Resolve-CondaCommand $CondaBat
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runtimeDir = Join-Path $projectRoot "runtime\windows"
$archivePath = Join-Path $runtimeDir "zima-cad-runtime-windows.zip"
$candidatePath = Join-Path $runtimeDir "zima-cad-runtime-windows.candidate.zip"
$validatorPath = Join-Path $projectRoot "tools\windows_package.py"
$smokeRoot = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    ("zcad-runtime-smoke-" + [guid]::NewGuid().ToString("N"))

New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
if (Test-Path -LiteralPath $candidatePath) {
    Remove-Item -LiteralPath $candidatePath -Force
}

# The working Windows baseline uses OpenBLAS. Checking Conda metadata as well
# as DLLs prevents a nominally successful package that fails during NumPy
# import on another machine.
$prefixOutput = @(
    & $CondaBat run -n $EnvironmentName python -c `
        "import sys; print(sys.prefix)"
)
if ($LASTEXITCODE -ne 0) {
    throw "Cannot resolve Conda environment: $EnvironmentName"
}
$environmentPrefix = ($prefixOutput | Select-Object -Last 1).Trim()
if (-not (Test-Path -LiteralPath $environmentPrefix -PathType Container)) {
    throw "Conda returned an invalid environment prefix: $environmentPrefix"
}

$packageJson = (@(
    & $CondaBat list -n $EnvironmentName --json
) -join [Environment]::NewLine)
if ($LASTEXITCODE -ne 0) {
    throw "Cannot inspect packages in Conda environment: $EnvironmentName"
}
$packages = @($packageJson | ConvertFrom-Json)
foreach ($packageName in @("libblas", "libcblas", "liblapack")) {
    $package = @($packages | Where-Object { $_.name -eq $packageName })
    if ($package.Count -ne 1 -or $package[0].build_string -notmatch "openblas") {
        throw (
            "$packageName must use the OpenBLAS build in $EnvironmentName. " +
            "Install the conda-forge '*openblas' variants before packaging."
        )
    }
}
if (@($packages | Where-Object { $_.name -eq "libopenblas" }).Count -ne 1) {
    throw "libopenblas is missing from Conda environment: $EnvironmentName"
}

$environmentBin = Join-Path $environmentPrefix "Library\bin"
foreach ($dllName in @("openblas.dll", "libblas.dll", "libcblas.dll")) {
    $dllPath = Join-Path $environmentBin $dllName
    if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
        throw "Required OpenBLAS runtime DLL was not found: $dllPath"
    }
}

$smokeCode = (
    "import numpy as np; import zima_cad; " +
    "from PySide6 import QtCore; " +
    "from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox; " +
    "assert float(np.dot([1.0, 2.0], [3.0, 4.0])) == 11.0; " +
    "assert not BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape().IsNull()"
)

Push-Location $projectRoot
try {
    & $CondaBat run -n $EnvironmentName python -c $smokeCode
    if ($LASTEXITCODE -ne 0) {
        throw "Source Windows environment failed the import smoke test."
    }
}
finally {
    Pop-Location
}

# Headers, build-system metadata, documentation and debug symbols are not
# runtime inputs. Besides wasting space, the Viskores headers previously made
# the final archive paths too long for normal Windows extraction locations.
$excludePatterns = @(
    "include/*",
    "Library/include/*",
    "Library/lib/cmake/*",
    "Library/lib/pkgconfig/*",
    "Library/share/doc/*",
    "Library/share/man/*",
    "Library/lib/qt6/qml/Qt/test/*",
    "*.pdb"
)
$packArguments = @(
    "run", "-n", "base", "conda-pack",
    "-n", $EnvironmentName,
    "-o", $candidatePath,
    "--force"
)
foreach ($pattern in $excludePatterns) {
    $packArguments += @("--exclude", $pattern)
}

try {
    & $CondaBat @packArguments
    if ($LASTEXITCODE -ne 0) {
        throw "conda-pack failed for environment: $EnvironmentName"
    }

    & $CondaBat run -n $EnvironmentName python `
        $validatorPath $candidatePath --kind runtime
    if ($LASTEXITCODE -ne 0) {
        throw "The candidate Windows runtime archive failed validation."
    }

    if (($smokeRoot.Length + 1 + 140) -gt 240) {
        throw (
            "Temporary path is too long for the runtime smoke test: " +
            "$smokeRoot. Point TEMP to a short directory and retry."
        )
    }
    New-Item -ItemType Directory -Force -Path $smokeRoot | Out-Null

    & tar.exe -xf $candidatePath -C $smokeRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot extract the candidate Windows runtime archive."
    }

    $condaUnpack = Join-Path $smokeRoot "Scripts\conda-unpack.exe"
    $packedPython = Join-Path $smokeRoot "python.exe"
    & $condaUnpack
    if ($LASTEXITCODE -ne 0) {
        throw "conda-unpack failed in the temporary runtime."
    }

    Push-Location $projectRoot
    try {
        & $packedPython -c $smokeCode
        if ($LASTEXITCODE -ne 0) {
            throw "Packed Windows runtime failed the import smoke test."
        }
    }
    finally {
        Pop-Location
    }

    # Preserve the last known-good runtime until every candidate check passes.
    Move-Item -LiteralPath $candidatePath -Destination $archivePath -Force
}
finally {
    if (Test-Path -LiteralPath $candidatePath) {
        Remove-Item -LiteralPath $candidatePath -Force
    }
    if (Test-Path -LiteralPath $smokeRoot) {
        Remove-Item -LiteralPath $smokeRoot -Recurse -Force
    }
}

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
Write-Host "Windows runtime archive written and verified:"
Write-Host $archivePath
Write-Host "SHA-256: $archiveHash"
