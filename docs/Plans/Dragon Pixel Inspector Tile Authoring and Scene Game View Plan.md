# Dragon Pixel Inspector, Tile Authoring, and Scene/Game View Plan

> **Plan status:** Superseded for active tracking; remaining POCs I-L and Slice 2 gates carried forward
> **Accepted architecture:** `DPE-ARCH-0008`
> **Superseded for active tracking:** 2026-07-25
> **Active master plan:** [Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan](Dragon%20Pixel%20Engine%20Slices%201-4%20Version%201.0%20Completion%20Plan.md)

## Summary

Deliver a Unity-familiar but metadata-driven Inspector, executable custom C#/C++ components, nested and polymorphic object editing, an orthogonal 2D tile-authoring system, and independently dockable Scene and Game views.

The editor remains authoritative: all changes use validated commands, Preview/Play code runs only in workers, and no known component requires editable raw JSON.

## Implementation Changes

### 1. Governance and baseline repair

- Treat the external `Dragon Pixel Engine Notes.md` as authoritative and mirror its latest request into the repository before work begins.
- Store this plan identically under repository `docs/Plans` and the external `Plans` directory.
- Advance Design, Prompt/Result, `AGENTS.md`, affected ADRs, risks, roadmap, and acceptance records together to `DPE-ARCH-0008`.
- Update ADR-0005/0007/0010/0014 and add ADRs for tile authoring and multi-viewport rendering.
- Diagnose the current `dpe_physics_tests.exe` relink interruption and restore a clean Windows Release/ASan baseline before feature implementation.

### 2. Unity-familiar structured Inspector

- Replace the two-column `QTreeView` Inspector with a service-backed scrollable panel containing a GameObject header, collapsible component cards, a searchable Add Component popup, and specialized first-party drawers.
- Present booleans, bounded numbers, enums, vectors, Euler rotations, colors, strings, assets, entities, components, collections, and nested objects through native Qt controls. Known fields never expose editable JSON.
- Preserve a read-only diagnostic/raw-data foldout only for missing or incompatible opaque records.
- Make mixed values first-class and mutation-free until the user explicitly commits a value.
- Coalesce continuous edits into one Undo item and keep all component actions transaction-backed.
- Add an `IPropertyDrawer` registry. Project metadata may select built-in/declarative drawers; arbitrary project editor widgets remain outside the editor process.

### 3. Nested objects, interfaces, and executable custom components

- Complete metadata format v4 with contracts, object types, recursive value shapes, component policy/category/language/source/module fields, and deterministic C#/C++ generator parity.
- Support fixed and nullable nested objects, reorderable lists, string-key dictionaries, polymorphic inline values, and interface-filtered entity/component references.
- Serialize by stable IDs, reject inline cycles, and route nested/collection/reference operations through validated path-based commands.
- Implement project format v3 `componentRoots` and validated `*.dpecomponents` discovery without loading code into the editor.
- Add atomic Create C# Component and Create C++ Component workflows plus an explicit Build Components action.
- Build outputs and module manifests live beneath `.dragonpixel/Cache`; successful builds restart workers, while failed/stale modules remain editable with diagnostics.
- Execute generated C# factories and `dpe_component_plugin_v1` C++ modules only inside workers. The editor never reflects over or maps project binaries.

### 4. Separate Scene and Game views

- Refactor the combined viewport into independently dockable Scene and Game panels, tabified by default but usable side by side, with per-user state.
- Scene retains the editor camera, 2D/3D modes, picking, gizmos, overlays, drag/drop, snapping, and tile painting.
- Game continuously shows the unique enabled primary camera outside Play and the isolated Play worker during Play; Stop restores the live preview.
- Route Play input only through the focused Game view and neutralize it on focus loss, Pause, Stop, crash, or capture release.
- Extend worker rendering to named outputs with independent sizes, camera sources, revisions, shared-memory mappings, and picking correlation.
- Add aspect, fit/scale, diagnostic, adapter, and frame-statistic controls to Game view.

