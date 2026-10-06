param(
    [string]$Reference = '6d6c776ccf24af87952657bcb4ab44735c81b16a',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$comparisonRoot = Join-Path $repoRoot 'build/scene-comparison'
$referenceRoot = Join-Path $comparisonRoot 'reference'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build/benchmark-results' }
$null = New-Item -ItemType Directory -Force -Path (Join-Path $referenceRoot 'include/remi/scene'), $OutputDirectory
foreach ($entry in @(
    @{ Source = 'engine/include/remi/scene/Scene.hpp'; Destination = 'include/remi/scene/Scene.hpp' },
    @{ Source = 'engine/src/scene/Scene.cpp'; Destination = 'Scene.cpp' }
)) {
    $sourceText = & git -C $repoRoot show "${Reference}:$($entry.Source)"
    if ($LASTEXITCODE -ne 0) { throw "Cannot read reference source $($entry.Source)" }
    [System.IO.File]::WriteAllText((Join-Path $referenceRoot $entry.Destination),
        ($sourceText -join "`n"), [System.Text.UTF8Encoding]::new($false))
}
& cmake -S (Join-Path $PSScriptRoot 'scene-comparison') -B (Join-Path $comparisonRoot 'build') `
    -G 'Visual Studio 17 2022' -A x64 "-DREMI_ROOT=$repoRoot" "-DREMI_REFERENCE_DIR=$referenceRoot"
if ($LASTEXITCODE -ne 0) { throw 'Scene comparison configure failed' }
& cmake --build (Join-Path $comparisonRoot 'build') --config Release --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Scene comparison build failed' }
Write-Output "Reference commit: $Reference; Release; 2 warmups and 20 samples per variant"
foreach ($variant in @('baseline','current')) {
    & (Join-Path $comparisonRoot "build/Release/scene_$variant.exe") (Join-Path $OutputDirectory "scene-$variant.csv")
    if ($LASTEXITCODE -ne 0) { throw "Scene $variant benchmark failed" }
}
