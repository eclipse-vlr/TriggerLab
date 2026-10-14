param(
    [string]$BuildDir = 'build-release',
    [string]$OutputDir = 'dist',
    [string]$Version = '0.4.0-preview.3'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.-]*$') { throw 'Invalid version' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$cache = Get-Content -LiteralPath (Join-Path $buildPath 'CMakeCache.txt') -Raw
if ($cache -notmatch 'TRIGGERLAB_STATIC_RUNTIME:BOOL=ON' -or $cache -notmatch 'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64') {
    throw 'Configure an x64 build with TRIGGERLAB_STATIC_RUNTIME=ON before packaging'
}
$outputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
$repoPrefix = $repoRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (!$outputPath.StartsWith($repoPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output directory must be inside the repository' }
$name = "TriggerLab-$Version-windows-x64"
$stagingPath = Join-Path $outputPath ('staging-' + [Guid]::NewGuid().ToString('N'))
$packagePath = Join-Path $stagingPath $name
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
foreach ($exe in @('TriggerLab.exe','TriggerLabTrainer.exe')) {
    $source = Join-Path $buildPath "Release/$exe"
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing $source" }
    Copy-Item -LiteralPath $source -Destination $packagePath
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE.txt') -Destination $packagePath
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/QUICKSTART-RU.txt') -Destination (Join-Path $packagePath 'README.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Destination $packagePath
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/trainer-validation.md') -Destination $packagePath
$revision = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git revision' }
$dirty = [bool](& git -C $repoRoot status --porcelain)
$manifest = "version=$Version`nrevision=$revision`nsource_dirty=$dirty`nplatform=windows-x64`nruntime=static-msvc`nchannel=preview`n"
[IO.File]::WriteAllText((Join-Path $packagePath 'VERSION.txt'),$manifest,[Text.UTF8Encoding]::new($false))
$checksums = Get-ChildItem -LiteralPath $packagePath -File | Sort-Object Name | ForEach-Object {
    $digest = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    "$($digest.Hash.ToLowerInvariant())  $($_.Name)"
}
[IO.File]::WriteAllLines((Join-Path $packagePath 'SHA256SUMS.txt'),[string[]]$checksums,[Text.UTF8Encoding]::new($false))
$archive = Join-Path $outputPath "$name.zip"
Compress-Archive -LiteralPath $packagePath -DestinationPath $archive -Force
$archiveHash = Get-FileHash -LiteralPath $archive -Algorithm SHA256
[IO.File]::WriteAllText("$archive.sha256","$($archiveHash.Hash.ToLowerInvariant())  $name.zip`n",[Text.UTF8Encoding]::new($false))
Write-Output $archive
