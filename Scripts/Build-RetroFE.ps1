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

# Build native WebP decoding libraries before configuring RetroFE.
$webpSource = Join-Path $project "ThirdParty\libwebp-1.6.0"
$webpBuild = Join-Path $project "ThirdParty\libwebp-1.6.0-build-x64"

if (-not (Test-Path (Join-Path $webpSource "CMakeLists.txt"))) {
    throw "libwebp 1.6.0 source missing: $webpSource"
}

Write-Host "`n[1/4] Configure and build libwebp 1.6.0 (Windows x64)..."

& cmake -S $webpSource -B $webpBuild `
    -G "Visual Studio 17 2022" -A x64 `
    -DBUILD_SHARED_LIBS=OFF `
    -DWEBP_BUILD_ANIM_UTILS=OFF `
    -DWEBP_BUILD_CWEBP=OFF `
    -DWEBP_BUILD_DWEBP=OFF `
    -DWEBP_BUILD_GIF2WEBP=OFF `
    -DWEBP_BUILD_IMG2WEBP=OFF `
    -DWEBP_BUILD_VWEBP=OFF `
    -DWEBP_BUILD_WEBPINFO=OFF `
    -DWEBP_BUILD_WEBPMUX=OFF `
    -DWEBP_BUILD_EXTRAS=OFF `
    -DWEBP_ENABLE_SIMD=ON

if ($LASTEXITCODE -ne 0) {
    throw "libwebp configuration failed (exit $LASTEXITCODE)."
}

& cmake --build $webpBuild --config Release `
    --target webp webpdemux --parallel 4

if ($LASTEXITCODE -ne 0) {
    throw "libwebp build failed (exit $LASTEXITCODE)."
}

foreach ($library in @("libwebp.lib", "libwebpdemux.lib")) {
    $path = Join-Path (Join-Path $webpBuild "Release") $library
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required libwebp library missing: $path"
    }
}

Write-Host "libwebp libraries verified." -ForegroundColor Green

# Configure and build patched libpng with native APNG support.
$pngSource = Join-Path $project "ThirdParty\libpng-1.6.59"
$pngBuild = Join-Path $project "ThirdParty\libpng-1.6.59-build-x64"
$zlibInclude = Join-Path $project "ThirdParty\zlib-x64\include"
$zlibLibrary = Join-Path $project "ThirdParty\zlib-x64\lib\zlib.lib"

foreach ($required in @(
    (Join-Path $pngSource "CMakeLists.txt"),
    (Join-Path $pngSource "png.h"),
    $zlibLibrary,
    (Join-Path $zlibInclude "zlib.h")
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required APNG build dependency missing: $required"
    }
}

$pngHeader = [System.IO.File]::ReadAllText(
    (Join-Path $pngSource "png.h")
)

if (-not $pngHeader.Contains("PNG_READ_APNG_SUPPORTED")) {
    throw "libpng source does not contain the required APNG patch."
}

Write-Host "`n[APNG] Configure and build patched libpng 1.6.59 (Windows x64)..."

& cmake -S $pngSource -B $pngBuild `
    -G "Visual Studio 17 2022" -A x64 `
    -DPNG_SHARED=OFF `
    -DPNG_STATIC=ON `
    -DPNG_TESTS=OFF `
    -DPNG_TOOLS=OFF `
    -DPNG_EXECUTABLES=ON `
    -DPNG_BUILD_ZLIB=OFF `
    "-DZLIB_INCLUDE_DIR=$zlibInclude" `
    "-DZLIB_LIBRARY_RELEASE=$zlibLibrary"

if ($LASTEXITCODE -ne 0) {
    throw "Patched libpng configuration failed (exit $LASTEXITCODE)."
}

& cmake --build $pngBuild --config Release `
    --target png_static --parallel 4

if ($LASTEXITCODE -ne 0) {
    throw "Patched libpng build failed (exit $LASTEXITCODE)."
}

$pngLibrary = Join-Path $pngBuild "Release\libpng16_static.lib"

if (-not (Test-Path -LiteralPath $pngLibrary -PathType Leaf)) {
    throw "Required patched libpng library missing: $pngLibrary"
}

Write-Host "Patched APNG-enabled libpng library verified." -ForegroundColor Green

Write-Host "`n[2/4] Configure CMake (Visual Studio 2022, x64)..."
& cmake -S $source -B $build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }

Write-Host "`n[3/4] Build Release..."
& cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw "Release build failed (exit $LASTEXITCODE)." }

Write-Host "`n[4/4] Package Windows runtime with LibVLC..."
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

