[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$cppRoot = Join-Path $repoRoot "cpp"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstall = $null
if (Test-Path -LiteralPath $vswhere) {
    $vsInstall = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}
if (-not $vsInstall) {
    $vsInstall = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Directory `
        -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object {
            Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue
        } |
        Where-Object { Test-Path (Join-Path $_.FullName "VC\Auxiliary\Build\vcvars64.bat") } |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $vsInstall) {
    throw "Visual Studio with the Desktop development with C++ workload was not found."
}

$devShell = Join-Path $vsInstall "Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path -LiteralPath $devShell)) {
    throw "Visual Studio Developer PowerShell was not found at $devShell"
}
& $devShell -Arch amd64 -SkipAutomaticLocation

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) {
    $bundledVcpkg = Join-Path $vsInstall "VC\vcpkg"
    if (Test-Path -LiteralPath (Join-Path $bundledVcpkg "vcpkg.exe")) {
        $vcpkgRoot = $bundledVcpkg
    }
}
if (-not $vcpkgRoot -or
    -not (Test-Path -LiteralPath (Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
    throw "vcpkg was not found. Install the Visual Studio vcpkg component or set VCPKG_ROOT."
}
$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_DOWNLOADS = Join-Path $repoRoot "build\vcpkg-downloads"

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $cmake) {
    $cmake = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found. Install the Visual Studio CMake tools component."
}

$presetSuffix = $Configuration.ToLowerInvariant()
Push-Location $cppRoot
try {
    & $cmake --preset "windows-vcpkg-$presetSuffix"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

    if ($RunTests) {
        & $cmake --build --preset "windows-$presetSuffix" --parallel
    } else {
        & $cmake --build --preset "windows-$presetSuffix" --target zima-cad-cpp --parallel
    }
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE." }

    if ($RunTests) {
        $ctest = Join-Path (Split-Path $cmake -Parent) "ctest.exe"
        & $ctest --preset "windows-$presetSuffix"
        if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE." }
    }
} finally {
    Pop-Location
}

$output = Join-Path $repoRoot "build\cpp-windows-$presetSuffix\zima-cad-cpp.exe"
Write-Host "ZIMA-CAD Windows $Configuration build: $output"
