# Dragon Pixel Engine

Dragon Pixel Engine is implementing the complete Slice 2 authoring workflow while Slice 1's final cross-platform performance gate remains open on macOS. The repository contains the architecture prototypes, portable native core, managed contracts and C ABI interop, distinct preview/play MonoGame and experimental KNI workers, deterministic scene-v3 persistence, linked-prefab and physics foundations, external Python automation, a writable sample project, and the Qt 6.11 editor.

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

Launch the editor with a writable copy of the checked-in sample:

```powershell
& '.\scripts\dev\Launch-Editor.ps1'
```

Use `-SkipBuild` for a fast edit/run cycle after a successful build. Use `-ResetSample` when you intentionally want to discard the writable development copy and recreate it from `samples\Slice1Sample`:

```powershell
& '.\scripts\dev\Launch-Editor.ps1' -SkipBuild -ResetSample
```

The editor opens the 2D/3D sample in authoring mode. The Hierarchy, typed Inspector, Project Explorer, Scene View, and structured Console are model-backed. GameObject presets, multi-selection, component editing, asset drag/drop, Undo/Redo, workspaces, picking, camera controls, and Move/Rotate/Scale gizmos route authoritative edits through validated scene transactions.

A dedicated preview worker consumes the editor-owned mirror and reloads revisions in place. Play launches a separate MonoGame or experimental KNI worker from an immutable snapshot. Simulate Preview runs an isolated native Box2D/Jolt world; Play always owns a disposable runtime world. Stop destroys runtime state without writing simulated transforms into the authoring scene. Both framework adapters use real graphics-device render targets, revisioned local IPC, ID-buffer picking, and BGRA8 shared frames.

## Ubuntu verification from Windows

With Docker Desktop running, build a clean Ubuntu 24.04 image pinned to Qt 6.11.1, .NET SDK 10.0.203, and the repository's vcpkg baseline, then run the Release suite:

```powershell
& '.\scripts\ci\Test-UbuntuContainer.ps1'
```

Reuse the cached toolchain image for the Clang AddressSanitizer matrix:

```powershell
& '.\scripts\ci\Test-UbuntuContainer.ps1' -AddressSanitizer -SkipImageBuild
```

The container copies the source into its native filesystem before building so shared-memory frame measurements are not distorted by a Windows bind mount. Use `-VerboseTests` to display individual worker metrics and graphics-probe results.
