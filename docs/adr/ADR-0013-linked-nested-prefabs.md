# ADR-0013: Linked Nested Prefabs

> **Status:** Proposed
> **Date:** 2026-07-24
> **Last reviewed:** 2026-07-29
> **Design revision:** `DPE-ARCH-0016`

## Context

Slice 2 requires reusable hierarchies that remain linked to their source, support nesting, survive source evolution, and remain recoverable when a source is missing or newer than the editor. Copy-only templates lose source relationships, while persisting a fully expanded hierarchy makes overrides ambiguous and duplicates source-owned data. Prefab operations also must obey the editor's command, identity, deterministic-JSON, unknown-data-preservation, and play-isolation rules.

## Decision

### Ownership and durable records

- `dpe.prefab` version 1 is the authoritative linked-prefab document. A prefab owns a stable asset UUID, one prefab-local root UUID, ordered local entity/component records, direct nested-instance records, sorted direct dependencies, and a canonical SHA-256 source revision.
- Scenes and prefabs store locally owned entities separately from linked prefab-instance records. Expanded/materialized entities are a derived authoring view and are never written back as ordinary local entities merely because the source resolved successfully.
- Component identity remains `(entity UUID, component type UUID)`, with at most one component of a type per entity. Prefab property and component overrides address this composite identity after resolving the source entity through the instance mapping.
- An instance stores its instance UUID, source asset UUID, last resolved source revision, placement parent/order, stable entity mappings, normalized overrides, and the last successfully resolved fallback entities. The fallback includes enough ordered entity/component and nested-instance data to preserve the visible authored result and opaque data when the source cannot be resolved.

### Stable identity and expansion

- A mapping key is `(nested-instance path, source entity UUID)`. The path is the ordered sequence of instance UUIDs from the outer instance through the nested source boundary. Its value is the persistent materialized entity UUID used by selection, references, commands, overrides, Undo, and runtime flattening.
- Duplicate uses of one source and repeated nested sources receive distinct paths and therefore cannot collide. Surviving mapping entries are never regenerated solely because the source revision changes.
- Materialization resolves local records, nested instances, mappings, and overrides recursively into one authoring graph. Direct/indirect dependency cycles are rejected before save. Load and expansion enforce configurable depth and materialized-entity-count guards and surface a structured diagnostic instead of truncating silently.
- Runtime snapshots contain only the flattened entities/components and resolved asset bindings. Prefab source revisions, mappings, overrides, and fallbacks do not cross into MonoGame/KNI adapters or native simulation.

### Overrides and source changes

- Normalized override operations cover entity add, remove, rename, enable, reparent, and reorder; component add, remove, enable, and reorder; and property assignment. Each operation has a stable operation UUID, target path/identity, schema version, canonical payload, and deterministic order.
- Source refresh runs only through an explicit Repair/Rebase command. It builds and validates a candidate expansion, preserves mappings for surviving source entities, allocates mappings for new entities inside the command, reapplies overrides, and commits atomically.
- Overrides whose targets disappeared or became incompatible are retained as unresolved data with diagnostics. The command must not silently discard them. The last successfully resolved fallback is replaced only after the new candidate validates and commits.
- Missing or incompatible/newer sources disable Apply and Revert because their target semantics cannot be proven. The instance remains inspectable from fallback data, and Unpack Completely remains available.

### Authoring workflows

- Create from Selection validates that the selection has one reusable root, converts eligible scene-local relationships to prefab-local identities, writes the new prefab, and replaces the selection with one linked instance in a single recoverable transaction.
- Instantiate creates an instance record and all required stable mappings as one command. It does not duplicate source-owned entities into the scene document.
- Apply always requires an explicitly selected nesting level. It rejects references from reusable prefab content to scene-local objects outside that prefab boundary, updates the selected source, rebases affected instances, and commits all changed documents atomically.
- Revert Selected removes only the chosen normalized overrides; Revert All removes all overrides at the chosen instance level. Both rebuild a validated candidate before commit.
- Unpack replaces the selected linked level with locally owned records while preserving deeper linked instances. Unpack Completely recursively replaces the full expanded result with locally owned records and removes all prefab provenance. Both preserve materialized UUIDs, hierarchy/component order, references, and opaque payloads.

### Persistence and recovery

- Canonical source revisions are computed from canonical semantic JSON while excluding the self-referential revision field and recovery/journal metadata.
- Create, Apply, and any operation touching multiple authoritative documents use the atomic multi-document protocol in ADR-0006: validate the complete set, stage and flush every output, record a recovery manifest/backups, commit as one recoverable unit, and roll back or recover deterministically after failure.
- Every prefab command records before-images for source documents, instance records, mappings, overrides, fallbacks, hierarchy order, and opaque payloads so transaction rollback and Undo restore the exact prior logical state.

