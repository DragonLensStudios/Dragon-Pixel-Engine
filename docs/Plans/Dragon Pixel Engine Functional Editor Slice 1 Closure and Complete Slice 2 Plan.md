# Dragon Pixel Engine Functional Editor: Slice 1 Closure and Complete Slice 2

> **Plan status:** In progress  
> **Architecture revision:** `DPE-ARCH-0006`  
> **Started:** 2026-07-24  
> **Last updated:** 2026-07-25  
> **Repository:** `C:\Projects\Github\Engines\Dragon Pixel Engine`  
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine`

## Objective

Deliver a genuinely user-operable Qt editor in which authored GameObjects drive the real MonoGame/KNI viewport and isolated play session. Close every original Slice 1 gate and deliver the complete Slice 2 authoring experience: project browsing, typed inspection, commands and undo/redo, drag/drop, multi-selection, unified 2D/3D scene manipulation, linked nested prefabs, asset previews, baseline lighting, and live 2D/3D physics.

“GameObject” is the editor-facing term. The portable model remains `EntityRecord`.

## Delivery Sequence

### 1. Governance and existing Slice 1 gate

- Reconcile the externally updated Notes file into the repository mirror and verify all paired Markdown files byte-for-byte.
- Store and maintain this plan in `docs/Plans` and the external `Plans` directory.
- Advance Design, Prompt/Result, and `AGENTS.md` together to `DPE-ARCH-0006`; preserve the immutable Original Prompt.
- Record macOS honestly: 14 of 15 tests pass, while `poc_b.worker_viewport` fails the 1280×720 performance gate.
- Fix the Windows launcher path-with-spaces issue with `ProcessStartInfo.ArgumentList` and use a writable sample under `out/dev`.
- Replace relative worker sleeps with absolute deadlines and bounded coarse-wait/spin completion.
- Make POC B correlate input through the presented frame. Require 1280×720, at least 30 presented FPS, and median input-to-present below 100 ms on every baseline platform.
- Run MonoGame and KNI measurements independently so one failure never suppresses the other adapter’s evidence.
- Re-run Release and native AddressSanitizer tests on Windows x64, macOS arm64, and Ubuntu x64.

### 2. Architecture acceptance and prototypes

Update ADR-0001, ADR-0002, ADR-0004, ADR-0005, ADR-0006, and ADR-0007. Add:

- ADR-0008: command transactions, validation, undo/redo, dirty state, and automation.
- ADR-0010: worker protocol, real frame production, picking, and latency correlation.
- ADR-0012: Box2D/Jolt ownership, fixed-step physics, and the C ABI extension.
- ADR-0013: linked nested prefab identity, overrides, rebasing, and recovery.

Add bounded evidence documents and tests for:

- POC E: real MonoGame and KNI offscreen rendering, readback, picking, scene-dependent pixels, resize, and performance.
- POC F: three-level nested prefabs, overrides, apply/revert/unpack, missing-source recovery, and deterministic migration.
- POC G: Box2D/Jolt world ownership, fixed stepping, C ABI batches, contacts, transforms, and crash cleanup.
- POC H: Qt Test interaction coverage for menus, editors, dialogs, drag/drop, keyboard navigation, focus, and accessibility.

ADRs move from Proposed only when their evidence passes on all three baseline platforms.

### 3. Document, project, and command foundations

- Split `EditorWindow` into Project, Scene, Selection, Command, Metadata, Asset, RuntimeSession, Diagnostics, Workspace, Prefab, and AssetPreview services.
- Make CommandService the only authoritative mutation route for panels, gizmos, drag/drop, prefabs, automation, and migration.
- Implement validation/dry-run, compound transactions, before-images, continuous-edit coalescing, undo/redo, clean savepoints, dirty state, and audit correlation.
- Implement preset creation, duplication with reference remapping, subtree deletion, reparent/reorder, multi-edit, transform deltas, component changes, asset assignment, and prefab commands.
- Deletion previews descendants and incoming references, confirms once, preserves dangling UUIDs as visible diagnostics, and restores IDs, ordering, and opaque data on Undo.
- Build and validate a candidate project/session before replacing the active one. Open, close, reload, and exit use Save–Discard–Cancel prompts.
- Replace flat discovery with a manifest-rooted index of folders, scenes, prefabs, and assets, including duplicate-ID, missing-source, invalid-root, cycle, dependency, and external-change diagnostics.

### 4. Runtime rendering and physics

- Replace the synthetic continuous renderer with real MonoGame/KNI `GraphicsDevice`, `RenderTarget2D`, `SpriteBatch`, vertex/index buffers, depth testing, effects, and BGRA8 readback over the existing shared-memory boundary.
- Keep MonoGame and KNI in separate worker assemblies behind the shared `IFrameworkAdapter` lifecycle.
- Render flattened scene snapshots containing enabled transforms, cameras, sprites, meshes, materials, Rotators, ambient light, one or more directional lights, and up to four point lights. Shadows remain excluded.
- Edit mode uses the editor camera. Play requires one enabled primary scene camera and diagnoses missing or ambiguous primaries.
- Keep graphics work on the framework graphics thread, IPC asynchronous, and preview snapshot reloads revision-correlated and in-place.
- Add editor camera, selection, resize, render revision, diagnostics subscription, and ID-buffer picking to the negotiated worker protocol.
- Acceptance paths must identify the real adapter, device, and backend; no synthetic fallback may satisfy them.

Physics:

- Pin [Box2D 3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1) and [Jolt 5.6.0](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0) through vcpkg.
- Put both behind engine-owned framework-neutral native interfaces. Backend types never enter Core, Scene, C ABI records, managed contracts, or saved JSON.
- Each native runtime world owns its 2D and 3D physics state, UUID mappings, transforms, and event queues.
- Step at 60 Hz with no more than four catch-up ticks, four Box2D solver substeps, and one Jolt collision step; report and discard excess accumulated time.
- Edit preview displays colliders without stepping. Simulate uses an isolated preview world. Play always simulates. Stop destroys runtime worlds and never mutates authoring state.
- Support `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `RigidBody3D`, `BoxCollider3D`, and `SphereCollider3D` with body mode, damping, gravity, velocity, sensor, density, friction, restitution, CCD, layer, and mask.
- Exclude joints, characters, vehicles, soft bodies, static mesh colliders, and runtime scale animation.

