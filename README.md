# Dragon Pixel Engine

Dragon Pixel Engine is pursuing the complete four-slice roadmap and design-defined `1.0.0` under the active mirrored master plan. Slice 1 remains open on macOS rendering/interaction gates, the Ubuntu sanitizer matrix, hosted Windows graphics/UI execution, and complete POC J acceptance; Slice 2 remains open on complete cross-platform designer/accessibility evidence; and the activated Slice 3/4 lifecycle and release paths are not yet accepted. Editor-library modularization PR #3, CI/GitFlow PR #4, and isolated Tiled-import PR #5 are merged into `develop`. The current tilemap-authoring workspace branch passes the complete strict Windows Release matrix at 60/60 in 446.31 seconds; its final focused MSVC AddressSanitizer tile workflow passes 6/6 stages, while the broader ASan matrix passes 59/60 because the existing POC J Qt-painted-FPS gate remains below its unchanged 30 FPS threshold under instrumentation. Hosted evidence from the CI/GitFlow work remains separately reported: Ubuntu Release passes 55/55, while Ubuntu ASan, macOS Release/ASan, and hosted Windows Release/ASan retain recorded failures. No complete cross-platform matrix is claimed. Focused DPE-ARCH-0010 lifecycle, DPE-ARCH-0011 external Rider source-authoring, DPE-ARCH-0012 managed GameObject-controller/runtime-transform, DPE-ARCH-0013 configurable input-map/rebinding, and DPE-ARCH-0014 Project Hub/asset/Hierarchy/multi-Inspector evidence remains green. The refreshed 189-record production-style bundle hash-verifies and passes its packaged MonoGame self-test. ADR-0006 handle-pinning/noncooperating-writer limits remain open. No POC/ADR/slice/release gate is promoted, and KNI remains experimental. The repository contains the architecture prototypes, portable native core, managed contracts and C ABI interop, distinct preview/play MonoGame and KNI workers, deterministic scene-v3 persistence, linked-prefab and physics foundations, external Python automation, a writable sample project, and the Qt 6.11 editor.

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

The directly runnable executable is `out\product\windows-x64\DragonPixelEditor\DragonPixelEditor.exe`. Its adjacent bundle includes Qt plugins, native runtime libraries, MonoGame/KNI workers, the isolated Tiled importer worker, contracts, Python tools, schemas, a sample, and a SHA-256 file manifest. This is a production-style developer bundle for rapid local testing; it is not yet the signed, clean-machine-validated POC R release package.

The editor opens the 2D/3D sample in authoring mode. The Hierarchy, typed Inspector, Project Explorer, Scene View, and structured Console are model-backed full-grid docks: there is no reserved center obstruction, and panels can split, tab, float, move to any dock area, reset deterministically, and restore versioned per-user layouts. GameObject presets, multi-selection, component editing, direct New C# Script/New C++ Component attachment, asset drag/drop, Undo/Redo, workspaces, picking, camera controls, and Move/Rotate/Scale gizmos route authoritative edits through validated scene transactions. Contained C#/C++ component sources appear in Project Explorer; double-clicking one or choosing the Inspector component-card Rider edit action regenerates a disposable `.dragonpixel/Ide/Rider` solution and opens the selected source in JetBrains Rider without loading project code into the editor process.

A dedicated preview worker consumes the editor-owned mirror and reloads revisions in place. Play launches a separate MonoGame or experimental KNI worker from an immutable snapshot. Simulate Preview runs an isolated native Box2D/Jolt world; Play always owns a disposable runtime world. Stop destroys runtime state without writing simulated transforms into the authoring scene. Both framework adapters use real graphics-device render targets, revisioned local IPC, ID-buffer picking, and BGRA8 shared frames.

## Tilemap authoring workspace

Choose **Assets > Create TileSet from PNG...** to turn a sprite sheet into a contained Texture and indexed TileSet. Select a PNG, name the TileSet, set cell size, margins, spacing, pixels-per-unit, and optional rectangular collision, then confirm the slicing preview. PNGs are validated by their actual image content, so a valid temporary or downloaded PNG without a filename extension is accepted and copied into the project with a normalized `.png` name; a missing or mislabeled non-PNG file is rejected before any asset is written.

In the 2D workspace, select a TileSet in Project Explorer and choose **Create Tilemap from Selected TileSet...** from the Assets menu or its Project context menu. The editor atomically creates an empty reusable Tilemap with one layer and opens it in the Tile Palette. Drag the Tilemap from Project Explorer into Scene View or the Hierarchy to create a Transform + Tilemap2D GameObject, or drag another compatible Tilemap onto the Inspector's Tilemap field to reassign it.

The Tile Palette displays atlas-cropped nearest-neighbor art and provides Paint, Erase, Rectangle, Fill, Eyedropper, and Select tools; flip-X, flip-Y, and quarter-turn brush transforms; zoom; Undo/Redo/Save; and stable-ID layer add, rename, visibility, reorder, and removal controls. Selecting the matching Tilemap GameObject enables direct painting in the 2D Scene View through its complete parent/local Transform. The highlighted cell follows TileSet cell dimensions and pixels-per-unit. Strokes interpolate without gaps, Escape cancels the active transaction, and 3D or Play mode cannot mutate the authoring Tilemap. Tile changes refresh the immutable preview through a bounded coalesced path, and the existing scene-plus-dirty-Tilemap save remains recoverable.

The current editor intentionally supports static orthogonal Tilemaps with one resolved TileSet. Isometric, hexagonal, staggered, multiple-TileSet, rule, terrain, animated, procedural, 3D, and reimport/merge workflows remain future focused features. See the [tilemap authoring workspace plan](docs/Plans/Dragon%20Pixel%20Engine%20Tilemap%20Authoring%20Workspace%20Plan.md) for exact scope, evidence, and open gates.

## Tiled tilemap import

With a project open, choose **Assets > Import Tiled Tilemap...** (`Ctrl+Alt+T`), select a `.tmj` or JSON-format Tiled map, and enter its pixels-per-unit scale. A successful import creates a contained atlas texture, reusable TileSet, and tilemap with version-3 asset sidecars, refreshes Project Explorer, and opens the map in the existing Tile Palette.

The current deliberately small compatibility slice accepts orthogonal maps with finite numeric arrays or infinite numeric chunks, exactly one inline or external JSON atlas TileSet, and one relative PNG atlas. It preserves layer order, names, visibility, negative coordinates, and all eight orthogonal Tiled GID flip/rotation combinations. TMX/TSX XML, multiple TileSets, object/image/group layers, encoded or compressed layer data, isometric/hex/staggered maps, image collections, animation/rule/terrain data, collision-object conversion, and reimport are not yet supported. Unsupported input is rejected before any project file is published. See the [Tiled import feature plan](docs/Plans/Dragon%20Pixel%20Engine%20Tiled%20Tilemap%20Import%20Plan.md) for the exact contract and evidence.

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
