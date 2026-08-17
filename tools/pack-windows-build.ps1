param(
    [string]$BuildDate = (Get-Date -Format "yyyy.MM.dd"),
    [string]$SourceRef = "HEAD",
    [string]$RuntimeArchive = "",
    [string]$OutputDirectory = "",
    [string]$StagingRoot = "",
    [string]$PythonCommand = "python",
    [switch]$Force,
    [switch]$KeepStaging
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "The Windows build must be created and smoke-tested on Windows."
}
if ($BuildDate -notmatch "^\d{4}\.\d{2}\.\d{2}$") {
    throw "BuildDate must use YYYY.MM.DD format."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$validatorPath = Join-Path $projectRoot "tools\windows_package.py"
if ([string]::IsNullOrWhiteSpace($RuntimeArchive)) {
    $RuntimeArchive = Join-Path `
        $projectRoot `
        "runtime\windows\zima-cad-runtime-windows.zip"
}
$RuntimeArchive = (Resolve-Path -LiteralPath $RuntimeArchive).Path

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot "builds"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
    $driveRoot = [System.IO.Path]::GetPathRoot($projectRoot)
    $StagingRoot = Join-Path $driveRoot "zb"
}
$StagingRoot = [System.IO.Path]::GetFullPath($StagingRoot)
if ($StagingRoot.TrimEnd("\") -eq `
        [System.IO.Path]::GetPathRoot($StagingRoot).TrimEnd("\")) {
    throw "StagingRoot must be a dedicated directory, not a drive root."
}
New-Item -ItemType Directory -Force -Path $StagingRoot | Out-Null

$python = Get-Command $PythonCommand -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw (
        "Python was not found. Activate the zima-cad Conda environment or " +
        "pass -PythonCommand."
    )
}
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -eq $git) {
    $git = Get-Command git -ErrorAction SilentlyContinue
}
if ($null -eq $git) {
    throw "Git was not found."
}
$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
if ($null -eq $tar) {
    throw "Windows tar.exe was not found."
}

$trackedStatus = @(
    & $git.Source -C $projectRoot status --porcelain --untracked-files=no
)
if ($LASTEXITCODE -ne 0) {
    throw "Cannot inspect the Git working tree."
}
if ($trackedStatus.Count -ne 0) {
    throw (
        "Tracked files have uncommitted changes. Commit them before creating " +
        "a reproducible Windows release build."
    )
}

$sourceCommit = (
    & $git.Source -C $projectRoot rev-parse "$SourceRef^{commit}"
).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceCommit)) {
    throw "Cannot resolve source ref: $SourceRef"
}

& $python.Source $validatorPath $RuntimeArchive --kind runtime
if ($LASTEXITCODE -ne 0) {
    throw "The selected Windows runtime archive failed validation."
}
$runtimeHash = (
    Get-FileHash -LiteralPath $RuntimeArchive -Algorithm SHA256
).Hash.ToLowerInvariant()

$buildName = "ZIMA-CAD-WINDOWS-$BuildDate"
$archivePath = Join-Path $OutputDirectory "$buildName.zip"
if ((Test-Path -LiteralPath $archivePath) -and -not $Force) {
    throw "Build archive already exists: $archivePath. Re-run with -Force."
}

$uniqueId = [guid]::NewGuid().ToString("N")
$workDir = Join-Path $StagingRoot "w-$uniqueId"
$distributionDir = Join-Path $workDir $buildName
$sourceTar = Join-Path $workDir "source.tar"
$candidatePath = Join-Path `
    $OutputDirectory `
    (".$buildName.$uniqueId.candidate.zip")

if (($workDir.Length + 1 + 180) -gt 240) {
    throw (
        "Staging path is too long: $workDir. Use a short path such as " +
        "-StagingRoot C:\zb."
    )
}

$smokeCode = (
    "import numpy as np; import zima_cad; " +
    "from PySide6 import QtCore; " +
    "from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox; " +
    "assert float(np.dot([1.0, 2.0], [3.0, 4.0])) == 11.0; " +
    "assert not BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape().IsNull()"
)

