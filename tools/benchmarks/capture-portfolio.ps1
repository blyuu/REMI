param(
    [string]$Executable = 'build/vs2022-x64/bin/Release/REMIRaven.exe',
    [string]$OutputDirectory = 'build/portfolio-evidence',
    [ValidateRange(30,10000)][int]$Frames = 300,
    [ValidateRange(0,10000)][int]$Warmup = 120
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$exePath = if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $root $Executable }
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) { throw "Build REMIRaven Release first: $exePath" }
foreach ($asset in @('build/local-assets/raven/raven.glb','build/local-assets/soul-city/soul_city_neighborhood.glb')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $asset) -PathType Leaf)) { throw "Missing local asset: $asset" }
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Push-Location $root
try {
    foreach ($route in @('deferred','forward')) {
        foreach ($culling in @('on','off')) {
            $name = "$route-culling-$culling"
            $arguments = @('--profile-csv',(Join-Path $outputPath "$name.csv"),
                '--profile-frames',"$Frames",'--warmup-frames',"$Warmup")
            if ($route -eq 'forward') { $arguments += '--forward' }
            if ($culling -eq 'off') { $arguments += '--no-culling' }
            Write-Host "Measuring $name at 1920x1080, hardware GPU, VSync OFF"
            & $exePath @arguments
            if ($LASTEXITCODE -ne 0) { throw "$name failed with exit code $LASTEXITCODE" }
            $rows = @(Import-Csv -LiteralPath (Join-Path $outputPath "$name.csv"))
            if ($rows.Count -ne $Frames) { throw "$name wrote $($rows.Count) rows; expected $Frames" }
            if (@($rows | Where-Object { $_.width -ne '1920' -or $_.height -ne '1080' -or
                $_.route -ne $route -or $_.culling -ne $culling }).Count) {
                throw "$name changed scene conditions during measurement"
            }
            $validGpu = @($rows | Where-Object { $_.gpu_valid -eq '1' -and $_.gpu_total_ms }).Count
            if (-not $validGpu) { Write-Warning "$name has no valid GPU samples; do not report a GPU time" }
        }
    }
} finally { Pop-Location }
Write-Host "Raw CSV files: $outputPath"
