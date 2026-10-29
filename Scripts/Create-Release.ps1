# RetroFEVLC - Official Windows x64 release packaging
# Uses version-specific staging. Never modifies the development runtime.

param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v\d+\.\d+\.\d+(?:-(?:Beta|Stable|RC\d+))?$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root 'Artifacts\windows\RetroFE'
$common = Join-Path $root 'Package\Environment\Common'
$windows = Join-Path $root 'Package\Environment\Windows'

$defaultTheme = Join-Path $common 'search_themes\default.conf'
$officialSettings = Join-Path $common 'settings.conf'
$sampleRom = Join-Path $common 'collections\Sega Genesis\roms\Zero Tolerance (USA, Europe).zip'

Write-Host '===== RETROFEVLC RELEASE PRE-FLIGHT ====='

foreach ($path in @($runtime, $common, $windows, $defaultTheme, $officialSettings, $sampleRom)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required release source missing: $path"
    }
}

$themes = @(Get-ChildItem -LiteralPath (Join-Path $common 'search_themes') -File)

if ($themes.Count -ne 1 -or $themes[0].Name -ne 'default.conf') {
    throw 'Official theme directory must contain only default.conf.'
}

$settingsText = [System.IO.File]::ReadAllText($officialSettings)

if ($settingsText -match '(?m)^\s*searchTheme\s*=\s*core\s*$') {
    throw 'Official settings must not activate the CORE theme.'
}

Write-Host 'Official default theme: verified'
Write-Host 'Official settings: verified'
Write-Host 'Original sample ROM: verified'
Write-Host 'Development runtime: found'
Write-Host ''
Write-Host 'SUCCESS: Release pre-flight checks passed.'
Write-Host 'No release files have been created or modified.'

Write-Host ''
Write-Host '===== RELEASE STAGING ====='

$releaseRoot = 'F:\RetroFEVLC'
$stagingRoot = Join-Path $releaseRoot "Release-Staging-$Version"
$staging = Join-Path $stagingRoot 'RetroFE'
$zipPath = Join-Path $releaseRoot "RetroFEVLC-Windows-x64-$Version.zip"

if (Test-Path -LiteralPath $zipPath) {
    throw "Release ZIP already exists. Refusing to overwrite: $zipPath"
}

if (Test-Path -LiteralPath $stagingRoot) {
    throw "Staging directory already exists. Refusing to overwrite: $stagingRoot"
}

New-Item -ItemType Directory -Path $staging -Force -ErrorAction Stop | Out-Null

# Copy official package source, not development configuration.
Get-ChildItem -LiteralPath $common -Force |
    Where-Object { $_.Name -ne 'core' } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $staging `
            -Recurse -Force -ErrorAction Stop
    }

# Add Windows-specific package files without copying development settings.
# Copy Windows-specific files, excluding the obsolete GStreamer core.
Get-ChildItem -LiteralPath $windows -Force |
    Where-Object { $_.Name -ne 'core' } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $staging `
            -Recurse -Force -ErrorAction Stop
    }

# Verify the official theme and configuration.
$stagedDefault = Join-Path $staging 'search_themes\default.conf'
$stagedCore = Join-Path $staging 'search_themes\core.conf'
$stagedSettings = Join-Path $staging 'settings.conf'

if (-not (Test-Path -LiteralPath $stagedDefault)) {
    throw 'Official default theme missing from staging.'
}

if (Test-Path -LiteralPath $stagedCore) {
    throw 'Custom CORE theme found in staging.'
}

if ((Get-FileHash $stagedSettings -Algorithm SHA256).Hash -ne
    (Get-FileHash $officialSettings -Algorithm SHA256).Hash) {
    throw 'Staged settings do not match official template.'
}

Write-Host "Staging directory: $staging"
Write-Host 'Default theme: included'
Write-Host 'CORE theme: excluded'
Write-Host 'Official settings: verified'
Write-Host ''
Write-Host 'SUCCESS: Initial release staging completed.'

Write-Host ''
Write-Host '===== COMPILED WINDOWS RUNTIME ====='

$runtimeCore = Join-Path $runtime 'core'
$stagedCore = Join-Path $staging 'core'

if (-not (Test-Path -LiteralPath (Join-Path $runtimeCore 'retrofe.exe'))) {
    throw 'Compiled RetroFE executable missing.'
}

# The compiled runtime is the sole source for the release core.
# Refuse to merge into an existing core directory.
if (Test-Path -LiteralPath $stagedCore) {
    throw "Staged core already exists; refusing to merge: $stagedCore"
}