## Consequences

Linked nested prefabs retain reuse and source intent without leaking framework-specific or prefab-specific concepts into runtime adapters. Stable path-qualified mappings preserve selection and references across rebases. Fallbacks and unresolved overrides make failures recoverable but increase file size and require explicit repair UX. Apply is deliberately constrained because cross-boundary scene references would make a reusable source invalid outside its originating scene.

+## DPE-ARCH-0014 refinement

The first public drag workflow accepts one locally owned Hierarchy root and creates a linked prefab in the selected contained Project Browser folder; the complete subtree is included and ambiguous, linked, or cross-project selections are rejected before staging. Dragging a prefab to Scene View instantiates it at scene-root world origin; dragging to a Hierarchy row instantiates it as a child at local origin. Versioned payloads carry project/scene identity, stable source identity, kind, and revision so stale or foreign drags cannot resolve by path alone. Hierarchy and Inspector expose linked/override state while all Apply/Revert/Repair/Unpack behavior retains this ADR's source, mapping, override, and recovery ownership. Dedicated Prefab Mode remains deferred.

## Validation and acceptance gate

POC F must pass on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64 before this ADR can be Accepted. It must prove:

- deterministic `dpe.prefab` version 1 output and `dpe.scene` version 3 migration;
- three-level nesting, duplicate nested sources, stable mapping reuse, deterministic allocation, and reference preservation;
- every override class, explicit nesting-level Apply, Revert Selected/All, Unpack, and Unpack Completely;
- source refresh with additions/removals/reorders, unresolved conflicts, missing/newer sources, fallback recovery, and opaque-data preservation;
- direct and indirect cycle rejection plus depth/entity-count guards; and
- atomic multi-document success, injected staging/commit failures, rollback, startup recovery, and exact Undo restoration.

Current Windows and historical Ubuntu evidence (2026-07-25): native `s2.linked_prefabs`/`poc_f.linked_prefabs` tests cover deterministic prefab-v1 bytes, three-level materialization, duplicate nested sources, path-qualified stable mappings, nested property/rename overrides, explicit source Apply, rebase allocation, selected/all Revert, complete Unpack, missing/newer/incompatible-source fallback, direct and indirect cycle rejection, depth/entity-count guards, and opaque preservation. Editor-service `s2.prefab_editor_service`/`poc_f.prefab_editor_workflows` tests cover Instantiate, persistent provenance through save/reload, Create from Selection, explicit-level Apply, Revert, fallback Unpack Completely, scene-reference rejection, exact source before/after journaling for Undo/Redo, external source conflict protection, injected source/scene commit failures, rollback, and deterministic startup recovery.

The current Windows strict Release and MSVC AddressSanitizer matrices both pass **45/45 tests**; ASan completes in **368.80 seconds**. Ubuntu's latest pre-DPE-ARCH-0008 Release and Clang AddressSanitizer matrices passed **36/36**; no current POC F matrix exists for Ubuntu or macOS.

The gate remains incomplete. Nested-level Apply does not yet cascade child source hashes/revisions through containing prefab sources; Create from Selection requires a locally owned subtree; Apply rejects instance-added entities until durable source-ID allocation is implemented; not every override form has a dedicated case; and partial Unpack, the complete three-level public Qt/accessibility scenario, and platform-specific failure recovery remain unproven. This ADR remains `Proposed`.

## DPE-ARCH-0016 Project View drag refinement

The Project View presents prefab assets and Hierarchy-to-Project creation through the existing version-1 drag envelope. Prefab instantiation into Scene/Hierarchy remains linked and command-backed; creating a prefab from a locally owned Hierarchy root remains Scene-dependent and uses the established atomic prefab/scene owner. Project-only asset/folder organization does not require a Scene and cannot accidentally enter the prefab path. Stale, cross-project, linked-source, ambiguous, or incompatible drops remain fail-before-mutation cases. No prefab format or mapping contract changes, and ADR-0013 remains `Proposed`.

## Related decisions

- ADR-0002: Hybrid Entity/Component Model and Ownership
- ADR-0005: Cross-Language Component Metadata and Inspector Reflection
- ADR-0006: JSON Versioning, Migration, and Unknown-Data Preservation
- ADR-0008: Command Transactions, Validation, Undo/Redo, Dirty State, and Automation
