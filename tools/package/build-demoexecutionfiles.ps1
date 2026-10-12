param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$release = Join-Path $root 'build\vs2022-x64\bin\Release'
$output = Join-Path $root 'demoexecutionfiles'

if (-not $SkipBuild) {
    Push-Location $root
    try {
        $targets = @('REMIGravity','REMIRelay','REMISandbox','REMIVillage')
        if (Test-Path -LiteralPath (Join-Path $root 'build\local-assets\raven\raven.glb') -PathType Leaf) {
            $targets += 'REMIRaven'
        }
        $mazePieces = 'a','b','c','d','e','f','g','h'
        if (@($mazePieces | Where-Object {
                -not (Test-Path -LiteralPath (Join-Path $root "build\local-assets\ancient-ruins\ruin_${_}.glb") -PathType Leaf)
            }).Count -eq 0) {
            $targets += 'REMIMazeEscape'
        }
        & cmake --build --preset release --target @targets
        if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
    } finally { Pop-Location }
}

$files = @(
    @('build\vs2022-x64\bin\Release\REMIGravity.exe','REMIGravity.exe'),
    @('build\vs2022-x64\bin\Release\REMIRelay.exe','REMIRelay.exe'),
    @('build\vs2022-x64\bin\Release\REMISandbox.exe','REMISandbox.exe'),
    @('build\vs2022-x64\bin\Release\REMIVillage.exe','REMIVillage.exe'),
    @('build\vs2022-x64\bin\Release\gravity.project.json','gravity.project.json'),
    @('build\vs2022-x64\bin\Release\shaders\Basic.hlsl','shaders\Basic.hlsl'),
    @('build\vs2022-x64\bin\Release\shaders\PostProcess.hlsl','shaders\PostProcess.hlsl'),
    @('build\vs2022-x64\bin\Release\shaders\DeferredGeometry.hlsl','shaders\DeferredGeometry.hlsl'),
    @('build\vs2022-x64\bin\Release\shaders\DeferredLighting.hlsl','shaders\DeferredLighting.hlsl'),
    @('assets\fonts\NotoSansKR-VF.ttf','assets\fonts\NotoSansKR-VF.ttf'),
    @('assets\fonts\NotoSansKR-OFL.txt','assets\fonts\NotoSansKR-OFL.txt'),
    @('assets\fonts\Pretendard-SemiBold.otf','assets\fonts\Pretendard-SemiBold.otf'),
    @('assets\fonts\LICENSE.txt','assets\fonts\LICENSE.txt'),
    @('assets\Quinn\quinn.rmc','assets\Quinn\quinn.rmc'),
    @('assets\Quinn\quinn.rmc','assets\characters\default.rmc'),
    @('build\local-assets\quarry\quarry_map.glb','build\local-assets\quarry\quarry_map.glb'),
    @('build\local-assets\quarry\survival_character.glb','build\local-assets\quarry\survival_character.glb'),
    @('build\local-assets\soul-city\soul_city_neighborhood.glb','build\local-assets\soul-city\soul_city_neighborhood.glb'),
    @('build\local-assets\soul-city\lights.csv','build\local-assets\soul-city\lights.csv'),
    @('tests\fixtures\static_triangle.glb','samples\static_triangle.glb'),
    @('tests\fixtures\sample.png','samples\sample.png'),
    @('tools\package\runtime\THIRD-PARTY-NOTICES.txt','THIRD-PARTY-NOTICES.txt')
)
if (Test-Path -LiteralPath (Join-Path $root 'build\local-assets\raven\raven.glb') -PathType Leaf) {
    $files += ,@('build\vs2022-x64\bin\Release\REMIRaven.exe','REMIRaven.exe')
    $files += ,@('build\local-assets\raven\raven.glb','build\local-assets\raven\raven.glb')
}
$mazePieces = 'a','b','c','d','e','f','g','h'
$mazeReady = @($mazePieces | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $root "build\local-assets\ancient-ruins\ruin_${_}.glb") -PathType Leaf)
}).Count -eq 0
if ($mazeReady -and (Test-Path -LiteralPath (Join-Path $release 'REMIMazeEscape.exe') -PathType Leaf)) {
    $files += ,@('build\vs2022-x64\bin\Release\REMIMazeEscape.exe','REMIMazeEscape.exe')
    foreach ($piece in $mazePieces) {
        $path = "build\local-assets\ancient-ruins\ruin_$piece.glb"
        $files += ,@($path,$path)
    }
}

