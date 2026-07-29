# Dragon Pixel Engine Project View and Team GitFlow Workflow Plan

> **Status:** Active — architecture and plan accepted; implementation pending
> **Branch:** `feature/project-view-workflow`
> **Target:** `develop`
> **Owner:** Codex / Dragon Lens Studios
> **Started:** 2026-07-29
> **Last updated:** 2026-07-29
> **Design revision:** `DPE-ARCH-0016`
> **Dependencies:** None; branch created directly from `develop` at merged PR #6 commit `fa59b227e3057af603c569f1913b652d22b50c5a`
> **Stack disposition:** Independent PR targeting `develop`; it is not stacked on another feature

## Goal and user value

Deliver the next complete daily-authoring feature: a cleaner, Unity-familiar Project View that treats folders as first-class navigation, keeps list/tile state stable, and makes validated drag/drop into Project folders, Scene, Hierarchy, and compatible Inspector targets predictable. In the same work item, make the repository's GitFlow process practical for two or more contributors by supporting concurrent independent PRs and explicitly documented dependent PR stacks without weakening review or CI.

An author should be able to understand where assets live, move through folders, import or organize files without opening a Scene, and drag a supported asset into the Scene with visible, deterministic behavior. A contributor should be able to start independent work from `develop` while another PR is under review, or explicitly stack a genuinely dependent PR and later retarget it safely.

## Background

PR #6 merged the DPE-ARCH-0015 Tilemap Editor into `develop`. DPE-ARCH-0014 already introduced a two-pane Project Browser, project-v4/asset-v3 indexing, `AssetService` operations, immutable runtime bindings, and versioned project/entity drag envelopes. The current surface is functional but incomplete as a daily project view:

- the breadcrumb is a passive label rather than navigation;
- refresh expands every folder and does not deliberately restore expansion, selection, or view state;
- Project-to-Project drops are rejected when no Scene is open even though `AssetService` owns those operations independently;
- folder destinations and drop acceptance are not consistently apparent;
- CI runs PR jobs only when the target is `main` or `develop`, so an explicitly dependent feature PR cannot receive the same checks while targeting its parent feature branch;
- the handbooks describe only one unstacked feature path and do not define safe concurrent ownership, merge ordering, retargeting, or conflict evidence.

## Existing architecture and authoritative owners

- `ProjectIndexService` builds and validates the detached project candidate. `ProjectModel`, `ProjectFilterProxyModel`, and `ProjectFolderProxyModel` expose stable indexed rows to Qt views.
- `AssetService` is the only owner for import, create-folder, rename, move, duplicate, trash, restore, dependency impact, and immutable runtime asset bindings.
- `CommandService` and existing scene/prefab command paths own Scene/Hierarchy creation and linked prefab behavior.
- `SelectionService` and stable domain IDs own selection; Qt indices are projections and may be replaced during refresh.
- `EditorWindow` currently composes the folder tree, list/tile content views, search/filters, breadcrumb label, details, and drop adapters.
- `scripts/ci/validate_ci.py`, `.github/workflows/slice1.yml`, the Development Constitution, GitFlow Handbook, `AGENTS.md`, and the feature PR template define and enforce repository workflow.

No new asset database, file manager, scene mutation owner, undo stack, drag protocol, or CI bypass will be introduced.

## Scope

### Project View

- Present the dock as **Project** with a two-pane folder/content workflow and Dragon Pixel-owned visuals.
- Add Back, Forward, Up, and clickable breadcrumb segment navigation with accessible names, enable states, and keyboard focus.
- Preserve the nearest valid active folder, folder expansion, stable item selection, splitter/view mode, and folder history across index refresh; fall back safely to the declared Assets root.
- Keep list and tile views synchronized over one content root, with folders first and deterministic case-insensitive natural ordering.
- Improve folder/asset recognition with consistent type icons, status cues, tooltips/details, and blank-space destination behavior without copying proprietary editor artwork.
- Make folder and stable-asset Project moves work without an open Scene. Keep entity-to-Project prefab creation Scene-dependent.
- Retain validated OS import, Project-to-Project move, Project-to-Scene/Hierarchy creation, Project-to-Inspector assignment, and Hierarchy-to-Project prefab creation paths with clear rejection diagnostics.
- Add public Qt and model/service regressions for navigation, refresh restoration, no-Scene moves, supported asset creation drops, stale/cross-project rejection, and keyboard/accessibility behavior.

### Team GitFlow

- Allow multiple independent feature branches and PRs to exist concurrently; each starts from the current `develop`, has one owner/scope/plan, and normally targets `develop`.
- Allow one `feature/*` PR to target one parent `feature/*` branch only when dependency and overlap are explicitly recorded in both plans and both PR bodies.
- Define child-before-parent prohibition, post-parent retarget/update/retest steps, merge queue ownership, conflict handling, and shared-history force-push restrictions.
- Run the cross-platform PR workflow for `feature/**` targets and validate `feature/* -> feature/*` relationships while continuing to reject unrelated branch topologies.
- Extend the feature PR template with ownership, dependency/stack, overlap, retarget, and verification fields.

