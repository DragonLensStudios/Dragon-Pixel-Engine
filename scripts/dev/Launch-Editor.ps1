[CmdletBinding()]
param([switch]$SkipBuild)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build-Windows.ps1') -Configuration Release
}

$editor = Join-Path $repositoryRoot 'out\build\windows-msvc-vcpkg\src\editor\Release\DragonPixelEditor.exe'
if (-not (Test-Path -LiteralPath $editor)) {
    throw "The editor executable was not found at $editor. Build the Release preset first."
}

$env:PATH = "C:\Qt\6.11.1\msvc2022_64\bin;$env:PATH"
$project = Join-Path $repositoryRoot 'samples\Slice1Sample\DragonPixelProject.json'
Start-Process -FilePath $editor -ArgumentList @('--project', $project) -WorkingDirectory $repositoryRoot
Write-Host 'Dragon Pixel Editor launched with the Slice 1 sample.' -ForegroundColor Green
