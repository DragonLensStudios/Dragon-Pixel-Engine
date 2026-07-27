[CmdletBinding()]
param(
    [switch]$AddressSanitizer,
    [switch]$SkipImageBuild,
    [switch]$VerboseTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$imageName = 'dragonpixel-slice1-ubuntu:24.04'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker is required to run the Ubuntu 24.04 Slice 1 matrix locally.'
}

if (-not $SkipImageBuild) {
    & docker build --file (Join-Path $PSScriptRoot 'Dockerfile.ubuntu24') --tag $imageName $repositoryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Ubuntu toolchain image build failed with exit code $LASTEXITCODE."
    }
}

$configurePreset = if ($AddressSanitizer) { 'unix-clang-asan' } else { 'unix-clang' }
$buildPreset = if ($AddressSanitizer) { 'unix-asan' } else { 'unix-release' }
$testVerbosity = if ($VerboseTests) { '--verbose' } else { '--output-on-failure' }
$containerScript = @"
set -euo pipefail
cmake --preset $configurePreset
cmake --build --preset $buildPreset
xvfb-run -a ctest --preset $buildPreset $testVerbosity
"@

& docker run --rm --init `
    --workdir /workspace `
    $imageName `
    bash -lc $containerScript

if ($LASTEXITCODE -ne 0) {
    throw "Ubuntu Slice 1 verification failed with exit code $LASTEXITCODE."
}
