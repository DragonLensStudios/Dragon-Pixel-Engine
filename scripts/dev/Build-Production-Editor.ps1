[CmdletBinding()]
param(
    [switch]$Fast,
    [switch]$SkipConfigure,
    [switch]$SkipSmokeTest,
    [switch]$Launch,
    [switch]$ResetSample
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildRoot = Join-Path $repositoryRoot 'out\build\windows-msvc-vcpkg'
$productRoot = Join-Path $repositoryRoot 'out\product\windows-x64'
$outputRoot = Join-Path $productRoot 'DragonPixelEditor'
$qtRoot = 'C:\Qt\6.11.1\msvc2022_64'
$qtDeploy = Join-Path $qtRoot 'bin\windeployqt.exe'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$outputEditor = Join-Path $outputRoot 'DragonPixelEditor.exe'

function Assert-ContainedOutput([string]$Path) {
    $resolvedProduct = [System.IO.Path]::GetFullPath($productRoot).TrimEnd('\') + '\'
    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedProduct, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to mutate an output outside $resolvedProduct`: $resolvedPath"
    }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Required bundle directory was not found: $Source"
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -LiteralPath $Source -Force |
        Copy-Item -Destination $Destination -Recurse -Force
}

function Copy-MissingDirectoryContents([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Required bundle directory was not found: $Source"
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($Source.Length).TrimStart('\')
        $target = Join-Path $Destination $relative
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target
        }
    }
}

if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}
if (-not (Test-Path -LiteralPath $qtDeploy -PathType Leaf)) {
    throw "Qt deployment tool was not found at $qtDeploy. Run Install-WindowsPrerequisites.ps1."
}
$runningBundle = Get-Process -Name 'DragonPixelEditor' -ErrorAction SilentlyContinue |
    Where-Object {
        try { $_.Path -and $_.Path.Equals($outputEditor, [StringComparison]::OrdinalIgnoreCase) }
        catch { $false }
    }
if ($runningBundle) {
    throw "Close the running production editor before rebuilding it so unsaved work is not interrupted: $outputEditor"
}

$visualStudioPath = & $vswhere -latest -products * -version '[17.0,19.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioPath) {
    throw 'A Visual Studio installation with MSVC was not found. Run Install-WindowsPrerequisites.ps1.'
}
$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64

$integratedVcpkgRoot = Join-Path $visualStudioPath 'VC\vcpkg'
if (-not $env:VCPKG_ROOT -or
    -not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
    $env:VCPKG_ROOT = $integratedVcpkgRoot
}
if (-not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
    throw "vcpkg was not found at $env:VCPKG_ROOT. Run Install-WindowsPrerequisites.ps1."
}

$effectiveSkipConfigure = $SkipConfigure -or $Fast
if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt'))) {
    $effectiveSkipConfigure = $false
}

Push-Location $repositoryRoot
try {
    if (-not $effectiveSkipConfigure) {
        cmake --preset windows-msvc
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    }
    cmake --build $buildRoot --config Release --target DragonPixelEditor
    if ($LASTEXITCODE -ne 0) { throw 'Incremental DragonPixelEditor build failed.' }
} finally {
    Pop-Location
}

$editorSource = Join-Path $buildRoot 'src\editor\Release\DragonPixelEditor.exe'
$tiledImporterSource = Join-Path $buildRoot 'src\importers\Tiled\Release\DragonPixelTiledImporterWorker.exe'
$nativeSource = Join-Path $buildRoot 'src\native\CAbi\Release'
$monoGameSource = Join-Path $repositoryRoot 'src\managed\DragonPixel.Adapter.MonoGame.Worker\bin\Release\net10.0'
$kniSource = Join-Path $repositoryRoot 'src\managed\DragonPixel.Adapter.Kni.Worker\bin\Release\net10.0'
$contractsSource = Join-Path $repositoryRoot 'src\managed\DragonPixel.Contracts\bin\Release\netstandard2.1\DragonPixel.Contracts.dll'
$sdlSource = Join-Path $buildRoot 'vcpkg_installed\x64-windows\bin\SDL3.dll'
$sdlLicenseSource = Join-Path $buildRoot 'vcpkg_installed\x64-windows\share\sdl3\copyright'
foreach ($requiredFile in @(
        $editorSource,
        $tiledImporterSource,
        (Join-Path $nativeSource 'dragonpixel.dll'),
        (Join-Path $nativeSource 'box2d.dll'),
        (Join-Path $monoGameSource 'DragonPixel.Adapter.MonoGame.Worker.dll'),
        (Join-Path $kniSource 'DragonPixel.Adapter.Kni.Worker.dll'),
        $contractsSource,
        $sdlSource,
        $sdlLicenseSource)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required bundle artifact was not found: $requiredFile"
    }
}

New-Item -ItemType Directory -Path $productRoot -Force | Out-Null
$stagingRoot = Join-Path $productRoot ('.DragonPixelEditor-staging-' + [Guid]::NewGuid().ToString('N'))
Assert-ContainedOutput $stagingRoot
Assert-ContainedOutput $outputRoot

try {
    New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
    Copy-Item -LiteralPath $editorSource -Destination (Join-Path $stagingRoot 'DragonPixelEditor.exe')
    Copy-Item -LiteralPath $tiledImporterSource `
        -Destination (Join-Path $stagingRoot 'DragonPixelTiledImporterWorker.exe')

    & $qtDeploy --release --no-translations --no-opengl-sw --dir $stagingRoot `
        (Join-Path $stagingRoot 'DragonPixelEditor.exe')
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }
    # windeployqt selects the interactive Windows platform plugin. Keep the
    # release offscreen plugin too so the deployed executable can self-test.
    Copy-Item -LiteralPath (Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll') `
        -Destination (Join-Path $stagingRoot 'platforms\qoffscreen.dll') -Force
    Copy-Item -LiteralPath $sdlSource -Destination (Join-Path $stagingRoot 'SDL3.dll')
    New-Item -ItemType Directory -Path (Join-Path $stagingRoot 'licenses') -Force | Out-Null
    Copy-Item -LiteralPath $sdlLicenseSource -Destination (Join-Path $stagingRoot 'licenses\SDL3.txt')

    $nativeDestination = Join-Path $stagingRoot 'runtime\native'
    New-Item -ItemType Directory -Path $nativeDestination -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $nativeSource 'dragonpixel.dll') -Destination $nativeDestination
    Copy-Item -LiteralPath (Join-Path $nativeSource 'box2d.dll') -Destination $nativeDestination
    Copy-DirectoryContents $monoGameSource (Join-Path $stagingRoot 'runtime\workers\monogame')
    Copy-DirectoryContents $kniSource (Join-Path $stagingRoot 'runtime\workers\kni')

    $contractsDestination = Join-Path $stagingRoot 'runtime\contracts'
    New-Item -ItemType Directory -Path $contractsDestination -Force | Out-Null
    Copy-Item -LiteralPath $contractsSource -Destination $contractsDestination
    Copy-DirectoryContents (Join-Path $repositoryRoot 'tools\python') (Join-Path $stagingRoot 'tools\python')
    Copy-DirectoryContents (Join-Path $repositoryRoot 'schemas') (Join-Path $stagingRoot 'schemas')
    Copy-DirectoryContents (Join-Path $repositoryRoot 'templates') (Join-Path $stagingRoot 'templates')
    $sampleSource = Join-Path $repositoryRoot 'samples\Slice1Sample'
    $existingWritableSample = Join-Path $outputRoot 'samples\Slice1Sample'
    if (-not $ResetSample -and
        (Test-Path -LiteralPath (Join-Path $existingWritableSample 'DragonPixelProject.json') -PathType Leaf)) {
        $sampleSource = $existingWritableSample
    }
    Copy-DirectoryContents $sampleSource (Join-Path $stagingRoot 'samples\Slice1Sample')
    if ($sampleSource -eq $existingWritableSample) {
        Copy-MissingDirectoryContents (Join-Path $repositoryRoot 'samples\Slice1Sample') `
            (Join-Path $stagingRoot 'samples\Slice1Sample')
    }

    $files = Get-ChildItem -LiteralPath $stagingRoot -Recurse -File | Sort-Object FullName
    $manifestFiles = @($files | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($stagingRoot.Length + 1).Replace('\', '/')
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    $manifest = [ordered]@{
        format = 'dpe.developer-editor-bundle'
        formatVersion = 1
        configuration = 'Release'
        platform = 'windows'
        architecture = 'x64'
        releaseGateSatisfied = $false
        note = 'Developer production-style bundle for iterative testing; not the signed POC R release package.'
        files = $manifestFiles
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        (Join-Path $stagingRoot 'bundle-manifest.json'),
        (($manifest | ConvertTo-Json -Depth 5) + "`n"),
        $utf8NoBom)

    $requiredBundlePaths = @(
        'DragonPixelEditor.exe',
        'DragonPixelTiledImporterWorker.exe',
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'Qt6Network.dll',
        'SDL3.dll',
        'platforms\qwindows.dll',
        'platforms\qoffscreen.dll',
        'runtime\native\dragonpixel.dll',
        'runtime\workers\monogame\DragonPixel.Adapter.MonoGame.Worker.dll',
        'runtime\workers\kni\DragonPixel.Adapter.Kni.Worker.dll',
        'runtime\contracts\DragonPixel.Contracts.dll',
        'licenses\SDL3.txt',
        'tools\python\dragonpixel_tools',
        'templates\Minimal2D\DragonPixelTemplate.json',
        'templates\Minimal3D\DragonPixelTemplate.json',
        'schemas\v4\project.schema.json',
        'schemas\v1\project-template.schema.json',
        'schemas\v3\asset-metadata.schema.json',
        'samples\Slice1Sample\DragonPixelProject.json',
        'samples\Slice1Sample\Assets\DefaultInput.dpeasset',
        'samples\Slice1Sample\Assets\Input\DefaultGameplay.dpeinputmap',
        'bundle-manifest.json')
    foreach ($relative in $requiredBundlePaths) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagingRoot $relative))) {
            throw "Production editor bundle is incomplete: $relative was not deployed."
        }
    }

    if (-not ($SkipSmokeTest -or $Fast)) {
        $savedPlatform = $env:QT_QPA_PLATFORM
        $overrideNames = @(
            'DPE_DEFAULT_SAMPLE_PROJECT',
            'DPE_EDITOR_NATIVE_LIBRARY',
            'DPE_EDITOR_MONOGAME_DLL',
            'DPE_EDITOR_KNI_DLL',
            'DPE_CONTRACTS_ASSEMBLY',
            'DPE_PROJECT_TEMPLATES_ROOT',
            'DPE_PYTHON_TOOLS_ROOT')
        $savedOverrides = @{}
        try {
            $env:QT_QPA_PLATFORM = 'offscreen'
            foreach ($name in $overrideNames) {
                $savedOverrides[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
                [Environment]::SetEnvironmentVariable($name, $null, 'Process')
            }
            & (Join-Path $stagingRoot 'DragonPixelEditor.exe') `
                --project (Join-Path $stagingRoot 'samples\Slice1Sample\DragonPixelProject.json') `
                --self-test monogame
            if ($LASTEXITCODE -ne 0) { throw 'Packaged MonoGame editor self-test failed.' }
        } finally {
            $env:QT_QPA_PLATFORM = $savedPlatform
            foreach ($name in $overrideNames) {
                [Environment]::SetEnvironmentVariable($name, $savedOverrides[$name], 'Process')
            }
        }
    }

    if (Test-Path -LiteralPath $outputRoot) {
        Get-ChildItem -LiteralPath $stagingRoot -Force | ForEach-Object {
            if ($_.Name -eq 'samples') {
                if (-not $ResetSample) {
                    Copy-MissingDirectoryContents $_.FullName (Join-Path $outputRoot 'samples')
                }
                return
            }
            $destination = Join-Path $outputRoot $_.Name
            if ($_.PSIsContainer) {
                Copy-DirectoryContents $_.FullName $destination
            } else {
                Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
            }
        }
        if ($ResetSample) {
            $outputSample = Join-Path $outputRoot 'samples\Slice1Sample'
            Assert-ContainedOutput $outputSample
            if (Test-Path -LiteralPath $outputSample) {
                Remove-Item -LiteralPath $outputSample -Recurse -Force
            }
            Copy-DirectoryContents (Join-Path $stagingRoot 'samples\Slice1Sample') $outputSample
        }
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    } else {
        Move-Item -LiteralPath $stagingRoot -Destination $outputRoot
    }
} catch {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    throw
}

Write-Host "Production-style editor bundle generated at $outputRoot" -ForegroundColor Green
Write-Host "Executable: $outputEditor" -ForegroundColor Green

if ($Launch) {
    $launcherArguments = @('-Production', '-SkipBuild')
    if ($ResetSample) { $launcherArguments += '-ResetSample' }
    & (Join-Path $PSScriptRoot 'Launch-Editor.ps1') @launcherArguments
}
