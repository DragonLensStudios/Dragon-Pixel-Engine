[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$AddressSanitizer,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$visualStudioPath = & $vswhere -latest -products * -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioPath) {
    $visualStudioPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $visualStudioPath) {
    throw 'A Visual Studio installation with MSVC was not found. Run Install-WindowsPrerequisites.ps1.'
}

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64

$qtRoot = 'C:\Qt\6.11.1\msvc2022_64'
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin\qmake.exe'))) {
    throw "Qt 6.11.1 was not found at $qtRoot. Run Install-WindowsPrerequisites.ps1."
}

$configurePreset = if ($AddressSanitizer) { 'windows-msvc-asan' } else { 'windows-msvc' }
$buildPreset = if ($AddressSanitizer) {
    'windows-asan'
} elseif ($Configuration -eq 'Debug') {
    'windows-debug'
} else {
    'windows-release'
}
$testPreset = if ($AddressSanitizer) {
    'windows-asan'
} elseif ($Configuration -eq 'Debug') {
    'windows-debug'
} else {
    'windows-release'
}

Push-Location $repositoryRoot
try {
    cmake --preset $configurePreset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    cmake --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    if (-not $SkipTests) {
        ctest --preset $testPreset --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    }
} finally {
    Pop-Location
}

Write-Host "Dragon Pixel Engine $buildPreset build completed successfully." -ForegroundColor Green
