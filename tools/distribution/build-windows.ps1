[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$Stage,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Commit = 'HEAD',
    [int]$Jobs = 4,
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'Visual Studio C++ tools are required.' }
& (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -SkipAutomaticLocation
$taskCmake = Join-Path $vsInstall 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$taskVcpkg = $env:VCPKG_ROOT
if (-not $taskVcpkg) { $taskVcpkg = Join-Path $vsInstall 'VC/vcpkg' }
$installed = Join-Path $repoRoot 'build/cpp-windows-release/vcpkg_installed/x64-windows'
$redist = @(Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC*.CRT') -Directory)
if ($redist.Count -ne 1) { throw 'Expected exactly one selected MSVC x64 CRT redistributable.' }
$arguments = @((Join-Path $PSScriptRoot 'package.py'), 'windows', '--commit', $Commit,
    '--stage', $Stage, '--output', $Output, '--installed', $installed,
    '--toolchain', (Join-Path $taskVcpkg 'scripts/buildsystems/vcpkg.cmake'),
    '--cmake', $taskCmake, '--redist', $redist[0].FullName, '--jobs', $Jobs)
if ($Release) { $arguments += '--release' }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw "Candidate build or validation failed: $LASTEXITCODE" }
