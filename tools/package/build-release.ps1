param(
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$version = '0.11.0'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "build/distributions/$stamp" }
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw "Output directory already exists: $outputRoot" }
$null = New-Item -ItemType Directory -Path $outputRoot
$sourceName = "REMI-$version-source"
$runtimeName = "REMI-$version-Windows-x64"
$sourceParent = Join-Path $outputRoot 'source-staging'
$runtimeParent = Join-Path $outputRoot 'runtime-staging'
$sourceRoot = Join-Path $sourceParent $sourceName
$runtimeRoot = Join-Path $runtimeParent $runtimeName
$verifyRoot = Join-Path $outputRoot 'verification'
$null = New-Item -ItemType Directory -Force -Path $sourceRoot,$runtimeRoot,$verifyRoot

# Capture both committed files and this task's uncommitted working-tree files.
# Git's ignore rules exclude local exports, logs and prior build output.
$listed = & git -C $repoRoot -c core.quotepath=false ls-files --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0 -or -not $listed) { throw 'Could not list source snapshot files' }
$sourceEntries = [System.Collections.Generic.List[object]]::new()
$rootPrefix = $repoRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
foreach ($relative in ($listed | Sort-Object -Unique)) {
    if (-not $relative -or [System.IO.Path]::IsPathRooted($relative)) { throw "Invalid source path: $relative" }
    $from = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $relative))
    if (-not $from.StartsWith($rootPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Source path escapes repository: $relative"
    }
    if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { continue }
    $item = Get-Item -LiteralPath $from -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Source link requires explicit review: $relative"
    }
    $to = Join-Path $sourceRoot $relative
    $null = New-Item -ItemType Directory -Force -Path (Split-Path $to -Parent)
    Copy-Item -LiteralPath $from -Destination $to
    $sourceEntries.Add([pscustomobject]@{
        Path = $relative.Replace('\','/')
        SHA256 = (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}
$baseCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not identify base commit' }
$sourceInfo = [ordered]@{
    Version = $version
    BaseCommit = $baseCommit
    SourceState = 'Snapshot of the current working tree, including uncommitted changes'
    SnapshotDateLocal = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz')
    FileCount = $sourceEntries.Count
    Files = @($sourceEntries)
}
$sourceInfo | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sourceRoot 'SOURCE-MANIFEST.json') -Encoding utf8
$sourceZip = Join-Path $outputRoot "$sourceName.zip"
[System.IO.Compression.ZipFile]::CreateFromDirectory($sourceParent,$sourceZip,[System.IO.Compression.CompressionLevel]::Optimal,$false)

function Invoke-BuildStep([string]$label,[string]$command,[string[]]$arguments) {
    $log = Join-Path $verifyRoot "$label.log"
    Push-Location $sourceRoot
    try {
        & $command @arguments > $log 2>&1
        if ($LASTEXITCODE -ne 0) {
            $tail = (Get-Content -LiteralPath $log -Tail 25) -join [Environment]::NewLine
            throw "$label failed with exit code $LASTEXITCODE`n$tail"
        }
    } finally { Pop-Location }
    Write-Output "$label passed"
}

Invoke-BuildStep 'configure' 'cmake' @('--preset','portable')
Invoke-BuildStep 'build' 'cmake' @('--build','--preset','portable','--parallel','8')
Invoke-BuildStep 'ctest' 'ctest' @('--preset','portable','--parallel','4')
Invoke-BuildStep 'install' 'cmake' @('--install','build/portable','--config','Release','--prefix',$runtimeRoot)

$required = @(
    'REMIGravity.exe','REMIRelay.exe','REMISandbox.exe','shaders/Basic.hlsl',
    'gravity.project.json','assets/fonts/Pretendard-SemiBold.otf','assets/fonts/LICENSE.txt',
    'samples/static_triangle.glb','samples/sample.png','START-HERE.txt',
    'Play-Gravity.cmd','Play-Relay.cmd','Open-Inspector.cmd','Run-Smoke-Tests.cmd',
    'THIRD-PARTY-NOTICES.txt'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $relative) -PathType Leaf)) {
        throw "Installed package is missing $relative"
    }
}
foreach ($bad in @('*.pdb','*.obj','*.rmc')) {
    if (Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File -Filter $bad) {
        throw "Unexpected $bad in portable package"
    }
}

$compilerFile = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'build/portable/CMakeFiles') -Recurse -File -Filter CMakeCXXCompiler.cmake | Select-Object -First 1
if (-not $compilerFile) { throw 'MSVC compiler metadata missing' }
$compilerMatch = [regex]::Match((Get-Content -LiteralPath $compilerFile.FullName -Raw),'set\(CMAKE_CXX_COMPILER "([^"]+)"\)')
if (-not $compilerMatch.Success) { throw 'MSVC compiler path missing' }
$dumpbin = Join-Path (Split-Path $compilerMatch.Groups[1].Value -Parent) 'dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin)) { throw "Cannot inspect DLL imports: $dumpbin" }
$imports = [ordered]@{}
foreach ($name in @('REMIGravity.exe','REMIRelay.exe','REMISandbox.exe')) {
    $output = (& $dumpbin /DEPENDENTS (Join-Path $runtimeRoot $name)) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $name" }
    if ($output -match '(?i)\b(?:MSVCP\d*|VCRUNTIME\d*|MSVCR\d*|UCRTBASED)\.DLL\b') {
        throw "$name still imports the Visual C++ runtime DLL"
    }
    $imports[$name] = @([regex]::Matches($output,'(?im)^\s+([A-Za-z0-9_.-]+\.dll)\s*$') | ForEach-Object { $_.Groups[1].Value })
}