try {
    New-Item -ItemType Directory -Force -Path $distributionDir | Out-Null

    & $git.Source -C $projectRoot archive `
        --format=tar `
        "--output=$sourceTar" `
        $sourceCommit
    if ($LASTEXITCODE -ne 0) {
        throw "git archive failed for source commit: $sourceCommit"
    }
    & $tar.Source -xf $sourceTar -C $distributionDir
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot extract the committed source snapshot."
    }

    $runtimeTarget = Join-Path `
        $distributionDir `
        "runtime\windows\python"
    New-Item -ItemType Directory -Force -Path $runtimeTarget | Out-Null
    & $tar.Source -xf $RuntimeArchive -C $runtimeTarget
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot extract the Windows runtime into the build staging area."
    }
    New-Item `
        -ItemType Directory `
        -Force `
        -Path (Join-Path $distributionDir "Projects") | Out-Null

    $buildInfo = @(
        "BuildName=$buildName",
        "SourceCommit=$sourceCommit",
        "RuntimeSHA256=$runtimeHash",
        "CreatedUtc=$([DateTime]::UtcNow.ToString('o'))"
    )
    Set-Content `
        -LiteralPath (Join-Path $distributionDir "WINDOWS-BUILD-INFO.txt") `
        -Value $buildInfo `
        -Encoding Ascii

    $longestLength = 0
    $longestPath = ""
    foreach ($item in (Get-ChildItem `
            -LiteralPath $distributionDir `
            -Recurse `
            -Force)) {
        $relativePath = $item.FullName.Substring($workDir.Length + 1)
        $relativePath = $relativePath.Replace("\", "/")
        if ($relativePath.Length -gt $longestLength) {
            $longestLength = $relativePath.Length
            $longestPath = $relativePath
        }
    }
    if ($longestLength -gt 180) {
        throw (
            "Build staging contains a path of $longestLength characters; " +
            "limit is 180: $longestPath"
        )
    }

    # Windows tar/libarchive handles the large tree without the path failures
    # observed with Compress-Archive.
    & $tar.Source -a -c -f $candidatePath -C $workDir $buildName
    if ($LASTEXITCODE -ne 0) {
        throw "Windows tar.exe failed to create the build ZIP."
    }

    & $python.Source $validatorPath $candidatePath --kind build
    if ($LASTEXITCODE -ne 0) {
        throw "The candidate Windows build archive failed validation."
    }

    # Smoke-test the staged copy only after the still-relocatable files have
    # been captured in the ZIP. conda-unpack must never run before packaging.
    $condaUnpack = Join-Path `
        $runtimeTarget `
        "Scripts\conda-unpack.exe"
    $packedPython = Join-Path $runtimeTarget "python.exe"
    & $condaUnpack
    if ($LASTEXITCODE -ne 0) {
        throw "conda-unpack failed in the staged Windows build."
    }
    Push-Location $distributionDir
    try {
        & $packedPython -c $smokeCode
        if ($LASTEXITCODE -ne 0) {
            throw "Staged Windows build failed the runtime import smoke test."
        }
    }
    finally {
        Pop-Location
    }

    # Do not replace an existing release archive until all checks pass.
    Move-Item -LiteralPath $candidatePath -Destination $archivePath -Force
}
finally {
    if (Test-Path -LiteralPath $candidatePath) {
        Remove-Item -LiteralPath $candidatePath -Force
    }
    if ((Test-Path -LiteralPath $workDir) -and -not $KeepStaging) {
        Remove-Item -LiteralPath $workDir -Recurse -Force
    }
}

$archiveHash = (
    Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
).Hash.ToLowerInvariant()
Write-Host "Windows build archive written and verified:"
Write-Host $archivePath
Write-Host "Source commit: $sourceCommit"
Write-Host "Runtime SHA-256: $runtimeHash"
Write-Host "Build SHA-256: $archiveHash"
if ($KeepStaging) {
    Write-Host "Staging retained at: $workDir"
}
