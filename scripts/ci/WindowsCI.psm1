Set-StrictMode -Version Latest

function Invoke-DpeCiNative {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Operation,

        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter()]
        [AllowEmptyCollection()]
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Operation failed with exit code $exitCode."
    }
}

Export-ModuleMember -Function Invoke-DpeCiNative
