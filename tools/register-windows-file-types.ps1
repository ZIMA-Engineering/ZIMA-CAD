# Register current C++ documents for this Windows user, without admin rights.
[CmdletBinding(SupportsShouldProcess)]
param([string]$Executable)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $Executable) {
    $Executable = @('cpp-windows-release', 'cpp-release', 'cpp-debug') |
        ForEach-Object { Join-Path $projectRoot "build\$_\zima-cad-cpp.exe" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if (-not $Executable -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw 'Build zima-cad-cpp before registering file types.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$types = [ordered]@{
    '.prtz' = @('ZIMA.CAD.Part', 'ZIMA-CAD Part')
    '.asmz' = @('ZIMA.CAD.Assembly', 'ZIMA-CAD Assembly')
    '.drwz' = @('ZIMA.CAD.Drawing', 'ZIMA-CAD Drawing')
    '.frmz' = @('ZIMA.CAD.Format', 'ZIMA-CAD Drawing Format')
    '.tblz' = @('ZIMA.CAD.TitleBlock', 'ZIMA-CAD Title Block')
}
# %1 launches exactly the selected document in a fresh GUI process. There is
# no DDE handler, COM activation, command shell, or existing-instance dispatch.
$openCommand = '"' + $Executable + '" "%1"'
if (-not $PSCmdlet.ShouldProcess('HKCU\Software\Classes', 'Register ZIMA-CAD document types')) { return }
$classes = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes')
try {
    foreach ($extension in $types.Keys) {
        $progId, $description = $types[$extension]
        $key = $classes.CreateSubKey($progId)
        try {
            $key.SetValue('', $description)
            $icon = $key.CreateSubKey('DefaultIcon')
            try { $icon.SetValue('', '"' + $Executable + '",0') } finally { $icon.Dispose() }
            $verb = $key.CreateSubKey('shell\open\command')
            try { $verb.SetValue('', $openCommand) } finally { $verb.Dispose() }
        } finally { $key.Dispose() }
        $key = $classes.CreateSubKey($extension)
        try {
            $key.SetValue('', $progId)
            $offered = $key.CreateSubKey('OpenWithProgids')
            try { $offered.SetValue($progId, [byte[]]@(), [Microsoft.Win32.RegistryValueKind]::None) }
            finally { $offered.Dispose() }
        } finally { $key.Dispose() }
        Write-Output "$extension -> $openCommand"
    }
} finally { $classes.Dispose() }

# Keep protected Windows UserChoice selections intact. A previous explicit
# choice can be changed through Explorer > Open with > Choose another app.
if (-not ('ZimaCad.FileAssociationNotification' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace ZimaCad {
    public static class FileAssociationNotification {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
    }
}
'@
}
[ZimaCad.FileAssociationNotification]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
