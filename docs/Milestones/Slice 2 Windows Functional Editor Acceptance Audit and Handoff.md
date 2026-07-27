# Slice 2 Windows Functional Editor Acceptance Audit and Handoff

> **Audit status:** In progress - Windows and Ubuntu registered matrices verified; macOS and complete Slice 2 acceptance open  
> **Recorded:** 2026-07-25  
> **Architecture baseline:** `DPE-ARCH-0006`  
> **Plan at time of audit:** [Dragon Pixel Engine Functional Editor: Slice 1 Closure and Complete Slice 2](../Plans/Dragon%20Pixel%20Engine%20Functional%20Editor%20Slice%201%20Closure%20and%20Complete%20Slice%202%20Plan.md)
> **Current active master plan (2026-07-25):** [Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan](../Plans/Dragon%20Pixel%20Engine%20Slices%201-4%20Version%201.0%20Completion%20Plan.md)
> **Platform evidence represented by this audit:** Windows 11 x64 and Ubuntu 24.04 x64 automated evidence; Windows manual QA

> **Evidence correction, 2026-07-25:** This is a historical 36-test checkpoint. Its tables labeled the revision echo as input-to-present, but that request did not change rendered state; those values are viewport-command-to-present transport evidence only. Current Windows work proves full-state action consumption through real MonoGame/KNI pixel and retained-ID pick displacement, but aggregate Qt key-event-to-first-reflecting-Qt-paint median/tail evidence remains open. The active master plan and current Design/Prompt Result supersede the counts and classifications below without erasing this checkpoint.

## Outcome

Dragon Pixel Engine has a user-operable Windows editor increment with indexed project browsing, typed component inspection, command-backed authoring and undo, real MonoGame/KNI viewport and play output, isolated physics simulation, asset previews, linked-prefab foundations, and tested Scene View gizmo transactions. The complete Windows and Ubuntu strict Release and native AddressSanitizer matrices each pass all 36 registered tests.

This is a Windows/Ubuntu implementation checkpoint, not Slice 1 or Slice 2 acceptance. The prior macOS evidence remains 14/15 with the 1280x720 POC B failure, and current macOS POCs E-H and sanitizer evidence are absent. KNI remains experimental, the affected ADRs remain Proposed, and the complete nested-prefab, Qt/accessibility, designer, and editor-service gates remain unfinished.

## Windows and Ubuntu Automated Evidence

| Matrix | Result | CTest time | Interpretation |
| --- | ---: | ---: | --- |
| Windows 11 x64 strict Release, MSVC | 36/36 passed | 118.39 s | Complete registered Windows Release suite passed |
| Windows 11 x64 MSVC AddressSanitizer | 36/36 passed | 134.93 s | Complete registered Windows sanitizer suite passed |
| Ubuntu 24.04 x64 strict Release, Clang | 36/36 passed | 102.20 s | Complete registered Ubuntu Release suite passed |
| Ubuntu 24.04 x64 Clang AddressSanitizer | 36/36 passed | 101.97 s | Complete registered Ubuntu sanitizer suite passed |

The 36-test registration covers these implementation areas:

- Physics and POC G: native world/facade, C ABI, managed interop, and worker physics.
- Command and prefab foundations: command validation, linked-prefab domain tests, POC F core tests, prefab editor service, and prefab editor workflows.
- Rendering and workers: POC B lifecycle/frame transport, POC E scene rendering for both adapters, direct MonoGame/KNI graphics, and Qt frame consumption.
- Project/editor services: project index, asset preview, candidate project model, structured Console, MonoGame/KNI editor workflows, crash recovery, and Python automation.
- POC H interaction: real Qt editor interactions, including transactional hierarchy/preset operations and Scene View gizmo preview, cancel, commit, multi-selection, local/global orientation, snapping, and focus.
- Existing Slice 1 contracts: native core, managed contracts/worker, POCs A/C/D, serialization, scanner integrity, and automation boundaries.

## Performance and Latency Evidence

The current 1280x720 measurements use revision-correlated input-to-present timing. MonoGame and KNI run independently, so one adapter assertion cannot suppress the other's evidence. The targeted Ubuntu POC B/E run passes 7/7 tests.

