[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LauncherPath,
    [Parameter(Mandatory = $true)]
    [string]$ProbeExecutable,
    [Parameter(Mandatory = $true)]
    [ValidateSet('ArgumentList', 'QuotedArguments')]
    [string]$ArgumentTransport
)

$ErrorActionPreference = 'Stop'
$launcher = (Resolve-Path -LiteralPath $LauncherPath -ErrorAction Stop).Path
$probe = (Resolve-Path -LiteralPath $ProbeExecutable -ErrorAction Stop).Path

if ($ArgumentTransport -eq 'ArgumentList') {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    if ($null -eq $startInfo.PSObject.Properties['ArgumentList']) {
        throw 'This test requires a PowerShell/.NET runtime with ProcessStartInfo.ArgumentList.'
    }
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'Dragon Pixel Launcher Path With Spaces ' + [Guid]::NewGuid().ToString('N'))
$editorDirectory = Join-Path $testRoot 'Editor Binary With Spaces'
$projectDirectory = Join-Path $testRoot 'Writable Project With Spaces'
$copiedProbe = Join-Path $editorDirectory 'Dragon Pixel Editor Probe.exe'
$project = Join-Path $projectDirectory 'DragonPixelProject.json'
$marker = Join-Path $testRoot 'argument-preserved.marker'

$environmentNames = @(
    'DPE_LAUNCH_PROBE_EXPECTED_EXECUTABLE',
    'DPE_LAUNCH_PROBE_EXPECTED_PROJECT',
    'DPE_LAUNCH_PROBE_MARKER',
    'DPE_LAUNCH_PROBE_SLEEP_MS'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    New-Item -ItemType Directory -Path $editorDirectory, $projectDirectory -Force | Out-Null
    Copy-Item -LiteralPath $probe -Destination $copiedProbe
    Get-ChildItem -LiteralPath (Split-Path -Parent $probe) -Filter 'clang_rt.asan_dynamic-*.dll' |
        Copy-Item -Destination $editorDirectory
    Set-Content -LiteralPath $project -Value '{"format":"dpe.project","version":3}' -Encoding UTF8

    $copiedProbe = (Resolve-Path -LiteralPath $copiedProbe).Path
    $project = (Resolve-Path -LiteralPath $project).Path
    $env:DPE_LAUNCH_PROBE_EXPECTED_EXECUTABLE = $copiedProbe
    $env:DPE_LAUNCH_PROBE_EXPECTED_PROJECT = $project
    $env:DPE_LAUNCH_PROBE_MARKER = $marker
    $env:DPE_LAUNCH_PROBE_SLEEP_MS = '500'

    & $launcher -SkipBuild `
        -LaunchProbeExecutable $copiedProbe `
        -LaunchProbeProject $project `
        -ArgumentTransport $ArgumentTransport `
        -StartupProbeMilliseconds 100

    $markerDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not (Test-Path -LiteralPath $marker -PathType Leaf) -and
        [DateTime]::UtcNow -lt $markerDeadline) {
        Start-Sleep -Milliseconds 25
    }
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "$ArgumentTransport did not preserve the exact executable and project arguments."
    }
    if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne 'argument-preservation-ok') {
        throw "$ArgumentTransport produced an invalid probe result."
    }

    Remove-Item -LiteralPath $marker -Force
    $env:DPE_LAUNCH_PROBE_EXPECTED_PROJECT = $project + '.must-not-match'
    $launchFailure = $null
    try {
        & $launcher -SkipBuild `
            -LaunchProbeExecutable $copiedProbe `
            -LaunchProbeProject $project `
            -ArgumentTransport $ArgumentTransport `
            -StartupProbeMilliseconds 100
    } catch {
        $launchFailure = $_
    }
    if ($null -eq $launchFailure) {
        throw "$ArgumentTransport reported success after the probe rejected its project argument."
    }
    if ($launchFailure.Exception.Message -notmatch 'exit code 23') {
        throw "$ArgumentTransport surfaced an unexpected startup failure: $($launchFailure.Exception.Message)"
    }

    Write-Output "$ArgumentTransport preserved the spaced executable/project paths and surfaced immediate argument rejection."
} finally {
    foreach ($name in $environmentNames) {
        $value = $previousEnvironment[$name]
        if ($null -eq $value) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        } else {
            Set-Item -LiteralPath "Env:$name" -Value $value
        }
    }

    Start-Sleep -Milliseconds 600
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = (Resolve-Path -LiteralPath $testRoot).Path
        $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar)
        if (-not $resolvedTestRoot.StartsWith(
                $resolvedTemporaryRoot + [System.IO.Path]::DirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove launcher test directory outside $resolvedTemporaryRoot."
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
