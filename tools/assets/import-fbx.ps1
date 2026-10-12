param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$MaterialMap,
    [switch]$StaticRestPose,
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe'
)
$ErrorActionPreference = 'Stop'
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
if (-not (Test-Path -LiteralPath $Blender -PathType Leaf)) { throw "Blender missing: $Blender" }
$target = [System.IO.Path]::GetFullPath($Output)
$arguments = @('--background','--factory-startup','--python',
    (Join-Path $PSScriptRoot 'import_fbx.py'),'--','--source',$sourcePath,'--output',$target)
if ($MaterialMap) { $arguments += @('--material-map',(Resolve-Path -LiteralPath $MaterialMap).Path) }
if ($StaticRestPose) { $arguments += '--static-rest-pose' }
if (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target }
& $Blender @arguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $target -PathType Leaf)) {
    throw "FBX import failed: $sourcePath"
}
Write-Host "REMI GLB ready: $target"
