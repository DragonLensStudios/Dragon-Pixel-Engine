#Requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot '..\WindowsCI.psm1') -Force

Invoke-DpeCiNative -Operation 'Expected successful command' -FilePath $env:ComSpec -ArgumentList @(
    '/d', '/c', 'exit', '0'
)

$failureWasRejected = $false
try {
    Invoke-DpeCiNative -Operation 'Expected failing command' -FilePath $env:ComSpec -ArgumentList @(
        '/d', '/c', 'exit', '19'
    )
}
catch {
    if ($_.Exception.Message -ne 'Expected failing command failed with exit code 19.') {
        throw
    }
    $failureWasRejected = $true
}

if (-not $failureWasRejected) {
    throw 'A failing native command was allowed to pass.'
}

Write-Host 'Windows CI native-command failure propagation passed.' -ForegroundColor Green