### 5. Complete Qt authoring UX

- Use service-backed `QAbstractItemModel` views with stable panel IDs instead of panel-owned tree/list state.
- Hierarchy: filtering, ordered multi-selection, inline rename/active state, keyboard navigation, drag reparent/reorder, duplication, subtree deletion, and context menus.
- GameObject presets:
  - Empty: Transform.
  - Sprite: Transform plus Sprite and a compatible selected/default asset.
  - Cube: Transform plus Static Mesh and Material.
  - Camera: Transform plus Camera with valid defaults.
  - Light: Transform plus directional Light.
- Each preset is one transaction, receives valid defaults, is created beneath the single selected parent or at scene root, and becomes selected.
- Inspector: scrollable component cards and typed drawers for bool, integer/number, string/enum, Vector2/3, quaternion-as-Euler, color, entity references, and filtered asset references.
- Support component enable/add/remove/reorder, mixed multi-selection values, inline validation, first-party custom drawers, specialized Transform/Collider drawers, and visible read-only opaque records.
- Project Explorer: folders, scenes, prefabs, assets, search/type filtering, importer/dependency status, refresh/watch, async sprite/mesh thumbnails, scene opening, asset inspection, compatible assignment, and drag-to-scene creation. General filesystem management remains deferred.
- Scene View: unified 2D/3D mode, orthographic pan/zoom, perspective orbit/pan/fly, focus selection, click picking, outlines, grid, camera/light/collider overlays, Move/Rotate/Scale gizmos, local/global mode, and snapping.
- Gizmos use preview transactions: mouse-down captures originals, movement updates only the preview mirror, release commits one undo item, and Escape restores the original values.
- Console: structured rows with timestamp, severity, subsystem, worker/session, correlation ID, context, filtering, clear/copy/export, and entity/asset navigation.
- Workspaces: built-in 2D, 3D, and Debug layouts; View-menu restoration; reset/save layout; per-user camera and layout state in platform application data.
- Correct text encoding, focus order, shortcuts, accessible names/roles/actions, high-DPI behavior, high contrast, and keyboard-only workflows.

### 6. Linked nested prefabs

- Store locally owned entities separately from linked prefab instances. Materialize links for authoring and flatten them for runtime.
- A prefab owns stable local UUIDs, one root, direct nested instances, a canonical SHA-256 source revision, ordered entity/component records, and dependencies.
- An instance stores instance/source IDs, source revision, placement parent, stable nested-path/entity mappings, normalized overrides, and last-known resolved fallback entities.
- Overrides cover entity add/remove/rename/enable/reparent/reorder, component add/remove/reorder/enable, and property assignment.
- Refresh preserves surviving mappings, allocates new IDs in the rebase command, reapplies overrides, and retains unresolved overridden data with diagnostics.
- Reject direct/indirect dependency cycles and enforce depth/entity expansion limits.
- Implement Create from Selection, Instantiate, Apply at an explicit nesting level, Revert Selected/All, Repair/Rebase, Unpack, and Unpack Completely.
- Reject reusable prefab references to scene-local entities outside the prefab.
- Missing/incompatible sources disable Apply/Revert while retaining fallback data; Unpack Completely remains available.
- Prefab create/apply is the only scoped asset-file creation/update exception and uses atomic multi-document save, backups, and undo data.

