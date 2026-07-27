[CmdletBinding()]
param(
    [string]$RepositoryDocsRoot,
    [string]$ExternalDocsRoot = 'C:\Projects\Documentation\Engines\Dragon Pixel Engine'
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryDocsRoot)) {
    $RepositoryDocsRoot = Join-Path $PSScriptRoot '..\..\docs'
}
$RepositoryDocsRoot = [System.IO.Path]::GetFullPath($RepositoryDocsRoot)
$ExternalDocsRoot = [System.IO.Path]::GetFullPath($ExternalDocsRoot)
$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)

function Get-RelativeMarkdownFiles([string]$Root) {
    $rootPrefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.md' |
        ForEach-Object { $_.FullName.Substring($rootPrefix.Length).Replace('\', '/') } |
        Sort-Object
}

$repositoryFiles = @(Get-RelativeMarkdownFiles $RepositoryDocsRoot)
$externalFiles = @(Get-RelativeMarkdownFiles $ExternalDocsRoot)
$setDifference = @(Compare-Object $repositoryFiles $externalFiles)
if ($setDifference.Count -ne 0) {
    $details = $setDifference | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }
    throw "Documentation mirror file sets differ:`n$($details -join "`n")"
}

$linkPattern = [regex]'!?\[[^\]]*\]\((?<target><[^>]+>|[^)]+)\)'
foreach ($relativePath in $repositoryFiles) {
    $repositoryPath = Join-Path $RepositoryDocsRoot $relativePath
    $externalPath = Join-Path $ExternalDocsRoot $relativePath
    $repositoryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $repositoryPath).Hash
    $externalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $externalPath).Hash
    if ($repositoryHash -ne $externalHash) {
        throw "Mirror hash mismatch: $relativePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($repositoryPath)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw "UTF-8 BOM is not permitted: $relativePath"
    }
    if ([Array]::IndexOf($bytes, [byte]0x0D) -ge 0) {
        throw "CR/CRLF line ending found: $relativePath"
    }
    try {
        $text = $strictUtf8.GetString($bytes)
    }
    catch {
        throw "Invalid UTF-8: $relativePath ($($_.Exception.Message))"
    }

    foreach ($match in $linkPattern.Matches($text)) {
        $target = $match.Groups['target'].Value.Trim().Trim('<', '>')
        if ($target.StartsWith('#') -or $target -match '^(?i:https?|mailto):') {
            if ($target -notmatch '^#' -and -not [System.Uri]::IsWellFormedUriString($target, [System.UriKind]::Absolute)) {
                throw "Malformed absolute Markdown link in ${relativePath}: $target"
            }
            continue
        }
        $pathOnly = [System.Uri]::UnescapeDataString(($target -split '#', 2)[0])
        if ([string]::IsNullOrWhiteSpace($pathOnly)) {
            continue
        }
        $linkDirectory = [System.IO.Path]::GetDirectoryName($repositoryPath)
        $combinedTarget = Join-Path -Path $linkDirectory -ChildPath (
            $pathOnly.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
        $resolvedTarget = [System.IO.Path]::GetFullPath($combinedTarget)
        if (-not (Test-Path -LiteralPath $resolvedTarget)) {
            throw "Broken local Markdown link in ${relativePath}: $target"
        }
    }
}

$designPath = Join-Path $RepositoryDocsRoot 'Dragon Pixel Engine Design Document.md'
$promptPath = Join-Path $RepositoryDocsRoot 'Dragon Pixel Engine LLM Prompt Source.md'
$agentsPath = Join-Path (Split-Path $RepositoryDocsRoot -Parent) 'AGENTS.md'
$designRevision = [regex]::Match(
    [System.IO.File]::ReadAllText($designPath),
    'Design revision:\*\* `(?<revision>DPE-ARCH-\d{4})`').Groups['revision'].Value
$promptRevision = [regex]::Match(
    [System.IO.File]::ReadAllText($promptPath),
    'Design revision:\*\* `(?<revision>DPE-ARCH-\d{4})`').Groups['revision'].Value
$agentRevision = [regex]::Match(
    [System.IO.File]::ReadAllText($agentsPath),
    'current synchronized design revision is `(?<revision>DPE-ARCH-\d{4})`').Groups['revision'].Value
$revisionsMatch = -not [string]::IsNullOrWhiteSpace($designRevision) -and
    $designRevision -eq $promptRevision -and
    $designRevision -eq $agentRevision
if (-not $revisionsMatch) {
    throw "Design revision mismatch: Design=$designRevision Prompt=$promptRevision AGENTS=$agentRevision"
}

Write-Host "Documentation mirrors passed: $($repositoryFiles.Count) UTF-8/LF Markdown pairs, revision $designRevision."
