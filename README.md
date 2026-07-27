# Dragon Pixel Engine

Dragon Pixel Engine is pursuing the complete four-slice roadmap and design-defined `1.0.0` under the active mirrored master plan. Slice 1 remains open on the unchanged macOS viewport gate, current Ubuntu/macOS matrices, and complete POC J acceptance; Slice 2 remains open on its complete cross-platform designer/accessibility evidence; and the activated Slice 3/4 lifecycle and release paths are not yet accepted. The current post-atomic-repair feature branch passes 56/56 Windows strict Release tests in 423.22 seconds and 56/56 MSVC AddressSanitizer tests in 560.43 seconds; its formerly inconclusive aggregate sanitizer interaction aliases now pass under unchanged caps. Focused DPE-ARCH-0010 lifecycle, DPE-ARCH-0011 external Rider source-authoring, DPE-ARCH-0012 managed GameObject-controller/runtime-transform, DPE-ARCH-0013 configurable input-map/rebinding, and DPE-ARCH-0014 Project Hub/asset/Hierarchy/multi-Inspector evidence remains green. The refreshed 188-record production-style bundle hash-verifies, passes its packaged MonoGame self-test, and creates/opens a new project outside the source tree. The atomic-publication repair resolves prior journals before successor saves, serializes cooperating recovery-root callers, preserves conflicts, and retains deterministic journal candidates; ADR-0006 handle-pinning/noncooperating-writer limits and current POSIX execution remain open. Current Ubuntu and macOS matrices remain open, no POC/ADR/slice/release gate is promoted, and KNI remains experimental. The repository contains the architecture prototypes, portable native core, managed contracts and C ABI interop, distinct preview/play MonoGame and KNI workers, deterministic scene-v3 persistence, linked-prefab and physics foundations, external Python automation, a writable sample project, and the Qt 6.11 editor.

## Development workflow

Feature work starts from current `develop`, uses one focused `feature/*` branch and a mirrored living plan, proceeds through small tested commits, and ends with aggregate diff review plus a draft pull request into `develop`. Codex and contributors must read [AGENTS.md](AGENTS.md) and the [Development Constitution](docs/Development/Development%20Constitution.md); the reusable [feature-plan template](docs/Development/Feature%20Plan%20Template.md), [verification checklist](docs/Development/Verification%20Checklist.md), and [feature PR template](.github/PULL_REQUEST_TEMPLATE/feature.md) define the handoff. Pull requests are reviewed before merge.

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

For the fastest production-style editor iteration on Windows, build only the Release editor and its required runtime dependencies, deploy a stable runnable bundle, run its packaged smoke test, and launch it:

```powershell
& '.\scripts\dev\Build-Production-Editor.ps1' -Launch
```

After the first validated bundle, close the running editor and use the fast incremental path while editing:

```powershell
& '.\scripts\dev\Build-Production-Editor.ps1' -Fast -Launch
```

Relaunch the current bundle without building:

```powershell
& '.\scripts\dev\Launch-Editor.ps1' -Production -SkipBuild
```

The directly runnable executable is `out\product\windows-x64\DragonPixelEditor\DragonPixelEditor.exe`. Its adjacent bundle includes Qt plugins, native runtime libraries, MonoGame/KNI workers, contracts, Python tools, schemas, a sample, and a SHA-256 file manifest. This is a production-style developer bundle for rapid local testing; it is not yet the signed, clean-machine-validated POC R release package.

The editor opens the 2D/3D sample in authoring mode. The Hierarchy, typed Inspector, Project Explorer, Scene View, and structured Console are model-backed full-grid docks: there is no reserved center obstruction, and panels can split, tab, float, move to any dock area, reset deterministically, and restore versioned per-user layouts. GameObject presets, multi-selection, component editing, direct New C# Script/New C++ Component attachment, asset drag/drop, Undo/Redo, workspaces, picking, camera controls, and Move/Rotate/Scale gizmos route authoritative edits through validated scene transactions. Contained C#/C++ component sources appear in Project Explorer; double-clicking one or choosing the Inspector component-card Rider edit action regenerates a disposable `.dragonpixel/Ide/Rider` solution and opens the selected source in JetBrains Rider without loading project code into the editor process.

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
