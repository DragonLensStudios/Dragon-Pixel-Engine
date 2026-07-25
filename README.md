# Dragon Pixel Engine

Dragon Pixel Engine is in Slice 1 cross-platform acceptance. The repository contains the architecture prototypes, portable native core, managed contracts and interop, distinct preview/play MonoGame and KNI workers, deterministic scene-v2 persistence, external Python automation, a sample project, and the Qt 6.11 editor shell.

## Windows development quick start

From an elevated PowerShell session:

```powershell
& '.\scripts\dev\Install-WindowsPrerequisites.ps1'
```

Open a new PowerShell session after installation, then verify the complete C++20, Qt Widgets, CMake/CTest, Ninja, and .NET 10 loop:

```powershell
& '.\scripts\dev\Test-WindowsEnvironment.ps1'
```

The verifier builds disposable native/Qt and managed smoke projects under the Windows temporary directory, runs their tests, checks dynamic Qt linkage, and removes successful artifacts. Use `-KeepArtifacts` when debugging the smoke project.

The install script is driven by the repository `.vsconfig`, uses the Qt-supported MSVC v143 toolset, and pins Qt to 6.11.1 MSVC 2022 x64. Environment decisions and evidence are recorded in `docs\Plans`.

Build and run the complete Windows Release test suite:

```powershell
& '.\scripts\dev\Build-Windows.ps1'
```

Build under MSVC AddressSanitizer:

```powershell
& '.\scripts\dev\Build-Windows.ps1' -AddressSanitizer
```

Launch the editor with the checked-in Slice 1 sample:

```powershell
& '.\scripts\dev\Launch-Editor.ps1'
```

The editor opens the 2D/3D sample in authoring mode. A dedicated preview worker consumes the editor-owned mirror; Play launches a separate MonoGame or experimental KNI worker from an immutable snapshot. Both use versioned local IPC and BGRA8 shared frames. Stop discards only the play world without modifying the authoring scene.

## Ubuntu verification from Windows

With Docker Desktop running, build a clean Ubuntu 24.04 image pinned to Qt 6.11.1 and .NET SDK 10.0.203, then run all 15 Release tests:

```powershell
& '.\scripts\ci\Test-UbuntuContainer.ps1'
```

Reuse the cached toolchain image for the Clang AddressSanitizer matrix:

```powershell
& '.\scripts\ci\Test-UbuntuContainer.ps1' -AddressSanitizer -SkipImageBuild
```

The container copies the source into its native filesystem before building so shared-memory frame measurements are not distorted by a Windows bind mount. Use `-VerboseTests` to display individual worker metrics and graphics-probe results.