## Public Contracts and Formats

| Boundary | Planned change |
| --- | --- |
| `dpe.project` | Version 2 adds explicit sorted scene roots; v1 derives them from startup scene |
| `dpe.scene` | Version 3 adds sibling ordering, physics settings, and `prefabInstances` |
| `dpe.prefab` | Version 1 linked-prefab document with revisions, local entities, nested instances, mappings, overrides, and fallbacks |
| `dpe.asset` | Version 2 adds sorted direct dependencies and importer/preview diagnostics |
| Component metadata | Version 3 adds defaults, constraints, enum choices, reference filters, editor hints, and drawer keys |
| `DragonPixel.Contracts` | Snapshot v3 adds enabled state, flattened prefab entities, asset bindings, cameras/rendering, physics DTOs, and revision correlation |
| `IFrameworkAdapter` | Real lifecycle plus viewport, picking, and frame-output contracts |
| Worker JSON-RPC | Additive resize, atomic reload, simulate-preview, pick, diagnostics, camera, and revision methods |
| Shared frame transport | Version 2 header adds snapshot, camera, command, and frame revisions while retaining negotiated v1 compatibility |
| `dpe_api_v1` | ABI minor 1 plus negotiated `dpe_physics_api_v1` extension |
| Editor commands | Stable typed/versioned hierarchy, component, multi-edit, gizmo, asset, prefab, and physics envelopes |

All migrations must be explicit, deterministic, atomic, recovery-backed, and preserve unknown component and prefab data.

## Verification and Acceptance

- Native/domain coverage: command validation and rollback, undo/redo, hierarchy cycles/order, duplication, subtree deletion, incoming references, metadata constraints, deterministic formats, atomic recovery, and opaque preservation.
- Prefab coverage: three-level nesting, duplicate nested sources, stable mappings, every override type, rebase conflicts, target-level apply, revert, both unpack modes, cycles, missing/newer sources, fallback recovery, dependencies, and multi-document failure.
- Physics coverage: 2D/3D drop/rest, body modes, sensors, masks, CCD, contacts, raycasts, pause/resume, catch-up limits, transform mapping, same-binary replay, ABI ownership, and crash cleanup.
- Renderer coverage: add/move/color/disable/delete operations change real device pixels and picking IDs through both adapters.
- Qt Test coverage uses real clicks, keys, dialogs, inline editors, drag/drop, gizmos, menus, focus, and action state through injectable prompt seams.
- End-to-end disposable-project coverage: preset creation, managed/native component edits, assignment, nested prefabs, physics, save/reopen, preview/play isolation, adapter switching, crash recovery, layout restore, console navigation, and proof of no source-tree writes.
- Run Release and native AddressSanitizer matrices on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64.
- Slice 1 closes only when its original cross-platform gates pass.
- Slice 2 closes when a technical designer can assemble, validate, run, save, reopen, and revise one small 2D and one small 3D scene—including nested prefabs and physics—without editing JSON.

## Windows and Ubuntu Functional Editor Checkpoint

This 2026-07-25 checkpoint records the implemented Windows editor workflow and passing Windows and Ubuntu registered test matrices. It does not close Slice 1, promote KNI from experimental status, establish the current POCs E-H or sanitizer matrix on macOS, or satisfy the complete Slice 2 designer acceptance scenario.

### Implemented and verified on Windows

