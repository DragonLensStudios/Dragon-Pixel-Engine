# Dragon Pixel Engine New Project, Asset Workflow, Prefab, Hierarchy, and Multi-Inspector Plan

> **Status:** Implemented; focused Windows verification complete, acceptance gates remain open
> **Started:** 2026-07-27  
> **Design baseline:** `DPE-ARCH-0013`  
> **Target design revision:** `DPE-ARCH-0014`  
> **Repository mirror:** `docs/Plans/Dragon Pixel Engine New Project Asset Workflow Prefab Hierarchy and Multi-Inspector Plan.md`  
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine New Project Asset Workflow Prefab Hierarchy and Multi-Inspector Plan.md`

## Objective

Deliver a Unity-familiar, command-backed authoring increment that creates clean 2D/3D projects and scenes, makes project assets manageable, makes linked prefabs easy to create and instantiate through drag/drop, improves ordered Hierarchy multi-selection and multi-object operations, and supports multiple independently lockable Inspector docks with safe shared-component editing.

This increment advances POCs F, H, M, and O but does not close an ADR, POC, slice, platform matrix, or KNI support gate without its complete evidence.

## Locked Product Decisions

- A no-argument editor launch opens a Project Hub; explicit `--project`, `--scene`, and self-test launches remain compatible.
- New projects use minimal installed 2D and 3D templates. The 2D scene contains only a primary orthographic camera; the 3D scene contains only a primary perspective camera and directional light.
- Scene View drops create one root object at world origin. Hierarchy-row drops create one child at local origin.
- Project Browser becomes a two-pane folder/content browser with breadcrumbs, grid/list presentation, filters, previews, status, and command-backed asset actions.
- OS import supports copied or read-only linked sources. PNG/JPEG are renderable sprite assets; unsupported files remain generic assets with diagnostics.
- Asset removal uses recoverable project trash and never writes linked external sources.
- Linked prefab drag workflows are included; a dedicated Prefab Mode is excluded.
- Multiple Inspector docks are supported. Their count/layout persists, but locks reopen cleared after an editor restart.
- Initial importer scope excludes 3D models, audio, and fonts.

## Architecture and Contract Work

1. Publish synchronized `DPE-ARCH-0014` Design and Prompt/Result records before source changes.
2. Update ADR-0005, ADR-0007, ADR-0008, ADR-0013, ADR-0018, and ADR-0020 without promoting their `Proposed` status.
3. Update `AGENTS.md` and ADR index status to the new revision/evidence boundary.
4. Implement the accepted `dpe.project` v4, `dpe.project-template` v1, and `dpe.asset` v3 schemas while preserving project v1-v3 and asset v1-v2 reads.
5. Keep scene v3, prefab v1, metadata v4, runtime snapshot v4, C ABI, Rider, component lifecycle, and input-map contracts compatible.

## Implementation Increments

### A. Project lifecycle and clean scenes

- Add a contained `ProjectLifecycleService` with template discovery, creation dry-run/commit, deterministic injected IDs, candidate validation, recent-project state, clean-scene creation, cancellation, recovery, and fault injection.
- Package immutable relocatable 2D/3D template resources.
- Add Project Hub, New Project, New Scene, and Save Scene As interactions through injectable prompt/dialog seams.
- Create the default input-map asset and C# component project in every new project without executing project code.

### B. Asset service and Project Browser

- Add `AssetService` operations for import, create-folder, rename, move, duplicate, dependency impact, trash, restore, and immutable runtime bindings.
- Add asset-v3 copy/link/generic/image handling with contained staging, hashes, stable IDs, and recovery manifests.
- Split the Project Browser into service-backed folder and content models with stable selection, filters, thumbnails, details, and public context/keyboard actions.
- Preserve source/component editing through existing Rider and component services.

### C. Drag/drop, prefabs, and Hierarchy

- Version project and hierarchy MIME payloads with project/scene identity, stable IDs, kind, and source revision.
- Support OS-to-Project import, internal Project moves, sprite/prefab drops to Scene/Hierarchy, asset-field assignment, and locally owned Hierarchy-root-to-Project prefab creation.
- Add Hierarchy toolbar/context/keyboard operations and transactional top-level multi-root reparent/reorder, duplication, deletion, enablement, and grouping.
- Surface prefab linked/override state and retain current Apply/Revert/Repair/Unpack commands.

### D. Multiple locked Inspectors

- Extract a reusable Inspector panel/controller and introduce ordered `SelectionService` state.
- Add dynamically created Inspector docks with stable dock IDs and independent target/lock/search state.
- Route every Inspector action through the panel target set rather than global selection.
- Preserve first-class mixed state, all-target validation, one compound commit, and exact Undo restoration.
- Keep unavailable locked identities visible and non-editable so Undo can restore them.

## Verification

- Unit tests: template/project/scene schemas, deterministic creation, containment, cancellation/recovery, project compatibility, asset copy/link/import/trash/restore, stable identities, link/case safety, runtime bindings, selection, and grouped hierarchy commands.
- Qt tests: Project Hub/wizards, New Scene/Save As, two-pane browser, OS/internal drag/drop, public asset actions, prefab round trips, Hierarchy multi-drag/keyboard/accessibility, multiple Inspector locks, mixed commits, target deletion/Undo, and unlocked layout restoration.
- Framework tests: imported PNG/JPEG produces actual MonoGame and separately reported KNI device pixels and correlated picking at world origin; Save/reopen and Play/Stop preserve authoring state.
- Run focused Windows strict Release and MSVC AddressSanitizer aliases first, then the current complete Windows matrix when the known atomic-publication defect is repaired or explicitly records its unrelated failure.
- Run current Ubuntu Release/Clang-ASan and macOS Release/sanitizer evidence before any three-platform or POC claim.
- Rebuild and verify the relocatable production bundle, installed templates/schemas/resources, packaged self-test, manifest hashes, and a newly created project outside the source tree.

## Work Log

| Date | State | Evidence / decision |
| --- | --- | --- |
| 2026-07-27 | Selected | User approved implementation of the decision-complete New Project, asset workflow, linked-prefab drag, Hierarchy, and multi-Inspector plan. Required documentation mirrors pass 47 UTF-8/LF pairs at `DPE-ARCH-0013` before the increment. |
| 2026-07-27 | In progress | Creating this mirrored plan and preparing synchronized `DPE-ARCH-0014` architecture/provenance updates before source changes. |
| 2026-07-27 | Governance published | Published synchronized `DPE-ARCH-0014` Design, `RESULT-DPE-ARCH-0014` Prompt/Result, Notes intake, ADR-0005/0007/0008/0013/0018/0020 refinements, ADR index, `AGENTS.md`, README boundary, and this plan. The recursive validator passes all 48 UTF-8/LF Markdown pairs. Design SHA-256 is `C9692A2F0556ECF0A7BF047941F874BA7922980F546587D9514C4C9B4EE64E1E`; Prompt/Result SHA-256 is `2A7575D605DFA0AD7B76E675160DDCD8D00F59B6C30CA08DF756EE41ED72F6FF`. Source implementation may now begin. |
| 2026-07-27 | Project lifecycle implemented | Added project-v4, project-template-v1, and asset-v3 schemas; relocatable Minimal 2D/3D templates; contained staged `ProjectLifecycleService` creation/dry-run/cancellation/recovery/recent-project/clean-scene paths; v1-v3 in-memory project compatibility; Project Hub; and File-menu New Project, New Scene, and Save Scene As. No-argument launch no longer selects the sample implicitly, while explicit project/scene, development, and self-test entry points remain compatible. |
| 2026-07-27 | Asset and Project Browser implemented | Added copy/read-only-link PNG/JPEG and generic imports, asset-v2 reads and asset-v3 writes, stable IDs and hashes, dependency results, immutable runtime bindings, create/rename/move/duplicate/trash/restore operations, contained recovery journals, and atomic folder moves. The Project Browser now has folder/content panes, breadcrumbs, list/thumbnail modes, search/type/status filters, details, badges, stable refresh selection, keyboard/context actions, and existing Rider/component source editing. Imported image bytes render and pick through the separate MonoGame and KNI workers. |
| 2026-07-27 | Drag, prefab, Hierarchy, and Inspector implemented | Added versioned project/entity MIME envelopes and validated OS import, project moves, image/prefab Scene and Hierarchy drops, Inspector asset assignment, and locally owned Hierarchy-root-to-Project linked-prefab creation. Hierarchy selection is ordered and supports accessible add/expand/collapse/rename/delete/duplicate/parent/focus/prefab actions plus atomic top-level multi-root grouping/reparent/reorder/enable/delete/duplicate behavior. `SelectionService` now backs reusable Inspector panels; View > New Inspector creates independent stable docks with lock snapshots, mixed-value intersection editing, all-or-nothing multi-target transactions, exact Undo before-images, unavailable deleted-target state, and unlocked layout restoration. |
| 2026-07-27 | Runtime and workspace defects repaired | Runtime input correlations now retain original event timestamps while deferring expiry until the worker handshake is ready, so queued startup input is presented instead of expiring. Workspace state v6 safely reconstructs persisted extra Inspector docks and Start Play consistently raises the Game dock. The sanitizer investigation also found and corrected a test-only stale `QModelIndex` reuse after the Project model refreshed during prefab creation; the test now reacquires the destination through a stable domain role. |
| 2026-07-27 | Windows Release evidence | Focused contract/service/rendering/Rider/prefab aliases pass **18/18**. `AssetServiceTests` pass **9/9**. The complete registered `s2.editor_interactions` Release test passes in **121.29 seconds**. The final five DPE-ARCH-0014 interaction functions—Project Hub, locked/mixed multi-Inspector, atomic Hierarchy operations, project/Hierarchy domain drops, and pre-handshake input—pass with QTest totals **7/7 in 37.07 seconds**. `git diff --check` passes. |
| 2026-07-27 | Windows sanitizer evidence | Nine focused service/model/prefab MSVC AddressSanitizer aliases pass **9/9**. The final five DPE-ARCH-0014 interaction functions pass with QTest totals **7/7 in 59.97 seconds**, with no AddressSanitizer diagnostic and no orphaned workers. An attempted aggregate sanitizer `s2.editor_interactions` run exceeded its 480-second harness timeout without a sanitizer report; it is inconclusive and is not recorded as a complete sanitizer-suite pass. |
| 2026-07-27 | Production bundle and external-project proof | `Build-Production-Editor.ps1 -SkipConfigure` rebuilt the production-style developer bundle and its packaged MonoGame self-test exited zero. All **188** manifest records exist and hash-verify with no unlisted files. Bundle-manifest SHA-256 is `B304E12BFA6F2BBC1F49739D62E8D41E908C46ED4A2BD1E56FDF90CB926DFAB1`; editor SHA-256 is `1616B2383EDD640A6846715DCBA81744FC18C124EA02EEB0B7595AFD316F87E9`. The deployed no-argument executable showed the Project Hub and created/opened `C:\Users\monyd\Documents\DPE Packaged Workflow 20260727` from packaged Minimal 2D resources. The result is `dpe.project` v4 with a `dpe.scene` v3 containing exactly one orthographic `Main Camera` and generated Assets, Prefabs, and Components roots. |
| 2026-07-27 | Final disposition and handoff | The requested implementation is complete for the focused Windows increment. Current Ubuntu/macOS matrices were not available in this work item; the known intermittent atomic-publication recovery defect still blocks a new authoritative full Windows matrix; aggregate POC J timing remains open; and the sanitizer aggregate is inconclusive. Therefore POCs F/H/M/O, all named ADRs, Slices 2/3, cross-platform acceptance, release-package acceptance, and KNI production support remain open. Next work should repair the atomic-publication defect, run the complete Windows matrices, and execute current Ubuntu/macOS Release/sanitizer/device-rendering/accessibility evidence. |

## Completion Checklist

- [x] Publish and mirror `DPE-ARCH-0014` governance and contract updates.
- [x] Implement project lifecycle service, templates, Project Hub, New Project, New Scene, and Save As.
- [x] Implement project-v4/template-v1 compatibility fixtures.
- [x] Implement asset-v3 service, image import/runtime binding, and recoverable trash.
- [x] Implement the two-pane Project Browser and public asset operations.
- [x] Implement versioned drag/drop and linked prefab paths.
- [x] Implement Hierarchy multi-root operations and usability controls.
- [x] Implement reusable multiple Inspector docks and lock-safe target routing.
- [x] Pass focused Release and sanitizer verification.
- [x] Verify production bundle and new-project launch outside the source tree.
- [x] Record remaining platform/POC blockers without overstating acceptance.
- [x] Verify every mirrored Markdown pair is byte-identical UTF-8/no-BOM with LF endings.