$buildInfo = [ordered]@{
    Version = $version
    Platform = 'Windows x64'
    Configuration = 'Release'
    Compiler = $compilerMatch.Groups[1].Value
    Runtime = 'MSVC static (/MT)'
    BaseCommit = $baseCommit
    SourceArchive = [System.IO.Path]::GetFileName($sourceZip)
    SourceState = 'Current working-tree snapshot; see SOURCE-MANIFEST.json in source archive'
    Character = 'Box fallback; local Quinn export excluded'
    DllImports = $imports
}
$buildInfo | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtimeRoot 'BUILD-INFO.json') -Encoding utf8

$manifest = Join-Path $runtimeRoot 'MANIFEST-SHA256.csv'
$entries = Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File | Where-Object { $_.FullName -ne $manifest } | Sort-Object FullName
$rows = foreach ($entry in $entries) {
    [pscustomobject]@{
        Path = $entry.FullName.Substring($runtimeRoot.Length+1).Replace('\','/')
        SHA256 = (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        Bytes = $entry.Length
    }
}
$rows | Export-Csv -LiteralPath $manifest -NoTypeInformation -Encoding utf8
$runtimeZip = Join-Path $outputRoot "$runtimeName.zip"
[System.IO.Compression.ZipFile]::CreateFromDirectory($runtimeParent,$runtimeZip,[System.IO.Compression.CompressionLevel]::Optimal,$false)

# Unpack into a separate path with a space and non-ASCII characters. Run with
# an unrelated current directory and a minimal PATH to catch accidental
# source-tree paths and missing runtime files.
$externalRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("REMI 실행 검증 " + [guid]::NewGuid().ToString('N'))
$extractRoot = Join-Path $externalRoot 'extracted'
$workRoot = Join-Path $externalRoot 'unrelated working directory'
$null = New-Item -ItemType Directory -Force -Path $extractRoot,$workRoot
[System.IO.Compression.ZipFile]::ExtractToDirectory($runtimeZip,$extractRoot)
$externalPackage = Join-Path $extractRoot $runtimeName
foreach ($row in (Import-Csv -LiteralPath (Join-Path $externalPackage 'MANIFEST-SHA256.csv'))) {
    $file = Join-Path $externalPackage $row.Path
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or
        (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.SHA256) {
        throw "Extracted package hash mismatch: $($row.Path)"
    }
}

$checks = @(
    @{ Name = 'gravity-warp'; Exe = 'REMIGravity.exe'; Args = '--smoke --warp' },
    @{ Name = 'gravity-level2-warp'; Exe = 'REMIGravity.exe'; Args = '--smoke --warp --level 2' },
    @{ Name = 'relay-warp'; Exe = 'REMIRelay.exe'; Args = '--smoke --warp' },
    @{ Name = 'inspector-warp'; Exe = 'REMISandbox.exe'; Args = '--smoke --warp --inspector --static-gltf "' + (Join-Path $externalPackage 'samples/static_triangle.glb') + '"' },
    @{ Name = 'gravity-hardware'; Exe = 'REMIGravity.exe'; Args = '--smoke' },
    @{ Name = 'relay-hardware'; Exe = 'REMIRelay.exe'; Args = '--smoke' },
    @{ Name = 'inspector-hardware'; Exe = 'REMISandbox.exe'; Args = '--smoke --inspector --static-gltf "' + (Join-Path $externalPackage 'samples/static_triangle.glb') + '"' }
)
$originalPath = $env:PATH
$results = [System.Collections.Generic.List[object]]::new()
try {
    $env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    foreach ($check in $checks) {
        $stdout = Join-Path $verifyRoot ($check.Name + '.stdout.log')
        $stderr = Join-Path $verifyRoot ($check.Name + '.stderr.log')
        $process = Start-Process -FilePath (Join-Path $externalPackage $check.Exe) `
            -ArgumentList $check.Args -WorkingDirectory $workRoot -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            throw "$($check.Name) exceeded 60 seconds"
        }
        $process.Refresh()
        if ($process.ExitCode -ne 0) {
            $detail = ((Get-Content -LiteralPath $stdout -Tail 20) +
                (Get-Content -LiteralPath $stderr -Tail 20)) -join [Environment]::NewLine
            throw "$($check.Name) failed with exit code $($process.ExitCode)`n$detail"
        }
        $results.Add([pscustomobject]@{ Name = $check.Name; ExitCode = $process.ExitCode })
        Write-Output "$($check.Name) passed"
    }
} finally { $env:PATH = $originalPath }

$verification = [ordered]@{
    SourceBuild = 'portable Release build from isolated source snapshot'
    CTest = 'passed; details in verification/ctest.log'
    PortablePackage = 'ZIP extracted outside repository; SHA256 manifest verified'
    MinimalPath = 'System32 and Windows directory only'
    RunChecks = @($results)
    ExtractedPath = $externalPackage
}
$verification | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $verifyRoot 'VERIFICATION.json') -Encoding utf8
$hashes = foreach ($archive in @($runtimeZip,$sourceZip)) {
    [pscustomobject]@{
        File = [System.IO.Path]::GetFileName($archive)
        SHA256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        Bytes = (Get-Item -LiteralPath $archive).Length
    }
}
$hashes | Export-Csv -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.csv') -NoTypeInformation -Encoding utf8
# Keep only distributable archives and verification records in the output.
$outputPrefix = $outputRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
foreach ($stage in @($sourceParent,$runtimeParent)) {
    $resolvedStage = [System.IO.Path]::GetFullPath($stage)
    if (-not $resolvedStage.StartsWith($outputPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Staging path escapes output directory: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
Write-Output "Runtime: $runtimeZip"
Write-Output "Source: $sourceZip"
Write-Output "Verification: $(Join-Path $verifyRoot 'VERIFICATION.json')"