### 5. Orthogonal tile-authoring MVP

- Add deterministic `dpe.tileset` v1 and `dpe.tilemap` v1 formats with stable tile IDs, ordered layers, and sparse 32x32 chunks.
- Add `Tilemap2D` and optional `TilemapCollider2D` components; scenes reference reusable tilemap assets.
- Add a contained PNG TileSet wizard plus a Tile Palette with paint, erase, rectangle, flood fill, eyedropper, selection, layer, and zoom controls.
- Route strokes through preview transactions so release creates one Undo item and Escape restores original chunks.
- Track tilemaps as dirty documents in atomic save/recovery workflows.
- Flatten resolved tile resources and generated collision records into runtime snapshot v4.
- Batch tiles through MonoGame/KNI and lower tile collisions to static Box2D colliders owned by the tilemap entity.
- Preserve unresolved data and expose repair diagnostics for missing or externally changed dependencies.

## Public Contracts

- `component-metadata` v4 gains component policies, module identity, object contracts/types, recursive shapes, and interface-filtered references.
- `dpe.project` v3 gains contained component roots.
- `dpe.tileset` v1 and `dpe.tilemap` v1 become new durable asset formats.
- `DragonPixel.Contracts.SceneSnapshot` v4 adds immutable asset bindings, flattened tiles, and generated tile collisions; saved `dpe.scene` remains version 3.
- Worker JSON-RPC becomes view-ID-aware.
- Editor commands gain nested paths, collections, polymorphic replacement, tile chunk edits, component reset/reorder, and atomic multi-document transactions.
- Managed and native modules expose equivalent worker-only factory, lifecycle, serialization, diagnostics, input, and registration boundaries.

## Verification and Acceptance

- Inspector tests cover drawers, mixed values, nested/collection/polymorphic values, interface filtering, references, validation, Undo/Redo, opaque records, accessibility, and keyboard use.
- Custom-code tests generate, build, execute, crash, restart, and reload one C# and one C++ module on every platform and prove editor-process exclusion.
- View tests prove independent simultaneous frames, primary-camera diagnostics, Scene interaction during Play, Game-only input, Stop isolation, and crash recovery.
- Tile tests cover PNG containment, slicing, stable IDs, every tool, chunk boundaries, layers, Undo/Redo, save/reopen, missing assets, external changes, adapter pixels/picking, and Box2D collision.
- Run public Qt interactions plus complete Release and native ASan matrices on Windows 11 x64, Ubuntu 24.04 x64, and macOS 14+ arm64 without reducing existing gates.
- Acceptance requires building C# and C++ components with nested/interface data, painting a colliding tilemap, viewing Scene and Game simultaneously, playing with input, and losslessly saving/reopening without editing JSON.

## Assumptions and Exclusions

- Initial tiles are static orthogonal 2D tiles. Isometric, hex, rule tiles, animation, terrain, and 3D tilemaps are deferred.
- PNG sprite-sheet intake is narrow; general asset import/rename/delete remains later work.
- Interface fields are portable data contracts and filtered references, not cross-language object pointers or inheritance.
- Project-authored Inspector widget code is deferred.
- KNI remains experimental until its full compatibility matrix passes.

## Execution Record

### Windows implementation result — 2026-07-25

The registered Windows implementation scope is complete and green. The following production paths are present:

