#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$QtVersion = '6.11.1',
    [string]$QtRoot = '',
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-LastExitCode {
    param([Parameter(Mandatory)][string]$Operation)

    if ($LASTEXITCODE -ne 0) {
        throw "$Operation failed with exit code $LASTEXITCODE."
    }
}

function Import-VisualStudioEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }

    $installPath = & $vswhere -latest -products Microsoft.VisualStudio.Product.BuildTools -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ASAN -property installationPath
    Assert-LastExitCode 'Visual Studio 2022 Build Tools discovery'
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw 'Visual Studio 2022 Build Tools with MSVC v143 and AddressSanitizer were not found. Run Install-WindowsPrerequisites.ps1 first.'
    }

    $vsDevCmd = Join-Path $installPath.Trim() 'Common7\Tools\VsDevCmd.bat'
    $command = "`"$vsDevCmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"
    $environmentLines = & $env:ComSpec /d /s /c $command
    Assert-LastExitCode 'Visual Studio developer environment initialization'

    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }

    return $installPath.Trim()
}

function Assert-Command {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "Required command '$Name' is not available."
    }
    return $command.Source
}

$visualStudioPath = Import-VisualStudioEnvironment
$cmakeBin = Join-Path $env:ProgramFiles 'CMake\bin'
$wingetLinks = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
$env:Path = "$cmakeBin;$wingetLinks;$env:Path"

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT)) {
        $QtRoot = $env:QT_ROOT
    }
    else {
        $QtRoot = "C:\Qt\$QtVersion\msvc2022_64"
    }
}
$QtRoot = [IO.Path]::GetFullPath($QtRoot)

$cl = Assert-Command cl
$cmake = Assert-Command cmake
$ctest = Assert-Command ctest
$ninja = Assert-Command ninja
$dotnet = Assert-Command dotnet
$vcpkg = Join-Path $visualStudioPath 'VC\vcpkg\vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkg)) {
    throw "The Visual Studio vcpkg component was not found at $vcpkg."
}
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
if (-not (Test-Path -LiteralPath $qmake)) {
    throw "Qt $QtVersion MSVC 2022 x64 was not found at $QtRoot."
}

$qtVersionActual = (& $qmake -query QT_VERSION).Trim()
Assert-LastExitCode 'Qt version query'
if ($qtVersionActual -ne $QtVersion) {
    throw "Expected Qt $QtVersion but found $qtVersionActual at $QtRoot."
}

$tempBase = Join-Path ([IO.Path]::GetTempPath()) 'DragonPixelEngine\ToolchainSmoke'
$smokeRoot = Join-Path $tempBase ([guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $smokeRoot 'source'
$buildRoot = Join-Path $smokeRoot 'build'
$managedRoot = Join-Path $smokeRoot 'managed'
New-Item -ItemType Directory -Force -Path $sourceRoot, $managedRoot | Out-Null

$cmakeProject = @'
cmake_minimum_required(VERSION 3.25)
project(DpeWindowsToolchainSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Widgets)

add_executable(dpe_toolchain_smoke main.cpp)
target_link_libraries(dpe_toolchain_smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

add_executable(dpe_asan_smoke asan.cpp)
target_compile_options(dpe_asan_smoke PRIVATE /fsanitize=address /Zi)

enable_testing()
add_test(NAME dpe_toolchain_smoke COMMAND dpe_toolchain_smoke)
add_test(NAME dpe_asan_smoke COMMAND dpe_asan_smoke)
'@

$cppSource = @'
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QString>
#include <QtGlobal>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QMainWindow window;
    QDockWidget dock(QStringLiteral("Environment Smoke"), &window);
    window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
    return QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1") ? 0 : 1;
}
'@

$asanSource = @'
#include <memory>

int main() {
    auto value = std::make_unique<int>(42);
    return *value == 42 ? 0 : 1;
}
'@

$managedProject = @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
</Project>
'@

$managedSource = @'
Console.WriteLine($"Dragon Pixel Engine .NET smoke: {Environment.Version}");
'@

$utf8NoBom = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $sourceRoot 'CMakeLists.txt'), $cmakeProject, $utf8NoBom)
[IO.File]::WriteAllText((Join-Path $sourceRoot 'main.cpp'), $cppSource, $utf8NoBom)
[IO.File]::WriteAllText((Join-Path $sourceRoot 'asan.cpp'), $asanSource, $utf8NoBom)
[IO.File]::WriteAllText((Join-Path $managedRoot 'DpeManagedSmoke.csproj'), $managedProject, $utf8NoBom)
[IO.File]::WriteAllText((Join-Path $managedRoot 'Program.cs'), $managedSource, $utf8NoBom)

$succeeded = $false
try {
    & $cmake -S $sourceRoot -B $buildRoot -G 'Ninja Multi-Config' "-DCMAKE_PREFIX_PATH=$QtRoot"
    Assert-LastExitCode 'CMake configure'

    & $cmake --build $buildRoot --config Release
    Assert-LastExitCode 'C++/Qt build'

    $env:Path = "$(Join-Path $QtRoot 'bin');$env:Path"
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QT_QPA_PLATFORM = 'offscreen'
    & $ctest --test-dir $buildRoot -C Release --output-on-failure
    Assert-LastExitCode 'C++/Qt tests'

    $nativeExecutable = Join-Path $buildRoot 'Release\dpe_toolchain_smoke.exe'
    $dependencies = & dumpbin /dependents $nativeExecutable
    Assert-LastExitCode 'Qt linkage inspection'
    if (($dependencies -join "`n") -notmatch 'Qt6Widgets\.dll') {
        throw 'The Qt smoke executable did not link dynamically to Qt6Widgets.dll.'
    }

    & $dotnet build (Join-Path $managedRoot 'DpeManagedSmoke.csproj') --configuration Release --nologo
    Assert-LastExitCode '.NET 10 build'

    $succeeded = $true
    [pscustomobject]@{
        Result = 'PASS'
        VisualStudio = $visualStudioPath
        MSVC = $env:VCToolsVersion
        CMake = (& $cmake --version | Select-Object -First 1)
        Ninja = (& $ninja --version)
        DotNetSdk = (& $dotnet --version)
        Vcpkg = (& $vcpkg version | Select-Object -First 1)
        Qt = $qtVersionActual
        QtRoot = $QtRoot
    } | Format-List
}
finally {
    if ($KeepArtifacts -or -not $succeeded) {
        Write-Host "Smoke artifacts retained at $smokeRoot"
    }
    else {
        $resolvedSmoke = [IO.Path]::GetFullPath($smokeRoot)
        $resolvedBase = [IO.Path]::GetFullPath($tempBase) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedSmoke.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected smoke path: $resolvedSmoke"
        }
        Remove-Item -LiteralPath $resolvedSmoke -Recurse -Force
    }
}
