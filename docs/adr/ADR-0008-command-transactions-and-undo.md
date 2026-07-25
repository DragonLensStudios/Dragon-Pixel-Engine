# ADR-0008: Editor Services, Command Transactions, and Undo/Redo

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

A functional editor has many mutation sources: Hierarchy, Inspector, Scene View gizmos, Project Explorer drag/drop, prefab workflows, migration, Python, and AI. If panels or tools write records and files independently, validation, undo, dirty state, audit evidence, preview synchronization, and unknown-data preservation will diverge. Slice 2 also needs long-lived models and services rather than state owned by one monolithic `EditorWindow`.

## Decision

### Service ownership

Split the editor into internal Project, Scene, Selection, Command, Metadata, Asset, RuntimeSession, Diagnostics, Workspace, Prefab, and AssetPreview services.

- ProjectService owns candidate project loading, document sets, save/recovery orchestration, project revision, and the clean savepoint.
- SceneService owns the authoritative in-memory authoring graph and read-only snapshots exposed to views.
- SelectionService owns ordered selection, active object, origin, and notifications; selection changes are editor state and are not undo items by default.
- CommandService is the only authority that may mutate authoring records.
- MetadataService supplies validated descriptors and property constraints.
- AssetService owns the manifest-rooted folder/scene/prefab/asset index, dependency diagnostics, external-change detection, and stable asset lookup.
- RuntimeSessionService receives only committed or explicit preview snapshots and never writes authoring state.
- DiagnosticsService stores structured events and navigation context.
- WorkspaceService owns non-authoritative per-user layouts and editor cameras.
- PrefabService materializes linked authoring views and prepares prefab operations; every resulting mutation is still committed by CommandService under ADR-0013.
- AssetPreviewService schedules disposable thumbnails/previews and cannot mutate authoritative assets.

Panels, models, dialogs, workers, and automation consume these services. They do not own duplicate scene or asset records and do not write project files.

Opening or reloading a project constructs and validates a complete candidate ProjectService session, including startup scene, roots, metadata, asset IDs, dependencies, and recoverable diagnostics. The current session is replaced only after the candidate succeeds and any dirty-session prompt completes.

### Command contract and validation

Every authoritative mutation uses a stable typed/versioned command envelope containing:

- command type and schema version;
- target project/document IDs and expected revisions;
- actor/capability identity and correlation ID;
- typed payload and explicit options;
- preconditions;
- deterministic affected-record and diagnostic results.

`validate`/dry-run resolves targets, permissions, metadata constraints, hierarchy/prefab invariants, incoming references, and expected revisions without mutation. It returns structured errors/warnings plus a deterministic change summary. Commit revalidates against the same base revision, then applies atomically. A failed command or canceled transaction leaves authoring records, command history, selection, dirty state, runtime mirror, and files unchanged.

CommandService emits one committed event with new document/command revisions and an audit correlation ID. ProjectService serializes validated state; commands never perform ad hoc scene-file writes. Prefab create/apply is the only Slice 2 asset-file creation/update exception and uses the atomic multi-document contract in ADR-0013.

### Transactions, previews, undo, and redo

- A transaction groups one or more commands into one atomic commit, one undo item, one runtime revision, and one audit event.
- Undo data stores exact before-images for affected records, hierarchy/root ordering, reference values, opaque payloads, and document membership. It does not copy the entire project when bounded affected-record state is sufficient.
- Transaction rollback and Undo restore original UUIDs, serialized unknown data, sibling/component ordering, enabled state, mappings, and diagnostics-relevant reference values.
- Redo replays the canonical committed operation only while its base is valid. A new divergent commit clears the redo branch. External document replacement or incompatible migration invalidates affected history with a visible diagnostic rather than applying it to a different base.
- The clean savepoint is a committed history position plus document hashes. Undoing or redoing back to that state clears dirty state; any authoritative difference marks the project dirty.
- Continuous editing coalesces compatible changes. A gizmo or drag starts a preview transaction with captured originals, publishes revisioned preview state to the viewport, and produces no undo entry while moving. Release validates and commits exactly one undo item; Escape/cancel restores originals and sends a fresh preview revision.
- Long-running preparation may report progress and cooperate with cancellation, but the authoritative commit phase is bounded and atomic.

### Required command families

Stable command families cover:

- Empty, Sprite, Cube, Camera, and Light GameObject presets;
- entity rename/enable, duplication, subtree deletion, hierarchy reparent, and sibling/root reorder;
- ordered multi-edit and transform delta operations;
- component add/remove/enable/reorder and typed property assignment;
- compatible asset assignment and drag-to-scene creation;
- linked-prefab create/instantiate/apply/revert/rebase/repair/unpack operations under ADR-0013;
- physics-authoring settings and components under ADR-0012.

