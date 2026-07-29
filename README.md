# Dragon Pixel Engine

Dragon Pixel Engine is pursuing the complete four-slice roadmap and design-defined `1.0.0` under the active mirrored master plan. Editor-library modularization PR #3, CI/GitFlow PR #4, isolated Tiled-import PR #5, and DPE-ARCH-0015 Tilemap PR #6 are merged into `develop`. The DPE-ARCH-0017 Project Window correction passes its changed aliases **3/3** on Windows Release in **340.03 seconds**, **3/3** under MSVC AddressSanitizer in **571.63 seconds** without a finding, and **3/3** on Ubuntu 24.04 Release in **48.25 seconds**. Three complete current-source Ubuntu runs are each **61/62** because of one sequence-sensitive crash-recovery allocator failure and two KNI POC J frame-throughput failures under the unchanged threshold, so draft PR #7 remains blocked. macOS is deferred for this feature and remains an open product gate; no complete cross-platform, POC, ADR, slice, release, or KNI production claim is made. KNI remains experimental.

## Development workflow

Feature work starts from current `develop`, uses one focused `feature/*` branch and a mirrored living plan, proceeds through small tested commits, and ends with aggregate diff review plus a draft pull request into `develop`. Multiple independent feature PRs may remain open and be reviewed manually at the same time. A genuinely dependent child may temporarily target one parent `feature/*` branch only when both plans and PR bodies identify the dependency, ownership, overlap, merge order, and retarget steps; the child cannot merge first and must be updated, retargeted to current `develop`, re-reviewed, and retested after its parent merges. Codex and contributors must read [AGENTS.md](AGENTS.md) and the [Development Constitution](docs/Development/Development%20Constitution.md); the reusable [feature-plan template](docs/Development/Feature%20Plan%20Template.md), [GitFlow handbook](docs/Development/GitFlow%20Handbook.md), [verification checklist](docs/Development/Verification%20Checklist.md), and [feature PR template](.github/PULL_REQUEST_TEMPLATE/feature.md) define the handoff.

## Project View workflow

The **Project** dock follows the supported structure and behavior of Unity 5.4's Project Window while retaining Dragon Pixel visuals and ownership. Favorites and the folder tree appear on the left; the active folder's immediate contents appear as scalable icons or a compact Name/Kind list on the right. The toolbar provides Create, search, type/status filters, Save Search, one/two-column layout, and lock; Back, Forward, Up, and clickable breadcrumbs provide stable navigation. Search is whitespace-AND with ORed `t:` types and Dragon Pixel `s:` statuses. Refresh restores valid folder, expansion, selection, splitter, layout, lock, and icon-size state as non-authoritative per-user settings without changing project files.

Supported OS files import into the validated visible folder. Project assets and folders can move between contained Project folders through `AssetService` even when no Scene is open; successful moves preserve stable IDs. Sprite, Tilemap, prefab, and compatible asset drops into Scene, Hierarchy, or Inspector continue through their existing validated command owners. Hierarchy-to-Project prefab creation remains Scene-dependent. Stale, cross-project, colliding, recursive, incompatible, and ambiguous mutations are rejected before authoritative writes. See the [Project View user guide](docs/Dragon%20Pixel%20Engine%20Project%20View%20User%20Guide.md) for the complete workflow and team GitFlow rules.

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

Choose **Assets > Create TileSet from Image...** to turn a PNG, JPEG, BMP, or GIF sprite sheet into a contained Texture and TileSet. Automatic, cell-size, and cell-count slicing share one preview and support offset, padding, empty-cell handling, pivot, pixels-per-unit, layout, and collision choices. Leave **Create a Tilemap and GameObject ready for painting** checked to atomically publish and open the Texture, TileSet, Tile Palette, Tilemap, assigned Tilemap2D GameObject, and optional TilemapCollider2D. Inputs are validated by decoded content rather than filename: valid extensionless PNGs remain accepted, while JPEG/BMP/GIF sources are normalized to real project PNG content. Missing, malformed, or unsupported formats such as SVG are rejected before publication.

For an existing TileSet, double-click it or choose **Create Tilemap from Selected TileSet...** from the Assets menu or its Project context menu. The editor asks for the map name, atomically creates an empty reusable Tilemap and palette, opens the workspace, and completes the same paint-ready GameObject workflow. It reuses only a selected Tilemap2D GameObject whose Tilemap reference is empty; it never silently replaces a populated reference. Project Explorer drag/drop accepts compatible images, TileSets, palettes, Tilemaps, and prefabs, and a Tilemap asset can still be assigned explicitly through the Inspector.

The Tile Palette workspace has explicit Active Palette, Active Target, and active-layer selectors, target following and pinning, a neutral multi-cell brush, palette organization, brush properties, TileSet definition editing, grid and renderer settings, and selection editing. Paint, Erase, Box, Line, Flood, Pick, Select, and Move tools have familiar-letter, legacy-number, and validated per-user Custom shortcut profiles. Basic, Animated, Rule, Rule Override, and losslessly preserved Custom tiles are represented in TileSet v2; Random Selection, Group Stamp, metadata-safe GameObject, and worker-only Custom Extension brushes extend the basic brush. Safe re-slicing preserves stable tile IDs and blocks removal of referenced definitions. Rectangular, hex point-top, hex flat-top, isometric, and isometric Z-as-Y layouts use the same portable projection, picking, and collision owner.

TileSet, palette, Tilemap, and scene changes participate in validated transactions and recoverable atomic saving. A persistent sharing lock retains the previous valid files and unsaved editor state. Grid and Sprite Outline colliders lower to neutral Box2D shapes, with safe opt-in rectangular Grid composite merging. Runtime snapshot v5 and both framework adapters consume the expanded contracts; KNI remains experimental. PR #6 remains draft while the required hosted cross-platform acceptance gates are incomplete. See the [Tilemap Editor user guide](docs/Dragon%20Pixel%20Engine%20Tilemap%20Editor%20User%20Guide.md) for the complete workflow and explicit limits, and the [tilemap authoring workspace plan](docs/Plans/Dragon%20Pixel%20Engine%20Tilemap%20Authoring%20Workspace%20Plan.md) for implementation evidence and open gates.

## Tiled tilemap import

With a project open, choose **Assets > Import Tiled Tilemap...** (`Ctrl+Alt+T`), select a `.tmj` or JSON-format Tiled map, and enter its pixels-per-unit scale. A successful import creates contained atlas textures, TileSets, a palette, and a Tilemap with version-3 asset sidecars, refreshes Project Explorer, and opens the paint-ready workspace.

The importer accepts finite numeric arrays or infinite numeric chunks, multiple inline or external JSON atlas TileSets, orthogonal, isometric, staggered, and hexagonal orientations, tile animation, and conservatively representable terrain/Wang rules. Layer order, names, visibility, negative coordinates, and Tiled GID transformations are preserved. TMX/TSX XML, encoded or compressed layers, object/image/group layers, image collections, unsupported Wang semantics, and reimport remain rejected before publication. See the [Tiled import feature plan](docs/Plans/Dragon%20Pixel%20Engine%20Tiled%20Tilemap%20Import%20Plan.md) for historical PR #5 evidence and the parity follow-up disposition.

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
