# ADR-0002: Hybrid Entity/Component Model and Ownership

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

The editor needs stable identities, hierarchy, inspection, undo, mixed C++/C# components, serialization, and missing-component recovery. Runtime systems may also need dense data-oriented caches. Exposing either language's object model or a pure runtime ECS as the public authoring model would weaken the other requirements.

## Decision

- Use an object-oriented authoring graph plus language-neutral serialized component records. Derived runtime stores/caches may be data-oriented but remain internal and disposable.
- A `World` owns loaded scenes and runtime services. A `Scene` owns entity records and root order. An entity belongs to one scene and contains a persistent RFC 9562 UUID, name, enabled state, nullable parent ID, ordered children, and ordered component records.
- Every component record contains stable type UUID, diagnostic name, schema version, enabled state, runtime owner (`native`, `managed`, or `data-only`), and JSON property payload.
- Native storage owns native component instances. The managed worker/GC owns managed instances. Components reference entities/components through stable IDs or validated handles, never memory addresses or cross-language inheritance.
- Spatial entities use framework-neutral `Transform` data. The canonical model is right-handed, Y-up, negative-Z-forward; 2D uses X/Y with Z depth.
- All authoritative graph/component mutations pass through validated commands and transactions.

## Consequences

The model prioritizes authoring, interoperability, migration, and data recovery while allowing hot runtime subsystems to optimize internally. Runtime caches require explicit rebuild/synchronization rules and cannot become a second persistence authority.

## Validation and acceptance gate

POC C must prove native/managed records share IDs/schema, stable-ID renames avoid migration, real schema changes require migration, and missing/newer records round-trip opaquely. S1.0 tests must prove ownership and lifecycle behavior.

Current evidence (2026-07-24): Windows and Ubuntu pass UUID identity, hierarchy, entity/component enabled state, native/managed records, command validation, atomic transaction commit/rollback, and opaque-record preservation tests. macOS arm64 remains pending.
