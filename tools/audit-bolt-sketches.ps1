[CmdletBinding()]
param([string]$Directory = 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD/03-FULL-AB')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$rows = [Collections.Generic.List[object]]::new()
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $repo $Directory) -Filter '*.prtz' -Recurse)) {
    $section = ''; $sketches = @()
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        if ($line.StartsWith('[')) { $section = $line }
        if ($section.StartsWith('[Sketch.') -and $line.StartsWith('data=')) { $sketches += ,($line.Substring(5) | ConvertFrom-Json -AsHashtable -Depth 100) }
    }
    $requests = [Collections.Generic.List[string]]::new()
    $requests.Add((@{command='open'; arguments=@{path=$file.FullName}} | ConvertTo-Json -Compress))
    foreach ($s in $sketches) { $requests.Add((@{command='sketch.solve_status'; arguments=@{sketch=$s.id}} | ConvertTo-Json -Compress)) }
    $script = Join-Path $repo 'build/bolt-sketch-audit.txt'
    [IO.File]::WriteAllLines($script,$requests,[Text.UTF8Encoding]::new($false))
    $answers = @(& (Join-Path $repo 'build/cpp-windows-release/zima-cad-cli.exe') --script $script)
    if ($LASTEXITCODE) { throw "Native audit failed for $($file.FullName)" }
    $results = @($answers | ForEach-Object { $_ | ConvertFrom-Json -AsHashtable -Depth 100 })
    for ($i=0; $i -lt $sketches.Count; ++$i) {
        $s = $sketches[$i]; $result = $results[$i+1].data
        $centers = @($s.circles | ForEach-Object {
            $center = $_.center
            $point = @($s.points | Where-Object { $_.id -eq $center })[0]
            @{point=$center; x=$point.x; y=$point.y; radius=$_.radius; contacts=@($s.constraints | Where-Object { $_.first -eq $center })}
        })
        $rows.Add(@{file=$file.FullName; sketch=$s.id; name=$s.name; segments=$s.segments.Count; circles=$s.circles.Count;
            constraints=$s.constraints.Count; dimensions=$s.dimensions.Count; freedom=$result.remaining_degrees_of_freedom;
            residual=$result.maximum_residual; status=$result.status; centers=$centers})
        Write-Output "$($file.Name) / $($s.name): $($result.status), DOF=$($result.remaining_degrees_of_freedom), residual=$($result.maximum_residual)"
    }
}
[IO.File]::WriteAllText((Join-Path $repo 'build/bolt-all-sketches-audit.json'),($rows | ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
