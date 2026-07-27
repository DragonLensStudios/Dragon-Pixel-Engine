[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$ResetSample,
    [switch]$Production,
    [Parameter(DontShow = $true)]
    [string]$LaunchProbeExecutable,
    [Parameter(DontShow = $true)]
    [string]$LaunchProbeProject,
    [Parameter(DontShow = $true)]
    [ValidateSet('Auto', 'ArgumentList', 'QuotedArguments')]
    [string]$ArgumentTransport = 'Auto',
    [Parameter(DontShow = $true)]
    [ValidateRange(50, 10000)]
    [int]$StartupProbeMilliseconds = 750
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$usingLaunchProbe = -not [string]::IsNullOrWhiteSpace($LaunchProbeExecutable) -or
    -not [string]::IsNullOrWhiteSpace($LaunchProbeProject)
if ($usingLaunchProbe) {
    if ([string]::IsNullOrWhiteSpace($LaunchProbeExecutable) -or
        [string]::IsNullOrWhiteSpace($LaunchProbeProject)) {
        throw 'LaunchProbeExecutable and LaunchProbeProject must be provided together.'
    }
    if (-not $SkipBuild) {
        throw 'The launcher probe requires SkipBuild so it cannot trigger a production build.'
    }
    if ($ResetSample) {
        throw 'ResetSample cannot be combined with the launcher probe.'
    }
    if ($Production) {
        throw 'Production cannot be combined with the launcher probe.'
    }

    if (-not (Test-Path -LiteralPath $LaunchProbeExecutable -PathType Leaf)) {
        throw "The launcher probe executable was not found: $LaunchProbeExecutable"
    }
    if (-not (Test-Path -LiteralPath $LaunchProbeProject -PathType Leaf)) {
        throw "The launcher probe project was not found: $LaunchProbeProject"
    }
    $editor = (Resolve-Path -LiteralPath $LaunchProbeExecutable -ErrorAction Stop).Path
    $project = (Resolve-Path -LiteralPath $LaunchProbeProject -ErrorAction Stop).Path
} else {
    if (-not $SkipBuild) {
        if ($Production) {
            & (Join-Path $PSScriptRoot 'Build-Production-Editor.ps1')
        } else {
            & (Join-Path $PSScriptRoot 'Build-Windows.ps1') -Configuration Release
        }
    }

    $editor = if ($Production) {
        Join-Path $repositoryRoot 'out\product\windows-x64\DragonPixelEditor\DragonPixelEditor.exe'
    } else {
        Join-Path $repositoryRoot 'out\build\windows-msvc-vcpkg\src\editor\Release\DragonPixelEditor.exe'
    }
    if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
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
    if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
        throw "The writable development sample is incomplete: $project was not found."
    }
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $editor
$startInfo.UseShellExecute = $false
$startInfo.WorkingDirectory = $repositoryRoot
$argumentListAvailable = $null -ne $startInfo.PSObject.Properties['ArgumentList']
$useArgumentList = switch ($ArgumentTransport) {
    'ArgumentList' {
        if (-not $argumentListAvailable) {
            throw 'The ArgumentList transport is unavailable in this PowerShell/.NET runtime.'
        }
        $true
    }
    'QuotedArguments' { $false }
    default { $argumentListAvailable }
}
if ($useArgumentList) {
    $startInfo.ArgumentList.Add('--project')
    $startInfo.ArgumentList.Add($project)
} else {
    # Windows PowerShell 5.1 uses .NET Framework and has no ArgumentList API.
    # Windows paths cannot contain a quote, so explicit quoting is unambiguous here.
    if ($project.Contains('"')) {
        throw 'The project path contains a quote and cannot be represented by the Windows launcher.'
    }
    $startInfo.Arguments = "--project `"$project`""
}
if ($Production) {
    $startInfo.EnvironmentVariables['PATH'] = "$(Split-Path -Parent $editor);$env:PATH"
} else {
    $startInfo.EnvironmentVariables['PATH'] = "C:\Qt\6.11.1\msvc2022_64\bin;$env:PATH"
}
$process = [System.Diagnostics.Process]::Start($startInfo)
if ($null -eq $process) {
    throw 'Dragon Pixel Editor did not start.'
}
try {
    if ($process.WaitForExit($StartupProbeMilliseconds)) {
        $exitCode = $process.ExitCode
        throw "Dragon Pixel Editor exited during its $StartupProbeMilliseconds ms startup probe with exit code $exitCode."
    }
} finally {
    $process.Dispose()
}

Write-Host "Dragon Pixel Editor started with project $project and remained active through the $StartupProbeMilliseconds ms startup probe." -ForegroundColor Green
