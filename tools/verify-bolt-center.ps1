[CmdletBinding()]
param(
    [string]$ModelDirectory = 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD/03-FULL-AB',
    [string]$OutputDirectory = 'build/bolt-center-verification',
    [switch]$PrepareRepair
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$runtime = Join-Path $repo 'build/cpp-windows-release/zima-cad-cli.exe'
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$manifest = [Collections.Generic.List[object]]::new()
$files = Get-ChildItem -LiteralPath (Join-Path $repo $ModelDirectory) -Filter '*.prtz' -Recurse
foreach ($file in $files) {
    $section = ''; $sketches = @{}; $history = @()
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        if ($line.StartsWith('[')) { $section = $line }
        if ($line.StartsWith('param.cpp_history=')) { $history += ,($line.Substring(18) | ConvertFrom-Json -AsHashtable -Depth 100) }
        if ($section.StartsWith('[Sketch.') -and $line.StartsWith('data=')) {
            $s = $line.Substring(5) | ConvertFrom-Json -AsHashtable -Depth 100
            $sketches[$s.id] = $s
        }
    }
    $feature = @($history | Where-Object { $_.name.EndsWith('004') })
    if ($feature.Count -ne 1) { throw "Expected one relief feature 004 in $($file.FullName)" }
    $sketch = $sketches[$feature[0].feature.sketch_id]
    if ($sketch.circles.Count -ne 2 -or $sketch.circles[0].center -ne $sketch.circles[1].center) { throw 'Expected two circles sharing one native center' }
    $center = $sketch.circles[0].center
    $dimension = @($sketch.dimensions | Where-Object { $_.kind -eq 'diameter' })[0]
    if (!$dimension) { throw 'Expected the authored relief diameter' }
    $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $folder = Join-Path $output ([string]$manifest.Count)
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $staged = Join-Path $folder $file.Name
    Copy-Item -LiteralPath $file.FullName -Destination $staged -Force
    $info = [Diagnostics.ProcessStartInfo]::new($runtime)
    $info.ArgumentList.Add('--stdin'); $info.ArgumentList.Add('--keep-going')
    $info.ArgumentList.Add('--working-directory'); $info.ArgumentList.Add($repo)
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $errors = $process.StandardError.ReadToEndAsync()
    $log = [Collections.Generic.List[object]]::new()
    function Invoke-BoltCommand([string]$Command, [hashtable]$Arguments = @{}, [switch]$Reject) {
        $request = @{command=$Command; arguments=$Arguments}
        $process.StandardInput.WriteLine(($request | ConvertTo-Json -Depth 30 -Compress)); $process.StandardInput.Flush()
        $line = $process.StandardOutput.ReadLine()
        if (!$line) { throw "CLI terminated during $Command" }
        $response = $line | ConvertFrom-Json -AsHashtable -Depth 100
        $log.Add(@{request=$request; response=$response})
        if ($Reject) { if ($response.ok) { throw "Unexpected accepted command: $Command" } }
        elseif (!$response.ok) { throw "$Command failed: $($response.code): $($response.message)" }
        return $response.data
    }
    function Check-BoltCenter([int]$Freedom, [double]$ExpectedX = 0) {
        $point = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$center}
        if ([Math]::Abs($point.entity.x-$ExpectedX) -gt 1e-7 -or [Math]::Abs($point.entity.y) -gt 1e-7) { throw 'Anchored center moved' }
        $status = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$sketch.id}
        if ($status.maximum_residual -gt 1e-7 -or $status.remaining_degrees_of_freedom -ne $Freedom) { throw 'Unexpected residual or degrees of freedom' }
        foreach ($reference in $sketch.external_references) {
            $actual = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$reference.id}
            if (($actual.entity.cached_points | ConvertTo-Json -Compress -Depth 10) -cne ($reference.cached_points | ConvertTo-Json -Compress -Depth 10)) { throw 'Read-only source moved' }
        }
    }
    $cases = 0
    try {
        $null = Invoke-BoltCommand 'open' @{path=$staged}
        foreach ($anchor in @($sketch.constraints | Where-Object { $_.kind -eq 'point_reference' -and $_.first -eq $center -and $_.second -eq 'sketch_origin' })) {
            $null = Invoke-BoltCommand 'sketch.constraint.delete' @{sketch=$sketch.id; constraint=$anchor.id}
        }
        $freeX = [double](@($sketch.points | Where-Object { $_.id -eq $center })[0].x)
        Check-BoltCenter 1 $freeX
        foreach ($support in @('sketch_axis:y','sketch_origin')) {
            $contactArguments = @{sketch=$sketch.id; kind='point_reference'; points=@($center,$support)}
            if ($support -ne 'sketch_origin') { $contactArguments = @{sketch=$sketch.id; kind='point_on_line'; points=@($center); geometry=@($support)} }
            $contact = Invoke-BoltCommand 'sketch.constraint.create' $contactArguments
            Check-BoltCenter 0
            $null = Invoke-BoltCommand 'undo'; Check-BoltCenter 1 $freeX
            $null = Invoke-BoltCommand 'redo'; Check-BoltCenter 0
            foreach ($kind in @('radius','diameter')) { foreach ($driving in @($true,$false)) { foreach ($locked in @($true,$false)) {
                $null = Invoke-BoltCommand 'sketch.dimension.set' @{sketch=$sketch.id; dimension=$dimension.id; driving=$false}
                $base = [double]$dimension.value / $(if ($kind -eq 'radius') { 2 } else { 1 })
                $test = Invoke-BoltCommand 'sketch.dimension.create' @{sketch=$sketch.id; kind=$kind; geometry=@($dimension.geometry); value=$base; driving=$driving; locked=$locked}
                foreach ($factor in @(0.98,1.02,1.0)) {
                    $before = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$dimension.geometry}
                    $change = Invoke-BoltCommand 'sketch.dimension.set' @{sketch=$sketch.id; dimension=$test.dimension; value=($base*$factor)}
                    $after = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$dimension.geometry}
                    $expected = $(if ($driving) { [double]$dimension.value*$factor/2 } else { [double]$before.entity.radius })
                    if ([Math]::Abs($after.entity.radius-$expected) -gt 1e-7) { throw 'Radius equation failed after dimension edit' }
                    Check-BoltCenter $(if ($driving) { 0 } else { 1 })
                    if ($change.changed) {
                        $null = Invoke-BoltCommand 'undo'
                        $restored = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$dimension.geometry}
                        if ([Math]::Abs($restored.entity.radius-$before.entity.radius) -gt 1e-7) { throw 'Dimension Undo failed' }
                        $null = Invoke-BoltCommand 'redo'; Check-BoltCenter $(if ($driving) { 0 } else { 1 })
                    }
                }
                foreach ($offset in @(0.1,-0.2,0.15)) {
                    $before = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$sketch.id}
                    $null = Invoke-BoltCommand 'sketch.point.move' @{sketch=$sketch.id; point=$center; position=@($offset,0.1)} -Reject
                    $after = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$sketch.id}
                    if ($after.revision -ne $before.revision) { throw 'Impossible drag changed the document revision' }
                    Check-BoltCenter $(if ($driving) { 0 } else { 1 })
                }
                $null = Invoke-BoltCommand 'sketch.dimension.delete' @{sketch=$sketch.id; dimension=$test.dimension}
                $null = Invoke-BoltCommand 'sketch.dimension.set' @{sketch=$sketch.id; dimension=$dimension.id; value=$dimension.value; driving=$dimension.driving; locked=$dimension.locked}
                ++$cases
            } } }
            $null = Invoke-BoltCommand 'sketch.constraint.delete' @{sketch=$sketch.id; constraint=$contact.constraint}
            foreach ($offset in @(0.1,-0.2,0.15)) {
                $null = Invoke-BoltCommand 'sketch.point.move' @{sketch=$sketch.id; point=$center; position=@($offset,0)}
                $point = Invoke-BoltCommand 'sketch.entity.get' @{sketch=$sketch.id; entity=$center}
                if ([Math]::Abs($point.entity.x-$offset) -gt 1e-7 -or [Math]::Abs($point.entity.y) -gt 1e-7) { throw 'Free axial drag did not move' }
                $null = Invoke-BoltCommand 'undo'; Check-BoltCenter 1
            }
            $freeX = 0
        }
        if ($PrepareRepair) {
            $null = Invoke-BoltCommand 'sketch.constraint.create' @{sketch=$sketch.id; kind='point_reference'; points=@($center,'sketch_origin')}
            Check-BoltCenter 0
            foreach ($suffix in @('001','002')) {
                $owner = @($history | Where-Object { $_.name.EndsWith($suffix) })[0]
                $profile = $sketches[$owner.feature.sketch_id]
                $circle = @($profile.circles)[0]
                if (!@($profile.constraints | Where-Object { $_.kind -eq 'point_reference' -and $_.first -eq $circle.center -and $_.second -eq 'sketch_origin' }).Count) {
                    $null = Invoke-BoltCommand 'sketch.constraint.create' @{sketch=$profile.id; kind='point_reference'; points=@($circle.center,'sketch_origin')}
                }
                $state = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$profile.id}
                if ($suffix -eq '002' -and $state.remaining_degrees_of_freedom -ne 0) {
                    $vertex = @($profile.points | Where-Object { $_.id -ne $circle.center } | Sort-Object -Property x -Descending)[0]
                    $null = Invoke-BoltCommand 'sketch.constraint.create' @{sketch=$profile.id; kind='point_on_line'; points=@($vertex.id); geometry=@('sketch_axis:x')}
                }
                $state = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$profile.id}
                if ($state.remaining_degrees_of_freedom -ne 0 -or $state.maximum_residual -gt 1e-7) { throw "Repaired profile $suffix is not fully constrained" }
            }
            $null = Invoke-BoltCommand 'regenerate'
            $null = Invoke-BoltCommand 'save'
            $null = Invoke-BoltCommand 'open' @{path=$staged}
            foreach ($profile in $sketches.Values) {
                $state = Invoke-BoltCommand 'sketch.solve_status' @{sketch=$profile.id}
                if ($state.remaining_degrees_of_freedom -ne 0 -or $state.maximum_residual -gt 1e-7) { throw 'Reopened bolt has an unconstrained profile' }
            }
        }
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Audit changed the source file' }
        $manifest.Add(@{source=$file.FullName; source_sha256=$sourceHash; staged=$staged; cases=$cases; repaired=[bool]$PrepareRepair})
        Write-Output "$($file.Name): $cases native cases passed"
    }
    finally {
        $process.StandardInput.Close(); $process.WaitForExit(); $process.Dispose()
        [IO.File]::WriteAllText((Join-Path $folder 'commands.json'),($log | ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
        $diagnostics = $errors.GetAwaiter().GetResult()
        if ($diagnostics) { Write-Output $diagnostics }
    }
}
[IO.File]::WriteAllText((Join-Path $output 'manifest.json'),($manifest | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
