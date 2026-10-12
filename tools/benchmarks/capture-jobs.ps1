param(
    [string]$Executable = 'build/vs2022-x64/tools/benchmarks/Release/REMIGltfPrepBenchmark.exe',
    [string]$Model = 'build/local-assets/raven/raven.glb',
    [string]$OutputDirectory = 'build/portfolio-evidence',
    [ValidateRange(5,1000)][int]$Repeats = 30
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$exePath = if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $root $Executable }
$modelPath = if ([IO.Path]::IsPathRooted($Model)) { $Model } else { Join-Path $root $Model }
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) { throw "Build REMIGltfPrepBenchmark Release first: $exePath" }
if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) { throw "Missing local glTF model: $modelPath" }
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Push-Location $root
try {
    & $exePath (Join-Path $root 'tests/fixtures/static_triangle.glb') (Join-Path $outputPath 'jobs-small.csv') "$Repeats"
    if ($LASTEXITCODE -ne 0) { throw "Small glTF preparation benchmark failed: $LASTEXITCODE" }
    & $exePath $modelPath (Join-Path $outputPath 'jobs-model.csv') "$Repeats"
    if ($LASTEXITCODE -ne 0) { throw "Model glTF preparation benchmark failed: $LASTEXITCODE" }
} finally { Pop-Location }
Write-Host "Raw Job System CSV files: $outputPath"