A preset is one transaction. Empty creates Transform; Sprite creates Transform plus Sprite; Cube creates Transform plus Static Mesh and Material; Camera creates Transform plus valid camera defaults; Light creates Transform plus a directional Light. The single selected entity is the parent; otherwise the new object is a root. Defaults come from metadata, all IDs are allocated before commit, and the committed object becomes selected.

Duplication allocates all new IDs before mutation and deterministically remaps references within the duplicated set while preserving external references.

Subtree deletion enumerates descendants and incoming entity references during dry-run. The editor confirms once using that preview. Commit deletes the subtree but deliberately preserves incoming UUID values outside it as dangling references with repair diagnostics. Undo restores the subtree, its precise ordering, opaque records, and prior diagnostics. The engine does not silently null references whose intent is unknown.

### Project lifecycle and automation

The title and actions reflect project validity, dirty state, undo/redo availability, selection, and runtime state. Invalid actions are disabled with an accessible reason. Open, close, reload, and exit use Save-Discard-Cancel prompts through an injectable prompt service. Cancel preserves the active session; a failed save cannot proceed as Discard implicitly.

Python, AI, migration, and future plugins call the same command service with explicit capabilities. They receive dry-run results, cannot bypass validation, and produce actor-, capability-, input-, approval-, result-, and correlation-aware audit events. Direct writes to authoritative project roots remain prohibited.

## Consequences

One mutation path makes UI, headless tests, automation, undo, runtime reload, and audit behavior agree. Exact before-images cost memory, and transactional model notifications require care, but they avoid whole-project snapshots and preserve opaque data. Service boundaries increase construction and dependency-injection work while allowing Qt models, tests, and later plugins to observe the same state without owning it.

## Alternatives considered

- **Panel-specific mutations and undo stacks:** rejected because the same domain change would validate and recover differently by entry point.
- **Whole-project snapshot per undo item:** simple but rejected as the normal strategy because large projects would consume unbounded memory and obscure affected-record intent.
- **UI callback reversal:** rejected because it cannot replay headlessly, migrate schemas, or restore data after a panel is destroyed.
- **Automatically null incoming references on delete:** rejected because it destroys user intent and makes exact Undo/diagnosis harder.
- **Treat selection and camera movement as authoring commands:** rejected by default because they are per-user editor state; a command may still select its result as a post-commit UI effect.

## Public command behavior

Command envelopes are additive and version negotiated. Unknown command types or unsupported versions fail before execution. Stable IDs, not display names or tree indices, identify targets. Each result contains applied/not-applied state, previous/new revisions, affected IDs, structured diagnostics, and correlation ID. No result reports success before the atomic commit completes.

## Validation and acceptance gate

Domain tests must cover validation with no writes, optimistic revision conflicts, transaction rollback, continuous-edit coalescing, clean savepoints, dirty transitions, undo/redo branching, hierarchy cycles/order, duplication/reference remapping, subtree deletion/incoming references, exact opaque restoration, multi-edit, command cancellation, and audit correlation.

POC H and end-to-end Qt tests must drive real preset actions, inline rename, enable toggles, drag reparent/reorder, typed multi-edit, gizmo commit/cancel, delete confirmation, undo/redo, dirty prompts, disabled action states, asset assignment, and save/close/reopen through public UI events and injectable prompts. Automation conformance must produce the same validated result as the UI for an equivalent command.

Current evidence (2026-07-25): Windows and Ubuntu now execute typed command validation, compound scene transactions, bounded before-images, Undo/Redo history and branching, dirty/savepoint state, preset creation, duplication with ID remapping, subtree deletion, reparenting, multi-edit/transform deltas, component operations, continuous gizmo previews, and prefab-source journaling. Candidate project indexing, asset previews, model-backed Project Explorer, structured Console, and PrefabService are independently tested. A live Inspector test proves an invalid constrained value leaves state clean and creates no Undo item while a valid value commits; gizmo Escape restores the ordered multi-selection originals and release creates one Undo item.

The complete Windows Release matrix passed **36/36 tests in 118.39 seconds**, and the complete MSVC AddressSanitizer matrix passed **36/36 tests in 134.93 seconds**. Ubuntu Release passed **36/36 tests in 102.20 seconds**, and Ubuntu Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. Manual Qt QA against only the disposable writable `out/dev/Slice1Sample` copy confirmed typed Inspector editing and a GameObject preset as one transaction followed by Undo; it also confirmed real adapter preview/play controls and isolated simulation.

This is not acceptance evidence for the whole decision. `EditorWindow` still contains substantial orchestration and workflow behavior instead of being composition-only, and the complete Project/Scene/Selection/Command/Metadata/Asset/RuntimeSession/Diagnostics/Workspace service split is unfinished. Hierarchy multi-drag/reorder, deletion-reference repair UX, Inspector entity/asset choosers and component ordering, all dirty prompt cases, complete automation equivalence, broader accessibility/device flows, and a current macOS POC H run remain open. This ADR therefore remains `Proposed`.