- The managed launcher handles repository paths containing spaces with `ProcessStartInfo.ArgumentList`, and development/manual runs use a writable disposable project copy under `out/dev` instead of the tracked sample.
- POC B uses absolute frame deadlines and revision-correlated input-to-present measurements for MonoGame and KNI independently. The current 1280x720 Windows run records 60.0 FPS and 15.6 ms median latency for MonoGame, and 40.0 FPS and 46.6 ms for KNI; both meet the unchanged Windows gate of at least 30 FPS and below 100 ms.
- POC E exercises scene-driven device rendering, readback, revision correlation, picking, and resize through both adapters. The current individual Windows runs record 60.1 FPS and 15.7 ms for MonoGame, and 40.7 FPS and 46.7 ms for KNI.
- The current targeted Ubuntu POC B/POC E run passes 7/7 tests. The combined worker run records 61.7 FPS and 16.3 ms for MonoGame, and 32.3 FPS and 55.1 ms for KNI. The independent adapter run records 61.6 FPS and 16.4 ms for MonoGame, and 33.3 FPS and 55.2 ms for KNI.
- Rendering now performs actual one-pixel ID picking on the framework graphics thread instead of reading back the full ID render target. Frame consumption keeps a persistent seqlock reader and derives presented FPS from frame timestamps.
- The Ubuntu path includes a position-independent-code clean-link fix for native Unix targets, propagation of the sanitizer runtime into managed child processes, and the required `pkg-config` package in the checked-in Docker environment.
- The Windows editor exposes the manifest-rooted indexed Project Explorer, asset preview/status information, typed Inspector editing, command validation, dirty/savepoint behavior, undo/redo, hierarchy/preset transactions, multi-selection, gizmo preview/commit/cancel behavior, structured Console data, isolated preview/play sessions, physics simulation controls, and linked-prefab editor workflows covered by the registered tests.
- Automated Windows and Ubuntu coverage includes the native and managed Box2D/Jolt facade and ABI paths, command validation, linked-prefab core and editor workflows, project indexing/candidate open, asset previews, structured Console, real framework rendering, and Qt interaction tests.

### Current automated platform record

| Matrix | Result | CTest time | Disposition |
| --- | ---: | ---: | --- |
| Windows 11 x64 strict Release, MSVC | 36/36 passed | 118.39 s | Windows implementation verified |
| Windows 11 x64 MSVC AddressSanitizer | 36/36 passed | 134.93 s | Windows sanitizer implementation verified |
| Ubuntu 24.04 x64 strict Release, Clang | 36/36 passed | 102.20 s | Ubuntu implementation verified within registered coverage |
| Ubuntu 24.04 x64 Clang AddressSanitizer | 36/36 passed | 101.97 s | Ubuntu sanitizer implementation verified within registered coverage |

### Manual Windows QA record

- Opened a disposable `out/dev` sample rather than the tracked source sample.
- Confirmed indexed Project Explorer contents, previews, and importer/dependency status presentation.
- Confirmed typed Inspector presentation and editing.
- Created an Empty GameObject as a child in one transaction, observed the dirty marker, then used `Ctrl+Z` and confirmed exact clean-state restoration.
- Started and stopped isolated preview physics simulation without applying runtime state to authoring data.
- Displayed actual MonoGame and KNI Preview and Play output; exercised KNI Pause and Stop.
- Confirmed the manual session produced no writes in the tracked sample tree.

### Gates that remain open

- Repair and pass the unchanged macOS POC B viewport gate.
- Execute the current POCs E-H and Release/native-sanitizer matrix on macOS arm64; the previous macOS run remains 14/15 with the 1280x720 POC B failure.
- Complete the full nested-prefab newer-source, deep-nesting, override, recovery, and failure matrix.
- Broaden Qt interaction, accessibility, keyboard-only, high-DPI/high-contrast, and complete 2D/3D designer workflow evidence.
- Finish splitting behavior out of the still-monolithic `EditorWindow` into the accepted editor services.
- Keep ADRs Proposed and KNI experimental until their documented three-platform and broader compatibility gates pass.

## Assumptions and Exclusions

- KNI remains visibly experimental until its complete compatibility/content/input/packaging/platform matrix passes.
- Jolt promises same-binary repeatability for this build; cross-platform bitwise lockstep remains a later ADR and uses tolerance-based checks here.
- Component identity is `(entity UUID, component type UUID)`, with one component of a given type per entity.
- Runtime snapshots flatten prefab provenance.
- General project creation/templates, arbitrary import, filesystem rename/delete, plugin distribution, packaging/updating, Unity/Unreal work, and migration UI remain Slice 3 or later.
- No shadows, skeletal animation, advanced materials, joints, terrain, global illumination, ray tracing, VR, or other AAA features are included.

## Execution Record