- Metadata-v4 and project-v3 validation; typed GameObject/component-card inspection; nested fixed/nullable objects, lists, dictionaries, polymorphic contract values, filtered references, mixed values, component policies, path commands, Undo/Redo, and read-only opaque diagnostics.
- Searchable Add Component plus Create C# Component, Create C++ Component, and Build Components workflows. Generated source and metadata use stable IDs. Build outputs are content-addressed under `.dragonpixel/Cache`; C++ builds bootstrap the installed Visual Studio toolchain even when the editor was not launched from a developer shell.
- Worker-only managed factories and native `dpe_component_plugin_v1` modules with lifecycle, immutable properties, input, diagnostics, exception containment, stale/missing-module fallback, and restart-based reload. The Qt editor only reads data manifests and never loads project binaries.
- Independently dockable Scene and Game panels with stable view IDs, independent frame mappings/sizes, live primary-camera Game preview, isolated Play replacement, Scene continuity during Play, focus-owned normalized Game input, and neutralization on focus loss, Pause, Stop, and crash.
- `dpe.tileset` v1 and `dpe.tilemap` v1 schemas and native document services; sparse deterministic 32×32 chunks; transactional paint, erase, rectangle, bounded fill, eyedropper, selection, layers, Undo/Redo, dirty tracking, atomic save/recovery, external-change diagnostics, and sample assets.
- A contained-PNG TileSet wizard with deterministic slicing/stable tile IDs and optional rectangular collision; runtime snapshot-v4 flattening; immutable embedded PNG bindings; device texture/source-rectangle tile drawing through MonoGame and KNI; and tile collision lowering through engine-owned Box2D data.
- `InputMotion2D` as the end-to-end portable input proof. A Play worker consumes `move.x`/`move.y`, changes runtime-only transforms and real device-produced pixels through both adapters, correlates the input revision, and accepts an explicit neutral action state without changing authoring data.
- Windows ASan runtime deployment is now automatic beside every instrumented native binary, including DLLs and native hosts launched by managed or Qt tests. This repairs the prior `dpe_physics_tests.exe`/sanitizer baseline and removes reliance on a developer-shell PATH at test time.

### Verification evidence

- Windows 11 x64 Release build: succeeded with zero managed warnings/errors.
- `ctest --preset windows-release --output-on-failure`: **42/42 passed in 183.73 seconds**.
- Windows MSVC AddressSanitizer build: succeeded; the final uninterrupted `ctest --preset windows-asan --output-on-failure` run passed **42/42 in 215.54 seconds** with no sanitizer report.
- POC E executes MonoGame and KNI independently and now includes action-to-device-pixel movement plus neutral-state correlation in addition to the unchanged 1280×720, at-least-30-FPS, and below-100-ms median latency gates.
- POC I registered tests prove generated C#/C++ builds, content-addressed cache reuse, managed/native factory loading, lifecycle/input delivery, missing-module diagnostics, and worker-only execution on Windows.
- POC H/public Qt interaction aliases pass in both Release and ASan, including the current Inspector, TileSet wizard/palette, Scene/Game, prompt, action-state, save/reopen, and worker-recovery coverage.
- After the final CMake target consolidation, one Release aggregate produced a non-diagnostic `poc_h.qt_interactions` failure after the identical `s2.editor_interactions` alias passed. The alias immediately passed verbosely, passed three consecutive standalone repeats, and both aliases then passed back-to-back in 88.37 seconds. No assertion or sanitizer report reproduced; retain this as test-flakiness monitoring rather than a closed defect.

### Remaining acceptance gates

This record does not promote Slice 1, Slice 2, KNI, or ADRs I/K/L to cross-platform acceptance. A current Ubuntu 24.04 x64 and macOS 14+ arm64 build/test run of the expanded 42-test suite is still required. macOS also retains its historical 1280×720 POC B throughput failure until a current run proves otherwise. The current named-view implementation uses separately supervised Scene-preview, Game-preview, and Play workers; POC L must still decide/validate whether one preview worker can own simultaneous outputs as described by ADR-0017. Complete screen-reader/high-contrast/manual designer review and distribution/LGPL review also remain open.

## Active-Tracking Handoff

`DPE-ARCH-0009` expands active delivery to Slices 1-4. This plan remains the durable DPE-ARCH-0008 implementation/evidence record. Its Ubuntu/macOS expanded matrices, POCs I-L, single-preview-worker topology decision, adapter tile-pixel/pick/collision evidence, designer/accessibility review, and distribution review remain open and are carried into the active master plan without being claimed complete.
