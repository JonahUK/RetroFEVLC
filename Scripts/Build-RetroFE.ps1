# RetroFE Search Dev: configure, build and package Windows x64 Release.
# Run from PowerShell: & ".\Scripts\Build-RetroFE.ps1"
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "RetroFE"
$source = Join-Path $project "Source"
$build = Join-Path $project "Build-Search-x64"
$package = Join-Path $PSScriptRoot "Package.py"

if (-not (Test-Path $source)) { throw "Source directory not found: $source" }
if (-not (Test-Path $package)) { throw "Packaging script not found: $package" }
foreach ($command in @("cmake", "python")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command not found on PATH: $command"
    }
}

Write-Host "`n[1/3] Configure CMake (Visual Studio 2022, x64)..."
& cmake -S $source -B $build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }

Write-Host "`n[2/3] Build Release..."
& cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw "Release build failed (exit $LASTEXITCODE)." }

Write-Host "`n[3/3] Package Windows runtime with LibVLC..."
Push-Location $root
try {
    & python $package --os windows --build full --vlc-x64 --clean --build-dir $build
    if ($LASTEXITCODE -ne 0) { throw "Packaging failed (exit $LASTEXITCODE)." }
}
finally {
    Pop-Location
}

Write-Host "`nSUCCESS: Configure, build and packaging completed."
Write-Host "Runtime: $(Join-Path $root 'Artifacts\windows\RetroFE')"