| Platform/test path | Adapter | Presented rate | Median input-to-present | Platform gate |
| --- | --- | ---: | ---: | --- |
| Windows POC B worker viewport | MonoGame | 60.0 FPS | 15.6 ms | Passed |
| Windows POC B worker viewport | KNI | 40.0 FPS | 46.6 ms | Passed; KNI remains experimental |
| Windows POC E independent rendering | MonoGame | 60.1 FPS | 15.7 ms | Passed |
| Windows POC E independent rendering | KNI | 40.7 FPS | 46.7 ms | Passed; KNI remains experimental |
| Ubuntu POC B combined worker viewport | MonoGame | 61.7 FPS | 16.3 ms | Passed |
| Ubuntu POC B combined worker viewport | KNI | 32.3 FPS | 55.1 ms | Passed; KNI remains experimental |
| Ubuntu POC E independent rendering | MonoGame | 61.6 FPS | 16.4 ms | Passed |
| Ubuntu POC E independent rendering | KNI | 33.3 FPS | 55.2 ms | Passed; KNI remains experimental |

The unchanged gate is at least 30 presented FPS with median input-to-present below 100 ms. These measurements establish the gate only for the recorded Windows and Ubuntu environments; they do not repair or replace the prior failing macOS evidence.

## Cross-Platform Fixes Delivered

- Picking now executes an actual one-pixel ID-buffer read on the framework graphics thread, eliminating the previous full ID-target readback.
- Paused workers continue servicing queued graphics-thread pick operations without publishing or advancing frames; the POC B Pause regression covers the normal click path and structured timeout boundary.
- The viewport consumer retains a persistent shared-frame seqlock reader and derives presented FPS from producer frame timestamps rather than polling cadence.
- Native Unix targets use position-independent code where required, eliminating the clean Ubuntu shared-library link failure.
- Native sanitizer runtime settings propagate into managed child worker processes, so the Ubuntu Clang ASan matrix exercises the real process topology.
- The checked-in Ubuntu Dockerfile installs `pkg-config`, satisfying clean dependency discovery instead of relying on host state.

## Implemented Windows Scope

### Launch, project, and document behavior

- The managed launcher uses `ProcessStartInfo.ArgumentList`, so the repository's path containing spaces is passed without reparsing errors.
- Development and manual editor runs launch a writable disposable sample copy under `out/dev`, not the tracked source sample.
- The Project Explorer uses the manifest-rooted index and presents folders, scenes, prefabs, assets, preview images, importer/dependency status, search/filter state, and diagnostics exercised by the automated tests and manual QA.
- Candidate project construction and validation execute before replacing the active project in the covered project-model path.
- Command validation, compound mutation, dirty/savepoint state, undo/redo, and structured Console models pass their registered Windows tests.

### Authoring UI

- The typed Inspector exposes first-party property values and metadata-backed editors within the current component/drawer coverage.
- Hierarchy and preset actions use authoritative commands. The manually verified Empty preset creates beneath the selected parent as one transaction, selects the result, marks the scene dirty, and restores the exact clean savepoint with one `Ctrl+Z`.
- Scene View gizmos project from selected transforms, support ordered multi-selection, local/global deltas, snapping, and focus-to-selection bounds.
- Gizmo drag updates only a candidate preview scene; Escape restores exact originals without a dirty or undo change; mouse release commits one compound undo item through the authoritative transaction path.
- Asset previews, project status, structured Console, workspaces, and current keyboard/action paths are present within the registered Windows coverage.

### Runtime, rendering, and physics

- Preview and Play use isolated worker sessions. Authoring data remains editor-owned and Stop does not apply runtime state to the saved scene.
- Actual MonoGame and KNI devices produce the recorded Preview and Play frames. Scene-driven POC E coverage includes readback, revision correlation, picking, resize, and scene-dependent output.
- MonoGame and KNI worker evidence is kept independent. KNI Pause and Stop were exercised manually, but KNI remains visibly experimental.
- Box2D/Jolt native facade, ABI, managed interop, worker integration, fixed-step behavior, and cleanup paths pass the registered Windows POC G and Slice 2 tests.
- Edit simulation starts and stops in an isolated preview world and does not write simulated transforms into authoritative authoring data.

### Linked prefabs