## Non-goals

- Copying Unity branding, icons, labels, exact layout, packages, serialized formats, or proprietary behavior.
- A general filesystem explorer, unrestricted external file moves, arbitrary source-code file management, content browser database replacement, source control client, or asset reimport/cache-reconstruction completion.
- Multi-select atomic Project moves until `AssetService` has an explicit bounded batch operation; drag payload v1 may carry multiple items but ambiguous mutation remains rejected.
- Cursor/surface-specific 3D placement, new import formats, editor-process importer execution, or changes to linked-prefab semantics.
- Automatic merging, bypassing human review, weakening required checks, allowing dependent PRs to merge out of order, or force-pushing a teammate's branch without coordination.
- macOS stabilization in this feature. macOS remains required for the product and its status will be reported as unrun/deferred rather than passed.

## Contract, format, ABI, protocol, platform, and support impact

- **Durable formats:** none. Project v4, asset v3, scene v3, prefab v1, tile formats, and per-user workspace data retain their versions.
- **Drag protocol:** project/entity MIME envelope remains `dpe.drag` version 1. Existing identity, revision, kind, and stable-ID validation remains mandatory.
- **C ABI / managed contracts / worker protocols:** no change.
- **Process topology:** no product process change. GitHub review topology adds explicit dependent feature targets; the disposable-worker boundary is unchanged.
- **Editor mutation path:** Project-only operations stop depending on Scene availability but continue through `AssetService`; Scene-affecting drops continue through existing commands.
- **Platforms:** changed portable and Qt paths require Windows Release/MSVC AddressSanitizer and Ubuntu Release/build evidence. macOS is explicitly deferred by user direction and remains an open product gate.
- **Support claims:** no POC, ADR, slice, release, platform, or KNI production promotion.

## Security and data safety

- Resolve destination folders from the current validated project model and manifest; never trust a display label or raw drag path as authority.
- Preserve project ID and source revision validation, containment, case/collision checks, stable asset IDs, staging, recovery manifests, and last-known-valid files.
- Reject recursive folder moves, cross-project or stale drags, project roots as move sources, ambiguous multi-item mutation, incompatible Scene targets, and missing source records before mutation.
- Navigation history, expansion, splitter, and view mode are per-user non-authoritative state. Invalid state is discarded without writing project files.
- A dependent PR relationship changes only review base/CI diff. It grants no new repository permissions and does not permit merging before its parent or bypassing the final `develop` review.

## Implementation increments

### Increment 1 — Architecture, workflow, and CI contract

1. Synchronize DPE-ARCH-0016 Design and Prompt/Result, `AGENTS.md`, master tracker, this plan, affected ADRs, Development Constitution, GitFlow Handbook, and feature PR template.
2. Add failing CI policy cases for a documented feature child targeting a feature parent and invalid self/non-feature stacks.
3. Update the workflow trigger and validator, run portable policy tests, review the workflow diff, and commit the coherent workflow change.

### Increment 2 — Project model presentation and navigation

1. Add focused model/public interaction regressions for deterministic folder-first natural ordering and navigation state.
2. Add Project Back/Forward/Up controls and clickable breadcrumb segments over stable logical folder identities.
3. Restore valid active folder, expansion, and stable selection after refresh without `expandAll`; synchronize list/tile roots and selection.
4. Add accessible item/type/status presentation and per-user list/tile/splitter state without making it authoritative.
5. Run focused Project Model and editor interaction tests and commit.

### Increment 3 — Folder and Scene drag/drop completion

1. Add a regression proving a stable asset and folder can move through `AssetService` with no Scene open.
2. Separate project-only drop preconditions from entity-to-Project prefab preconditions and retain existing stale/cross-project/revision checks.
3. Exercise OS-to-current-folder, Project-to-folder, sprite/prefab/Tilemap-to-Scene/Hierarchy, compatible Inspector assignment, and rejected target behavior through public Qt paths where practical.
4. Confirm successful moves preserve stable IDs and failed moves leave the index and authoritative bytes unchanged.
5. Run focused Release and MSVC AddressSanitizer checks and commit.

### Increment 4 — Evidence and review handoff

1. Run complete local Windows Release and relevant MSVC AddressSanitizer verification under unchanged thresholds.
2. Run or obtain Ubuntu Release configure/build/test evidence for the changed paths; record macOS as deferred/unrun.
3. Update this plan, master tracker, ADR evidence, user guide/README as affected, and PR body with exact results and limitations.
4. Byte-verify every mirrored Markdown pair, review `develop...HEAD`, commit sequence, workflow, generated/bundle impact, and `git diff --check`.
5. Push `feature/project-view-workflow`, open a draft PR into `develop`, and stop for human review without merging.