# Check all inputs before writing a partial demo folder.
foreach ($entry in $files) {
    $source = Join-Path $root $entry[0]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Demo input missing: $source" }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($entry in $files) {
    $source = Join-Path $root $entry[0]
    $destination = Join-Path $output $entry[1]
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$launchers = @{
    'Play-Gravity.cmd' = 'REMIGravity.exe'
    'Play-Relay.cmd' = 'REMIRelay.exe'
    'Play-Quarry.cmd' = 'REMIVillage.exe --quarry'
    'Play-SoulCity.cmd' = 'REMIVillage.exe --soul-city'
    'Open-Inspector.cmd' = 'REMISandbox.exe --inspector --static-gltf "samples\static_triangle.glb"'
}
foreach ($entry in $launchers.GetEnumerator()) {
    $script = @"
@echo off
setlocal
cd /d "%~dp0"
$($entry.Value) %*
set "REMI_RESULT=%ERRORLEVEL%"
if not "%REMI_RESULT%"=="0" pause
exit /b %REMI_RESULT%
"@
    Set-Content -LiteralPath (Join-Path $output $entry.Key) -Value $script -Encoding Ascii
}
if ($mazeReady -and (Test-Path -LiteralPath (Join-Path $output 'REMIMazeEscape.exe') -PathType Leaf)) {
    $mazeLauncher = '@echo off' + "`r`n" + 'setlocal' + "`r`n" + 'cd /d "%~dp0"' + "`r`n" + 'REMIMazeEscape.exe %*' + "`r`n" + 'if errorlevel 1 pause' + "`r`n"
    Set-Content -LiteralPath (Join-Path $output 'Play-MazeEscape.cmd') -Value $mazeLauncher -Encoding Ascii
}
$smoke = @'
@echo off
setlocal
cd /d "%~dp0"
REMIGravity.exe --smoke --warp || exit /b 1
REMIRelay.exe --smoke --warp || exit /b 1
REMISandbox.exe --smoke --warp || exit /b 1
REMIVillage.exe --quarry --smoke --warp || exit /b 1
REMIVillage.exe --soul-city --smoke --warp || exit /b 1
if exist REMIRaven.exe REMIRaven.exe --smoke --warp || exit /b 1
echo All supported demo smoke runs passed.
'@
Set-Content -LiteralPath (Join-Path $output 'Run-Smoke-Tests.cmd') -Value $smoke -Encoding Ascii

$readme = @'
# REMI local demo executables

Double-click an EXE in this folder. The EXE files and their shaders, fonts,
project file, sample, maps and characters are kept in relative paths so this
folder can be copied to another Windows x64 machine as one unit.

- Play-Gravity.cmd: gravity reversal game (Quinn character)
- Play-Relay.cmd: switch and timer game
- Play-Quarry.cmd: third-person quarry exploration
- Play-SoulCity.cmd: local Soul City prototype (Quinn character)
- REMIRaven.exe: direct launch of the local Raven/Soul City demo when included
- Play-MazeEscape.cmd: third-person Ancient Ruins maze when local assets are present
- Open-Inspector.cmd: scene inspection tool
- Run-Smoke-Tests.cmd: WARP checks for demos with smoke mode (Maze is manual)

Ancient Ruins, Soul City, Quarry and character assets are local third-party content. Verify
their license and redistribution rights before sharing this folder or ZIP.
The demoexecutionfiles folder is excluded from Git for that reason.

To refresh after a source build, run:
  powershell -ExecutionPolicy Bypass -File tools\package\build-demoexecutionfiles.ps1
'@
Set-Content -LiteralPath (Join-Path $output 'README.md') -Value $readme -Encoding Utf8

$manifest = foreach ($entry in $files) {
    $file = Join-Path $output $entry[1]
    [pscustomobject]@{ path = ($entry[1] -replace '\\','/'); bytes = (Get-Item -LiteralPath $file).Length;
        sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $output 'MANIFEST.json') -Encoding Utf8
Write-Host "Demo executables ready: $output"
$launcherCount = $launchers.Count + [int]($mazeReady -and (Test-Path -LiteralPath (Join-Path $output 'Play-MazeEscape.cmd') -PathType Leaf))
Write-Host "Files: $($files.Count) runtime inputs, $launcherCount launchers, 1 smoke runner"