| Date | Status | Evidence / decision |
| --- | --- | --- |
| 2026-07-24 | Started | User approved implementation of the complete plan. |
| 2026-07-24 | Completed | Reconciled the newer external Notes intake into the repository copy; all four required root document pairs matched by SHA-256 before architecture work began. |
| 2026-07-24 | Completed | Published synchronized `DPE-ARCH-0006` Design/Prompt/AGENTS governance and Proposed ADR updates/additions through ADR-0013; the recursive audit passed 27 UTF-8/LF mirrored Markdown pairs. |
| 2026-07-24 | Completed | Windows toolchain smoke passed with MSVC 19.44/v143 14.44.35207, CMake 4.4.0, Ninja 1.13.2, .NET SDK 10.0.203, vcpkg 2025-11-19, and dynamically linked Qt 6.11.1; native Qt, ASan, and managed smoke tests passed. |
| 2026-07-24 | In progress | Slice 1 gate repairs and native/editor/runtime implementation are underway in parallel. |
| 2026-07-25 | Windows verified | Strict Release passed 36/36 tests in 118.39 s and MSVC AddressSanitizer passed 36/36 in 134.93 s. POC B and POC E independently met the Windows 1280x720 throughput and input-to-present gates for MonoGame and KNI. |
| 2026-07-25 | Windows verified | Manual QA on the disposable `out/dev` sample confirmed indexed assets/previews/status, typed inspection, one-transaction child Empty creation, dirty marker plus `Ctrl+Z` clean restoration, isolated simulation, real MonoGame/KNI Preview and Play, KNI Pause/Stop, and no tracked sample writes. |
| 2026-07-25 | Ubuntu verified | Strict Release passed 36/36 tests in 102.20 s and Clang AddressSanitizer passed 36/36 in 101.97 s. Targeted POC B/E passed 7/7 with both adapters above 30 FPS and below 100 ms median input-to-present. |
| 2026-07-25 | Completed | Delivered graphics-thread one-pixel picking, persistent seqlock frame consumption, timestamp-based presented FPS, Unix PIC clean linking, sanitizer runtime propagation to managed child workers, and Ubuntu Docker `pkg-config` provisioning. |
| 2026-07-25 | Completed | Closed the paused-click regression: paused workers service retained ID-target picks on the graphics thread without publishing a frame, timeout failures return a structured error, and POC B proves picking does not advance the frozen sequence. |
| 2026-07-25 | In progress | Slice 1 and Slice 2 remain open for the prior macOS POC B failure, absent current macOS POCs E-H/sanitizer evidence, the complete prefab matrix, broader Qt/accessibility/designer scenarios, known implementation gaps, and the `EditorWindow` service split. ADRs remain Proposed and KNI remains experimental. |
| 2026-07-25 | Documented | Added the mirrored Windows functional-editor acceptance audit and synchronized this execution record. Both changed document pairs pass pair-level SHA-256, strict UTF-8/no-BOM, LF-only, local-link, and whitespace checks. The repository-wide mirror validator remains an integration handoff while concurrent ADR, prototype-evidence, and Prompt/Result updates are temporarily unsynchronized. |
| 2026-07-25 | Documented | Refreshed both mirrored records with the final Windows/Ubuntu matrices, targeted Ubuntu POC metrics, delivered cross-platform fixes, and corrected remaining gates. Scoped mirror, encoding, link, and whitespace verification passes; the repository-wide validator remains temporarily blocked by concurrent ADR, prototype-evidence, and compliance-inventory mirror updates outside this work item. |

## Completion Checklist

- [x] Repair the required Notes mirror.
- [x] Create the paired execution plan.
- [x] Publish synchronized `DPE-ARCH-0006` Design, Prompt/Result, ADR index, and `AGENTS.md`.
- [x] Repair and verify the launcher, frame pacing, and correlated POC B gate on Windows.
- [ ] Close the existing Slice 1 POC B gate on macOS without reducing its thresholds.
- [x] Implement and pass the currently registered POCs E-H coverage on Windows and Ubuntu.
- [ ] Pass the current POCs E-H and sanitizer matrix on macOS arm64.
- [ ] Complete document/command/project foundations on all platforms; the Windows/Ubuntu command, index, candidate-open, and preview foundations pass, but the `EditorWindow` service split remains open.
- [x] Implement and verify real scene-driven framework rendering and picking on Windows and Ubuntu.
- [x] Implement and verify baseline Box2D/Jolt physics on Windows and Ubuntu within registered coverage.
- [ ] Complete Qt Slice 2 UX and designer/accessibility evidence; the current registered Windows/Ubuntu interactions pass and the Windows manual workflow is usable, but broader designer and accessibility flows remain open.
- [ ] Complete linked nested prefabs; Windows/Ubuntu core/editor workflows pass, but the full newer-source and deep-nesting recovery matrix remains open.
- [ ] Pass automated and designer acceptance matrices.
- [ ] Record final evidence and verify every mirrored Markdown hash.
