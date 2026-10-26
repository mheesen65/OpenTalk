#requires -Version 5.1
<#
.SYNOPSIS
    Builds OpenTalk and produces a portable, no-install ZIP.

.DESCRIPTION
    Configures a Release build, compiles the client and server, stages them with
    CMake's install rules, copies the Qt runtime next to the executables with
    windeployqt, and zips the result into dist/.

    The archive is self-contained: no installer, no registry writes, no
    administrator rights, no service registration. Extract anywhere and run
    OpenTalkClient.exe.

.PARAMETER QtDir
    Qt kit directory, e.g. C:\Qt\6.7.3\msvc2019_64. Defaults to CMAKE_PREFIX_PATH
    or the active Qt installation on PATH.

.PARAMETER BuildDir
    Build tree to use. Default: <repo>\build.
#>
[CmdletBinding()]
param(
    [string]$QtDir = $env:CMAKE_PREFIX_PATH,
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }
$distDir = Join-Path $repoRoot 'dist'
$stageDir = Join-Path $distDir 'stage'

# Resolve the Qt kit, since windeployqt must come from the same kit that linked
# the binaries.
if (-not $QtDir) {
    $qmake = Get-Command qmake6 -ErrorAction SilentlyContinue
    if (-not $qmake) { $qmake = Get-Command qmake -ErrorAction SilentlyContinue }
    if ($qmake) { $QtDir = Split-Path -Parent (Split-Path -Parent $qmake.Source) }
}
if (-not $QtDir -or -not (Test-Path $QtDir)) {
    throw "Qt kit not found. Pass -QtDir <path to Qt msvc kit>, e.g. C:\Qt\6.7.3\msvc2019_64"
}

$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found under $QtDir\bin"
}

Write-Host "==> Configuring in $BuildDir"
cmake -S $repoRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH="$QtDir" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

Write-Host "==> Building Release"
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

Write-Host "==> Running tests"
ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host "==> Staging to $stageDir"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
cmake --install $BuildDir --config Release --prefix $stageDir
if ($LASTEXITCODE -ne 0) { throw "Install failed ($LASTEXITCODE)" }

$binDir = Join-Path $stageDir 'bin'
Write-Host "==> Deploying Qt runtime into $binDir"
& $windeployqt --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --compiler-runtime `
    (Join-Path $binDir 'OpenTalkClient.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# The server shares the Qt Core/Network DLLs already copied for the client.
& $windeployqt --release --no-translations --no-opengl-sw `
    (Join-Path $binDir 'OpenTalkServer.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for server ($LASTEXITCODE)" }

# Flatten bin/ to the archive root so users see the executables immediately.
$portableDir = Join-Path $distDir 'portable'
if (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
New-Item -ItemType Directory -Path $portableDir | Out-Null
Copy-Item (Join-Path $binDir '*') $portableDir -Recurse
Copy-Item (Join-Path $repoRoot 'LICENSE') $portableDir
Copy-Item (Join-Path $repoRoot 'README.md') $portableDir

# Opus is a direct dependency of the SDK, so its DLL has to travel with the
# binaries. Look where vcpkg and the Qt kit put it.
$opusCandidates = @(
    (Join-Path $QtDir 'bin/opus.dll'),
    $env:OPUS_DLL,
    (Join-Path $env:VCPKG_ROOT 'installed/x64-windows/bin/opus.dll'),
    (Join-Path $env:VCPKG_ROOT 'installed/x64-windows-release/bin/opus.dll'),
    (Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed/x64-windows/bin/opus.dll'),
    (Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed/x64-windows-release/bin/opus.dll')
) | Where-Object { $_ -and (Test-Path $_) }

if ($opusCandidates.Count -gt 0) {
    Copy-Item $opusCandidates[0] $portableDir
    Write-Host "==> Bundled $($opusCandidates[0])"
} else {
    Write-Warning "opus.dll was not found. Set OPUS_DLL or install opus via vcpkg; the packaged client will not start without it."
}

$version = '1.0.0'
$zipPath = Join-Path $distDir "OpenTalk-$version-portable.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Write-Host "==> Creating $zipPath"
Compress-Archive -Path (Join-Path $portableDir '*') -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host ""
Write-Host "Portable build ready: $zipPath"
Write-Host "Extract anywhere and run OpenTalkClient.exe (no admin rights required)."
