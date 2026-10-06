param(
    [string]$Blender = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$kit = Join-Path $repoRoot 'FREE_Post_Apocalypse_Survivor_Environment_Kitbash_set-93d57f55\fbx\Post_apocalypse_kitbash_free\Fbx\12.fbx'
$survivor = Join-Path $repoRoot 'Survival_Character-11d20d01\fbx\survival_character.fbx'
foreach ($source in @($kit, $survivor)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required local FBX is missing: $source" }
}
if (-not $Blender) {
    foreach ($candidate in @(
        'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe',
        'C:\Program Files\Blender Foundation\Blender 3.6\blender.exe')) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Blender = $candidate; break }
    }
}
if (-not $Blender) {
    $found = Get-Command blender -ErrorAction SilentlyContinue
    if ($found) { $Blender = $found.Source }
}
if (-not $Blender -or -not (Test-Path -LiteralPath $Blender -PathType Leaf)) {
    throw 'Blender was not found. Pass -Blender <path-to-blender.exe>.'
}
& $Blender --background --factory-startup --python (Join-Path $PSScriptRoot 'build_apocalypse_assets.py')
if ($LASTEXITCODE -ne 0) { throw "Blender asset conversion failed: $LASTEXITCODE" }
$output = Join-Path $repoRoot 'build\local-assets\apocalypse'
foreach ($name in @('apocalypse_building.glb', 'survival_character.glb')) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf)) { throw "Missing output: $name" }
}
Write-Host "Apocalypse assets ready: $output"
