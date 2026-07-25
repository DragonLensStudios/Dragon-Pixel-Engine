[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$ResetSample
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build-Windows.ps1') -Configuration Release
}

$editor = Join-Path $repositoryRoot 'out\build\windows-msvc-vcpkg\src\editor\Release\DragonPixelEditor.exe'
if (-not (Test-Path -LiteralPath $editor)) {
    throw "The editor executable was not found at $editor. Build the Release preset first."
}

$sourceSample = Join-Path $repositoryRoot 'samples\Slice1Sample'
$developmentRoot = Join-Path $repositoryRoot 'out\dev'
$developmentSample = Join-Path $developmentRoot 'Slice1Sample'
if ($ResetSample -and (Test-Path -LiteralPath $developmentSample)) {
    $resolvedDevelopmentRoot = (Resolve-Path -LiteralPath $developmentRoot).Path
    $resolvedDevelopmentSample = (Resolve-Path -LiteralPath $developmentSample).Path
    if (-not $resolvedDevelopmentSample.StartsWith(
            $resolvedDevelopmentRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset sample outside $resolvedDevelopmentRoot."
    }
    Remove-Item -LiteralPath $resolvedDevelopmentSample -Recurse -Force
    Write-Host "Reset writable development sample $resolvedDevelopmentSample." -ForegroundColor Yellow
}
if (-not (Test-Path -LiteralPath $developmentSample)) {
    New-Item -ItemType Directory -Path $developmentRoot -Force | Out-Null
    Copy-Item -LiteralPath $sourceSample -Destination $developmentSample -Recurse
}

$project = Join-Path $developmentSample 'DragonPixelProject.json'
if (-not (Test-Path -LiteralPath $project)) {
    throw "The writable development sample is incomplete: $project was not found."
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $editor
$startInfo.UseShellExecute = $false
$startInfo.WorkingDirectory = $repositoryRoot
if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
    $startInfo.ArgumentList.Add('--project')
    $startInfo.ArgumentList.Add($project)
} else {
    # Windows PowerShell 5.1 uses .NET Framework and has no ArgumentList API.
    # Windows paths cannot contain a quote, so explicit quoting is unambiguous here.
    $startInfo.Arguments = "--project `"$project`""
}
$startInfo.EnvironmentVariables['PATH'] = "C:\Qt\6.11.1\msvc2022_64\bin;$env:PATH"
$process = [System.Diagnostics.Process]::Start($startInfo)
if ($null -eq $process) {
    throw 'Dragon Pixel Editor did not start.'
}
$process.Dispose()

Write-Host "Dragon Pixel Editor launched with writable sample $project." -ForegroundColor Green
