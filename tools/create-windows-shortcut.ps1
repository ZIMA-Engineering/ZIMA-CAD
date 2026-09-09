# Desktop shortcut targets the GUI executable directly: no cmd.exe console.
param([string]$Destination = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'zima-cad.lnk'))
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$executable = @('cpp-windows-release', 'cpp-release', 'cpp-debug') |
    ForEach-Object { Join-Path $projectRoot "build\$_\zima-cad-cpp.exe" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $executable) { throw 'Build zima-cad-cpp before creating the shortcut.' }
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut([IO.Path]::GetFullPath($Destination))
$shortcut.TargetPath = $executable
$shortcut.Arguments = '--working-directory "' + $projectRoot + '"'
$shortcut.WorkingDirectory = $projectRoot
$shortcut.IconLocation = "$executable,0"
$shortcut.Description = 'ZIMA-CAD'
$shortcut.WindowStyle = 1
$shortcut.Save()
Write-Output "ZIMA-CAD shortcut: $Destination"
