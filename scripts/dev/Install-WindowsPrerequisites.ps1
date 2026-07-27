#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$QtVersion = '6.11.1',
    [string]$QtRoot = 'C:\Qt',
    [string]$AqtInstallRef = '8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b',
    [switch]$SkipVisualStudio,
    [switch]$SkipBuildTools,
    [switch]$SkipQt
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-LastExitCode {
    param([Parameter(Mandatory)][string]$Operation)

    if ($LASTEXITCODE -ne 0) {
        throw "$Operation failed with exit code $LASTEXITCODE."
    }
}

function Get-VisualStudioInstallation {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }

    $installPath = & $vswhere -latest -products * -property installationPath
    Assert-LastExitCode 'Visual Studio discovery'
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw 'No Visual Studio installation was found.'
    }

    return $installPath.Trim()
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell session so Visual Studio components can be installed.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsConfig = Join-Path $repositoryRoot '.vsconfig'

if (-not $SkipVisualStudio) {
    $installPath = Get-VisualStudioInstallation
    $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    if (-not (Test-Path -LiteralPath $setup)) {
        throw 'Visual Studio setup.exe was not found.'
    }

    Write-Host "Installing Visual Studio C++ components into $installPath ..."
    $arguments = "modify --installPath `"$installPath`" --config `"$vsConfig`" --passive --norestart"
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) {
        throw "Visual Studio Installer failed with exit code $($process.ExitCode)."
    }
    if ($process.ExitCode -eq 3010) {
        Write-Warning 'Visual Studio components were installed, but Windows requested a restart.'
    }
}

if (-not $SkipBuildTools) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $buildToolsPath = & $vswhere -latest -products Microsoft.VisualStudio.Product.BuildTools -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ASAN -property installationPath
    Assert-LastExitCode 'Visual Studio 2022 Build Tools discovery'
    if ([string]::IsNullOrWhiteSpace($buildToolsPath)) {
        Write-Host 'Installing Visual Studio 2022 Build Tools for the Qt-supported compiler and v143 x64 AddressSanitizer runtime ...'
        $buildToolsArguments = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
        winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity --override $buildToolsArguments
        Assert-LastExitCode 'Visual Studio 2022 Build Tools installation'
    }
}

Write-Host 'Installing CMake 4.4.0 or newer ...'
winget install --id Kitware.CMake --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
if ($LASTEXITCODE -notin 0, -1978335189) {
    Assert-LastExitCode 'CMake installation'
}

Write-Host 'Installing Ninja 1.13.2 or newer ...'
winget install --id Ninja-build.Ninja --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
if ($LASTEXITCODE -notin 0, -1978335189) {
    Assert-LastExitCode 'Ninja installation'
}

$cmakeBin = Join-Path $env:ProgramFiles 'CMake\bin'
$wingetLinks = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
$env:Path = "$cmakeBin;$wingetLinks;$env:Path"

if (-not $SkipQt) {
    $qtInstallRoot = Join-Path (Join-Path $QtRoot $QtVersion) 'msvc2022_64'
    $qmake = Join-Path $qtInstallRoot 'bin\qmake.exe'
    if (-not (Test-Path -LiteralPath $qmake)) {
        $uv = Get-Command uv -ErrorAction SilentlyContinue
        if ($null -eq $uv) {
            throw '`uv` is required for the isolated aqtinstall invocation but was not found.'
        }

        Write-Host "Installing Qt $QtVersion MSVC 2022 x64 into $QtRoot ..."
        $aqtSource = "git+https://github.com/miurahr/aqtinstall.git@$AqtInstallRef"
        $aqtWorkingDirectory = Join-Path ([IO.Path]::GetTempPath()) 'DragonPixelEngine\aqtinstall'
        New-Item -ItemType Directory -Force -Path $aqtWorkingDirectory | Out-Null
        Push-Location $aqtWorkingDirectory
        try {
            & $uv.Source tool run --from $aqtSource aqt install-qt windows desktop $QtVersion win64_msvc2022_64 --outputdir $QtRoot
            Assert-LastExitCode 'Qt installation'
        }
        finally {
            Pop-Location
        }
    }

    if (-not (Test-Path -LiteralPath $qmake)) {
        throw "Qt installation did not create $qmake."
    }

    [Environment]::SetEnvironmentVariable('QT_ROOT', $qtInstallRoot, 'User')
    $env:QT_ROOT = $qtInstallRoot
}

Write-Host 'Prerequisite installation finished. Open a new PowerShell session, then run scripts\dev\Test-WindowsEnvironment.ps1.'
