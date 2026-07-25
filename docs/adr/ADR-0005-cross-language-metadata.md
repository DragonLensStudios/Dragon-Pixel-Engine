# ADR-0005: Cross-Language Metadata and Inspector Schema

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

The Inspector must expose C++ and C# properties consistently without loading arbitrary project code into the editor. Runtime reflection and compiler RTTI are not portable contracts and are unsuitable for AOT/Unity sharing.

## Decision

- Define one versioned JSON metadata schema shared by native and managed generators.
- Descriptors include stable type/property UUIDs, diagnostic/display names, category, value type, defaults, ranges, units, nullability, asset/entity reference kind, visibility/read-only flags, schema/generator versions, and custom drawer keys.
- C# attributes plus a source generator emit manifests and registration code. C++ explicit registration declarations/macros plus build-time generation emit the same schema.
- The editor reads and validates manifests through `IMetadataService`; it does not load game assemblies or native project libraries for inspection.
- Stable IDs, not names, determine identity. Rename is metadata-only; representation changes require explicit schema migration.
- Runtime reflection may add diagnostics but cannot replace generated manifests or alter saved identity.

## Consequences

Build-time generation adds tooling and golden-fixture maintenance, but provides one AOT-safe Inspector contract, fast startup, deterministic validation, and language symmetry.

## Validation and acceptance gate

POC C must validate equivalent native/managed component manifests against one schema and prove property IDs/types/defaults/references/precision plus rename and schema-change behavior in a headless Inspector model.

Current evidence (2026-07-24): Windows and Ubuntu pass the shared version-2 schema, UUID type identities, generated managed/native manifests, stable-ID rename, migration, headless inspection, and Qt Inspector paths. macOS arm64 remains pending.