Copy-Item -LiteralPath $runtimeCore -Destination $stagedCore `
    -Recurse -ErrorAction Stop

$sourceDlls = @(
    Get-ChildItem -LiteralPath $runtimeCore -Recurse -Filter '*.dll' -File
)

$stagedDlls = @(
    Get-ChildItem -LiteralPath $stagedCore -Recurse -Filter '*.dll' -File
)

if ($sourceDlls.Count -ne $stagedDlls.Count) {
    throw "Runtime DLL count mismatch: source=$($sourceDlls.Count), staged=$($stagedDlls.Count)"
}

Write-Host "Runtime DLLs verified: $($stagedDlls.Count)"

foreach ($name in @('retrofe.exe', 'libvlc.dll', 'libvlccore.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stagedCore $name))) {
        throw "Required runtime component missing: $name"
    }
}

Write-Host 'RetroFE executable: included'
Write-Host 'LibVLC runtime: included'
Write-Host 'VLC plugin directory: included'
Write-Host 'SUCCESS: Windows runtime components staged.'

Write-Host ''
Write-Host '===== FINAL RELEASE VALIDATION ====='

$requiredFiles = @(
    'core\retrofe.exe',
    'core\libvlc.dll',
    'core\libvlccore.dll',
    'core\OpenSans.ttf',
    'settings.conf',
    'controls.conf',
    'search_themes\default.conf',
    'collections\Sega Genesis\roms\Zero Tolerance (USA, Europe).zip'
)

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $staging $relativePath

    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required release file missing: $relativePath"
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $staging 'layouts\Aeon Nox') -PathType Container)) {
    throw 'Aeon Nox layout missing.'
}

if (-not (Test-Path -LiteralPath (Join-Path $stagedCore 'plugins') -PathType Container)) {
    throw 'VLC plugin directory missing.'
}

$stagedThemes = @(
    Get-ChildItem -LiteralPath (Join-Path $staging 'search_themes') -File
)

if ($stagedThemes.Count -ne 1 -or $stagedThemes[0].Name -ne 'default.conf') {
    throw 'Official release must contain only default.conf.'
}

foreach ($name in @('log.txt', 'meta.db')) {
    if (Test-Path -LiteralPath (Join-Path $staging $name)) {
        throw "Development file found in release: $name"
    }
}

$stagedSettingsHash = (Get-FileHash -LiteralPath (Join-Path $staging 'settings.conf') -Algorithm SHA256).Hash
$officialSettingsHash = (Get-FileHash -LiteralPath $officialSettings -Algorithm SHA256).Hash

if ($stagedSettingsHash -ne $officialSettingsHash) {
    throw 'Release settings differ from official template.'
}

$sourceRelativeDlls = @(
    Get-ChildItem -LiteralPath $runtimeCore -Recurse -Filter '*.dll' -File |
        ForEach-Object { $_.FullName.Substring($runtimeCore.Length + 1) }
)

$stagedRelativeDlls = @(
    Get-ChildItem -LiteralPath $stagedCore -Recurse -Filter '*.dll' -File |
        ForEach-Object { $_.FullName.Substring($stagedCore.Length + 1) }
)

$dllDifferences = @(
    Compare-Object -ReferenceObject $sourceRelativeDlls -DifferenceObject $stagedRelativeDlls
)

if ($dllDifferences.Count -ne 0) {
    throw 'Staged DLL paths do not match compiled runtime.'
}

$legacyDlls = @(
    Get-ChildItem -LiteralPath $stagedCore -Recurse -File |
        Where-Object { $_.Name -like 'gst*.dll' }
)

if ($legacyDlls.Count -ne 0) {
    throw 'Legacy GStreamer DLLs detected in release.'
}

Write-Host 'Required files: verified'
Write-Host 'Aeon Nox: verified'
Write-Host 'Official settings: verified'
Write-Host 'Default theme only: verified'
Write-Host 'Development files: excluded'
Write-Host "Runtime DLL paths: $($stagedRelativeDlls.Count) verified"
Write-Host 'Legacy GStreamer DLLs: none'
Write-Host ''
Write-Host 'PASS: Final release validation successful.'
Write-Host 'Release staging is ready for ZIP packaging.'
Write-Host ''
Write-Host '===== CREATE RELEASE ZIP ====='

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# CreateNew prevents overwriting any existing archive.
$stream = [System.IO.File]::Open(
    $zipPath,
    [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::None
)

try {
    $archive = New-Object System.IO.Compression.ZipArchive(
        $stream,
        [System.IO.Compression.ZipArchiveMode]::Create,
        $false
    )

    try {
        $files = @(Get-ChildItem -LiteralPath $staging -Recurse -File)

        foreach ($file in $files) {
            $relative = $file.FullName.Substring($staging.Length + 1)
            $entryName = 'RetroFE/' + $relative.Replace('\', '/')

            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive,
                $file.FullName,
                $entryName,
                [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    }
    finally {
        $archive.Dispose()
    }
}
finally {
    $stream.Dispose()
}

Write-Host "ZIP created: $zipPath"

Write-Host ''
Write-Host '===== VERIFY RELEASE ZIP ====='

$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)

try {
    $entryNames = @($zip.Entries | ForEach-Object { $_.FullName })

    foreach ($required in @(
        'RetroFE/core/retrofe.exe',
        'RetroFE/core/libvlc.dll',
        'RetroFE/core/libvlccore.dll',
        'RetroFE/settings.conf',
        'RetroFE/search_themes/default.conf'
    )) {
        if ($entryNames -cnotcontains $required) {
            throw "Required ZIP entry missing: $required"
        }
    }

    if ($entryNames -contains 'RetroFE/search_themes/core.conf') {
        throw 'Custom CORE theme must not be in official release ZIP.'
    }

    Write-Host "ZIP entries: $($entryNames.Count)"
    Write-Host 'Required ZIP contents: verified'
}
finally {
    $zip.Dispose()
}

Write-Host ''
Write-Host 'PASS: Official RetroFEVLC release ZIP created and verified.'
Write-Host 'Existing releases have not been modified.'