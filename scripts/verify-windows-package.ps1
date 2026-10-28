param(
    [string]$OutputDir = 'dist'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$project = Get-Content -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -Raw
if ($project -notmatch 'project\(TriggerLab VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)') {
    throw 'Cannot read project version'
}
$expectedVersion = $Matches[1]
$packageName = "TriggerLab-$expectedVersion-windows-x64"
$outputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
$repoPrefix = $repoRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (!$outputPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package directory must be inside the repository'
}
$archive = Join-Path $outputPath "$packageName.zip"
$checksum = (Get-Content -LiteralPath "$archive.sha256" -Raw).Trim()
if ($checksum -notmatch '^([a-fA-F0-9]{64})  ([A-Za-z0-9.-]+\.zip)$') {
    throw 'Invalid archive checksum file'
}
$archiveHash = $Matches[1]
if ($Matches[2] -cne "$packageName.zip" -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $archiveHash) {
    throw 'Archive SHA256 does not match'
}

function Test-ApplicationWindow([string]$Executable) {
    $appProcess = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path $Executable) -PassThru
    try {
        if (!$appProcess.WaitForInputIdle(10000)) {
            throw "Window initialization timed out: $Executable"
        }
        Start-Sleep -Seconds 2
        $appProcess.Refresh()
        if ($appProcess.HasExited -or $appProcess.MainWindowHandle -eq 0) {
            throw "Application exited or has no main window: $Executable"
        }
        if (!$appProcess.CloseMainWindow() -or !$appProcess.WaitForExit(5000)) {
            throw "Application did not close normally: $Executable"
        }
        if ($appProcess.ExitCode -ne 0) {
            throw "Application exited with code $($appProcess.ExitCode): $Executable"
        }
        Write-Host "PASS: extracted $([IO.Path]::GetFileName($Executable)) opened and closed normally."
    } finally {
        if (!$appProcess.HasExited) { Stop-Process -Id $appProcess.Id -Force }
        $appProcess.Dispose()
    }
}

$verificationRoot = Join-Path $outputPath ('verification-' + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $archive -DestinationPath $verificationRoot
    $roots = @(Get-ChildItem -LiteralPath $verificationRoot -Force)
    if ($roots.Count -ne 1 -or !$roots[0].PSIsContainer -or $roots[0].Name -cne $packageName) {
        throw 'Unexpected archive root'
    }
    $packagePath = $roots[0].FullName
    $expectedFiles = @('TriggerLab.exe', 'TriggerLabTrainer.exe', 'LICENSE.txt', 'README.txt',
                      'CHANGELOG.md', 'VERSION.txt', 'SHA256SUMS.txt')
    $entries = @(Get-ChildItem -LiteralPath $packagePath -Force)
    if ($entries.Count -ne $expectedFiles.Count -or
        @($entries | Where-Object { $_.PSIsContainer -or $_.Name -cnotin $expectedFiles }).Count) {
        throw 'Unexpected package contents'
    }
    $payload = @($expectedFiles | Where-Object { $_ -ne 'SHA256SUMS.txt' })
    $seen = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $packagePath 'SHA256SUMS.txt')) {
        if ($line -notmatch '^([a-fA-F0-9]{64})  ([^\\/\r\n]+)$') {
            throw 'Invalid payload checksum entry'
        }
        $expectedHash = $Matches[1]
        $name = $Matches[2]
        if ($name -cnotin $payload -or $seen.ContainsKey($name)) {
            throw "Unexpected or duplicate checksum entry: $name"
        }
        $seen[$name] = $true
        if ((Get-FileHash -LiteralPath (Join-Path $packagePath $name) -Algorithm SHA256).Hash -ine $expectedHash) {
            throw "Payload SHA256 does not match: $name"
        }
    }
    if ($seen.Count -ne $payload.Count) { throw 'Incomplete payload checksums' }

    $manifest = Get-Content -LiteralPath (Join-Path $packagePath 'VERSION.txt') -Raw | ConvertFrom-StringData
    $revision = & git -C $repoRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git revision' }
    if ($manifest.version -cne $expectedVersion -or $manifest.revision -cne $revision -or
        $manifest.source_dirty -ine 'False' -or $manifest.platform -cne 'windows-x64' -or
        $manifest.runtime -cne 'static-msvc' -or $manifest.channel -cne 'stable') {
        throw 'Package manifest does not match the tested source'
    }
    $readme = Get-Content -LiteralPath (Join-Path $packagePath 'README.txt') -Raw
    if (!$readme.Contains("TriggerLab $expectedVersion") -or $readme.Contains('@VERSION@')) {
        throw 'Quickstart version was not substituted'
    }
    Write-Host "PASS: archive SHA256, payload checksums, contents and manifest ($expectedVersion, $revision)."
    foreach ($name in @('TriggerLab.exe', 'TriggerLabTrainer.exe')) {
        Test-ApplicationWindow (Join-Path $packagePath $name)
    }
} finally {
    if (Test-Path -LiteralPath $verificationRoot) {
        Remove-Item -LiteralPath $verificationRoot -Recurse -Force
    }
}
