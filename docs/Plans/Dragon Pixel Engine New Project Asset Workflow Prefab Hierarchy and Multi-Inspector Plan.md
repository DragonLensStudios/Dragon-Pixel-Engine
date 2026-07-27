# Dragon Pixel Engine New Project, Asset Workflow, Prefab, Hierarchy, and Multi-Inspector Plan

> **Status:** In progress  
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

## Completion Checklist

- [x] Publish and mirror `DPE-ARCH-0014` governance and contract updates.
- [ ] Implement project lifecycle service, templates, Project Hub, New Project, New Scene, and Save As.
- [ ] Implement project-v4/template-v1 compatibility fixtures.
- [ ] Implement asset-v3 service, image import/runtime binding, and recoverable trash.
- [ ] Implement the two-pane Project Browser and public asset operations.
- [ ] Implement versioned drag/drop and linked prefab paths.
- [ ] Implement Hierarchy multi-root operations and usability controls.
- [ ] Implement reusable multiple Inspector docks and lock-safe target routing.
- [ ] Pass focused Release and sanitizer verification.
- [ ] Verify production bundle and new-project launch outside the source tree.
- [ ] Record remaining platform/POC blockers without overstating acceptance.
- [ ] Verify every mirrored Markdown pair is byte-identical UTF-8/no-BOM with LF endings.