- The current Windows domain and editor-service tests cover linked prefab identity, deterministic source revisioning, mappings, normalized overrides, fallback materialization, cycle checks, instantiate, apply/revert, repair/rebase, and unpack workflows within their implemented fixtures.
- This does not yet prove the complete required newer-source, deep three-level nesting, duplicate nested source, every-override, fallback-conflict, and multi-document failure matrix.

## Manual Windows QA Record

Manual QA used the disposable project under `out/dev`.

| Scenario | Observed result |
| --- | --- |
| Open project | Disposable sample opened successfully; the tracked source sample was not used as the writable project |
| Browse project | Indexed explorer displayed project content, previews, and importer/dependency status |
| Inspect content | Typed Inspector displayed and edited the covered component properties |
| Create child GameObject | Empty preset created beneath the selected parent as one transaction and became selected |
| Dirty/undo | Creation showed the dirty marker; `Ctrl+Z` restored the exact clean state |
| Simulate | Isolated simulation started and stopped without applying runtime changes to authoring data |
| MonoGame | Actual Preview and Play output displayed |
| KNI | Actual Preview and Play output displayed; Pause and Stop operated |
| Source integrity | The manual session produced no writes in the tracked sample tree |

## Acceptance Audit

| Gate | Windows status | Ubuntu status | Overall status |
| --- | --- | --- | --- |
| Complete registered strict Release suite | Verified: 36/36 | Verified: 36/36 | macOS current matrix absent |
| Complete registered native ASan suite | Verified: 36/36 | Verified: 36/36 | macOS current sanitizer matrix absent |
| Corrected POC B throughput and input-to-present | Verified for both adapters | Verified for both adapters; targeted POC B/E 7/7 | Prior macOS 14/15 run still fails POC B; no threshold reduction permitted |
| POC E real scene-driven rendering/picking | Verified within registered coverage | Verified within registered coverage | macOS run pending |
| POC F linked nested prefabs | Current tests pass | Current tests pass | Full newer-source/deep/failure matrix and macOS run pending |
| POC G native 2D/3D physics | Verified within registered coverage | Verified within registered coverage | macOS run pending |
| POC H Qt interaction | Current interaction aliases pass | Current interaction aliases pass | Broader designer/accessibility flows and macOS run pending |
| Slice 1 acceptance | Local registered matrix passes | Local registered matrix passes | Not closed; blocked by unchanged macOS POC B gate |
| Slice 2 designer acceptance | Not closed | Not closed | Complete 2D/3D nested-prefab, physics, accessibility, and designer workflow evidence remains pending |
| ADR status | Proposed | Proposed | Remains Proposed until corresponding three-platform evidence and review pass |
| KNI supported status | Not granted | Not granted | Remains experimental until its complete compatibility/platform matrix passes |

## Required Handoff Work

1. Repair and pass POC B on macOS arm64 at 1280x720, at least 30 presented FPS, and median correlated input-to-present below 100 ms for MonoGame and KNI independently.
2. Run the current POCs E-H and full registered Release/native-sanitizer matrix on macOS arm64; preserve failures rather than reducing thresholds or removing a platform.
3. Complete the full linked-prefab newer-source and deep-nesting matrix, including duplicate nested sources, all override types, fallback recovery conflicts, cycle/expansion guards, target-level apply, both unpack modes, and multi-document failure recovery.
4. Expand real Qt Test and manual designer evidence for drag/drop, component operations, full property drawers, dialogs/prompts, workspaces, Console navigation, keyboard-only operation, accessible names/roles/actions, high-DPI, high contrast, and complete small 2D/3D project workflows.
5. Split the remaining composition and behavior out of `EditorWindow` into the accepted Project, Scene, Selection, Command, Metadata, Asset, RuntimeSession, Diagnostics, Workspace, Prefab, and AssetPreview services.
6. Re-run the complete three-platform acceptance matrices, update the plan/evidence/ADR statuses and living documents only when their evidence gates are satisfied, and verify every documentation mirror hash.

## Disposition

The Windows functional-editor increment and the registered Windows/Ubuntu matrices are ready for continued development and regression testing. The active execution plan remains **In progress**. No statement in this audit closes Slice 1, closes Slice 2, promotes an ADR based on two-platform evidence, or changes KNI's experimental status.

On 2026-07-25, `DPE-ARCH-0009` superseded the narrower plan for active tracking with the four-slice version 1.0 master plan. This audit remains historical evidence; every open Slice 1/2 handoff item remains binding.
