# ADR-0002: Hybrid Entity/Component Model and Ownership

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

The editor needs stable identities, hierarchy, inspection, undo, mixed C++/C# components, serialization, linked nested prefabs, and missing-component recovery. Runtime systems may also need dense data-oriented caches. Exposing either language's object model or a pure runtime ECS as the public authoring model would weaken the other requirements.

## Decision

- Use an object-oriented authoring graph plus language-neutral serialized component records. Derived runtime stores/caches may be data-oriented but remain internal and disposable.
- A `World` owns loaded scenes and runtime services. A `Scene` owns its local entity records, root/sibling order, physics settings, and linked prefab-instance records. An entity belongs to one authoritative scene or prefab document and contains a persistent RFC 9562 UUID, name, enabled state, nullable parent ID, sibling order, and ordered component records.
- Component identity is the composite `(entity UUID, component type UUID)`. An entity can contain at most one component record for a given type UUID. Every component record also contains a diagnostic name, schema version, enabled state, runtime owner (`native`, `managed`, or `data-only`), and JSON property payload.
- Native storage owns native component instances. The managed worker/GC owns managed instances. Components reference entities/components through stable UUIDs or validated handles, never memory addresses or cross-language inheritance.
- Scene- and prefab-local entities remain separate from linked prefab instances in durable storage. The authoring view materializes an instance by resolving its source, stable mappings, nested-instance path, and overrides. Missing or incompatible sources use the last successfully resolved fallback records without transferring persistence authority to the materialized view.
- Prefab source entities retain stable prefab-local UUIDs. Each instance maps `(nested-instance path, source entity UUID)` to a persistent materialized entity UUID so duplicate and nested uses of the same source cannot collide. Linked-prefab ownership, override, rebase, and recovery rules are defined in ADR-0013.
- Runtime snapshots flatten prefab instances into ordinary entity/component records. MonoGame, KNI, and native runtime systems receive no prefab provenance and cannot modify authoritative authoring records.
- Spatial entities use framework-neutral `Transform` data. The canonical model is right-handed, Y-up, negative-Z-forward; 2D uses X/Y with Z depth.
- All authoritative graph, component, prefab, and ordering mutations pass through validated commands and transactions. A command that changes ownership or identity must preserve before-images, opaque payloads, ordering, mappings, and references for rollback and Undo.

## Consequences

The model prioritizes authoring, interoperability, migration, prefab reuse, and data recovery while allowing hot runtime subsystems to optimize internally. Runtime caches and materialized prefab views require explicit rebuild/synchronization rules and cannot become a second persistence authority. Composite component identity keeps lookup and override addressing deterministic but excludes multiple components of the same type on one entity unless a later ADR changes the identity model.

## Validation and acceptance gate

POC C must prove native/managed records share IDs/schema, stable-ID renames avoid migration, real schema changes require migration, and missing/newer records round-trip opaquely. POC F must prove three-level nested prefab materialization, duplicate nested sources, stable mappings, all override classes, rebase conflict recovery, and lossless fallback behavior. Domain tests must prove composite component uniqueness, ownership, lifecycle, hierarchy ordering, rollback, and flattened snapshot identity.

Current evidence (2026-07-25): Windows and Ubuntu now pass UUID identity, hierarchy and sibling ordering, entity/component enabled state, native/managed records, command validation, transaction rollback, opaque-record preservation, `dpe.scene` version 3, and the current POC F linked-prefab ownership suite. That suite covers three-level materialization, duplicate nested sources, stable mappings, normalized override operations, cycle/depth/entity guards, missing-source fallback, rebase, apply/revert, and unpack behavior. The full matrices pass on Windows Release (36/36 in 118.39 seconds), Windows MSVC AddressSanitizer (36/36 in 134.93 seconds), Ubuntu Release (36/36 in 102.20 seconds), and Ubuntu Clang AddressSanitizer (36/36 in 101.97 seconds).

Current evidence still does not close every POC F acceptance case, including deeper apply-target semantics, newer-source recovery, and exhaustive multi-document failure injection, and the current POC F matrix has not run on macOS arm64. This ADR therefore remains `Proposed`.
