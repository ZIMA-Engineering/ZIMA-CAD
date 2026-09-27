# Use the same native desktop-integration service as Settings and setup.
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Executable,
    [ValidateSet('register', 'repair', 'remove', 'status')]
    [string]$Action = 'register'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $Executable) {
    $Executable = @('cpp-windows-release', 'cpp-release', 'cpp-debug') |
        ForEach-Object { Join-Path $projectRoot "build\$_\zima-cad-cpp.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if (-not $Executable -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw 'Build zima-cad-cpp before managing file types.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($Action -ne 'status' -and -not $PSCmdlet.ShouldProcess($Executable, "Desktop integration: $Action")) { return }
$process = Start-Process -FilePath $Executable -ArgumentList "--desktop-integration=$Action" -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Desktop integration returned status $($process.ExitCode)." }
