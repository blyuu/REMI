param(
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe',
    [int]$TextureSize = 2048
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$archive = Join-Path $root 'Models\Stellar Blade - Raven.zip'
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Raven archive missing: $archive" }
if (-not (Test-Path -LiteralPath $Blender -PathType Leaf)) { throw "Blender missing: $Blender" }
$source = Join-Path $root 'build\raven-source'
$model = Join-Path $source 'Raw Files\Model\Raven.fbx'
if (-not (Test-Path -LiteralPath $model -PathType Leaf)) {
    New-Item -ItemType Directory -Path $source -Force | Out-Null
    & tar -xf $archive -C $source
    if ($LASTEXITCODE -ne 0) { throw "Raven extraction failed: $LASTEXITCODE" }
}
$out = Join-Path $root 'build\local-assets\raven'
$target = Join-Path $out 'raven.glb'
if (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target }
& $Blender --background --factory-startup --python (Join-Path $PSScriptRoot 'import_raven.py') -- `
    --source (Join-Path $source 'Raw Files\Model') --output $out --texture-size $TextureSize
if ($LASTEXITCODE -ne 0) { throw "Raven import failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw 'Raven GLB missing' }
Write-Host "Local Raven import ready: $out"