## Affected tests

- `scripts/ci/tests/test_validate_ci.py`
- `src/editor/ProjectModelTests.cpp`
- `src/editor/EditorInteractionTests.cpp`
- Focused aliases that cover `s2.project_model`, `s2.asset_service`, `s2.editor_interactions`, and `poc_h.qt_interactions` as registered by CMake.
- Complete Windows Release/MSVC AddressSanitizer matrix and Ubuntu Release matrix selected by the repository verification checklist.

Tests will first reproduce defects where practical. No existing assertion, timeout, platform, test count, or performance threshold will be weakened.

## Documentation strategy

- Synchronized Design and Prompt/Result at DPE-ARCH-0016.
- This mirrored active plan and master tracker.
- `AGENTS.md`, mirrored Development Constitution and GitFlow Handbook, and `.github/PULL_REQUEST_TEMPLATE/feature.md`.
- ADR-0007, ADR-0008, ADR-0013, ADR-0018, and ADR-0020 refinements/evidence.
- README or Project View user guide only where the implemented public workflow needs contributor/user instructions.
- PR body using the feature template with explicit owner, dependency/stack, affected systems, exact evidence, and limitations.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Refresh invalidates Qt indices and loses user context | Store stable logical paths/IDs, restore only after candidate publication, and fall back to the nearest valid ancestor/Assets root. |
| Drag/drop bypasses service ownership | Keep views as proposal surfaces; call only `AssetService`, scene commands, or prefab commands after envelope validation. |
| Folder moves recurse, collide, or escape | Retain `AssetService` containment/collision validation and add no-Scene regression coverage. |
| Familiar presentation drifts into copying Unity assets | Use Dragon Pixel labels, styling, and generated/standard Qt icons; implement functional familiarity only. |
| Stacked PRs hide aggregate risk | Target the parent only while dependent, then update/retarget to current `develop`, review aggregate diff, and rerun affected gates before merge. |
| Contributors overwrite shared work | Record owner/scope/overlap, avoid force-push, merge one reviewed PR at a time, and make the child author resolve post-parent conflicts. |
| Windows-only UI confidence masks Unix failures | Require Ubuntu Release evidence and report macOS deferral explicitly without removing it from product gates. |

## Definition of done

- [ ] Architecture, plan, handbooks, ADRs, AGENTS, CI policy, and PR template agree and mirrors are byte-identical.
- [ ] Project View exposes functional Back/Forward/Up and clickable breadcrumb navigation with accessible states.
- [ ] Active folder, expansion, view mode, and stable selection restore safely across refresh.
- [ ] Immediate child content is deterministic, folder-first, and usable in list and tile modes.
- [ ] Project asset/folder moves work without an open Scene and preserve stable IDs.
- [ ] Supported Project-to-Scene/Hierarchy and Project-to-Inspector drops work through existing validated owners; invalid drops fail without mutation.
- [ ] Concurrent independent and explicit dependent feature PR rules are documented and CI-policy tested.
- [ ] Focused tests and required Windows/Ubuntu verification pass under unchanged thresholds; macOS status is explicit.
- [ ] Aggregate diff and commit sequence are reviewed, documentation/evidence is current, branch is pushed, and a draft PR into `develop` is ready for manual review.
- [ ] The branch remains unmerged.

## Work log

| Date | Entry |
| --- | --- |
| 2026-07-29 | Verified the Design, Prompt/Result, Original Prompt, and Notes mirror pairs exist and are byte-identical at DPE-ARCH-0015 before planning. Read the governing architecture, active tracker, prior Project workflow plan, CI plan, GitFlow controls, and affected ADRs. |
| 2026-07-29 | Fetched `origin`, confirmed PR #6 and CI/GitFlow PR #4 are merged, fast-forwarded local `develop` to `fa59b227e3057af603c569f1913b652d22b50c5a`, confirmed a clean worktree, and created `feature/project-view-workflow` directly from that commit. |
| 2026-07-29 | Inspected `ProjectIndexService`, Project models/proxies, `AssetService`, Project Browser composition, drag handlers, interaction tests, CI validator/workflow, and PR template. Selected one cohesive Project View plus team-review workflow feature and accepted synchronized DPE-ARCH-0016. |

## Handoff notes

Implementation has not yet been claimed. The branch is independent and should target `develop`. PR #6's merge is historical evidence only; it does not close the remaining POC K/O/P/J or platform gates. The first source increment is the regression-guarded CI/team workflow contract, followed by Project navigation and no-Scene drag/drop behavior.
