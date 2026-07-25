# ADR-0006: JSON Versioning, Migration, and Unknown-Data Preservation

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

Projects must be inspectable, diffable, deterministic across languages/platforms, safely migratable, and resilient when plugins/components are missing or newer than the editor.

## Decision

- Authoritative project, scene, prefab, asset metadata, workspace/user state, and migration reports use strict deterministic UTF-8 JSON. Binary outputs are disposable caches or measured transports only.
- Durable documents contain schema URI, integer format version, document UUID, producer engine version, and payload. Document and component schema versions evolve independently.
- Writers use stable field ordering, invariant numeric formatting, and deterministic ordering where the domain has no ordering semantics.
- Unknown/missing/newer component records remain visible opaque JSON subtrees with original type ID/name/version/payload preserved structurally through load/save. Renames keep stable IDs; replacements use explicit aliases/migrations.
- Migrations are ordered, deterministic, side-effect-free transformations with source/target versions, tool build, hashes, warnings, and decision records.
- Saves validate first, write/flush a sibling temporary file, replace atomically where supported, and retain bounded recovery/journal data. Failed load/migration/save cannot overwrite the last valid document.

## Consequences

JSON is larger and slower than binary, and structural preservation may canonicalize formatting, but it provides reviewability, cross-runtime implementation, migration evidence, and data-loss resistance.

## Validation and acceptance gate

POC C and Slice 1 tests must cover known, renamed, missing, newer/version-mismatched records, numeric precision, deterministic output, one explicit migration, atomic-save failure, and recovery behavior.

Current evidence (2026-07-24): Windows and Ubuntu pass deterministic scene format version 2, version 1 migration, known/renamed/missing/newer records, enabled-state persistence, atomic save/recovery, and full project save/close/reopen with visible read-only opaque data. macOS arm64 remains pending.

Primary specification: [RFC 8259 JSON](https://www.rfc-editor.org/rfc/rfc8259).
