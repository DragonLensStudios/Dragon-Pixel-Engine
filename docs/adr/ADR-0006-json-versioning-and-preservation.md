# ADR-0006: JSON Versioning, Migration, and Unknown-Data Preservation

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

Projects must be inspectable, diffable, deterministic across languages/platforms, safely migratable, and resilient when plugins, components, prefab sources, or editor versions are missing or newer than the current editor. Linked prefab create/apply operations may update more than one authoritative document and therefore need a failure boundary stronger than a sequence of independent saves.

## Decision

- Authoritative project, scene, prefab, asset metadata, workspace/user state, and migration reports use strict deterministic UTF-8 JSON. Binary outputs are disposable caches or measured transports only.
- Durable documents contain schema URI, integer format version, document UUID, producer engine version, and payload. Document and component schema versions evolve independently.
- Writers use stable field ordering, invariant numeric formatting, and deterministic ordering where the domain has no ordering semantics. Semantically ordered collections such as roots, siblings, components, nested instances, and override operations preserve their explicit order.
- `dpe.scene` version 3 adds explicit root/sibling ordering, physics settings, and `prefabInstances`. Its version 2 migration preserves every existing entity/component record and derives deterministic order for fields that did not previously exist.
- `dpe.prefab` version 1 stores one root, ordered prefab-local entities/components, direct linked nested instances, dependencies, normalized overrides, stable entity mappings, source revisions, and last-known-good fallback records. A canonical source revision is the lowercase SHA-256 of the canonical semantic JSON, excluding the self-referential revision field and recovery/journal metadata.
- Unknown/missing/newer component and prefab records remain visible opaque JSON subtrees with original type/source ID, name, version, mappings, overrides, fallback data, and payload preserved structurally through load/save. Renames keep stable IDs; replacements use explicit aliases/migrations.
- Source refresh/rebase is an explicit deterministic command. It preserves surviving entity mappings, allocates new materialized UUIDs before mutation, reapplies normalized overrides in canonical order, and retains unresolved overridden data with structured diagnostics rather than discarding it.
- Direct or indirect prefab dependency cycles are rejected. Load and migration also enforce explicit expansion-depth and materialized-entity-count guards; a rejected or guarded instance retains its durable record and fallback data.
- Migrations are ordered, deterministic, side-effect-free transformations with source/target versions, tool build, hashes, warnings, and decision records. Migration analysis never mutates the inspected source document.
- A single-document save validates first, writes and flushes a sibling temporary file, replaces atomically where supported, and retains bounded recovery/journal data. Failed load, migration, or save cannot overwrite the last valid document.
- Prefab create/apply uses an atomic multi-document save transaction. It validates the complete change set, writes and flushes every staged document, records a recovery manifest and backups, then commits replacements as one recoverable unit. Any staging or commit failure rolls back every replaced document or leaves enough journal state for deterministic startup recovery; partial success is never reported as committed.

## Consequences

JSON is larger and slower than binary, and structural preservation may canonicalize formatting, but it provides reviewability, cross-runtime implementation, migration evidence, and data-loss resistance. Prefab fallbacks and multi-document journals add storage and implementation cost. In return, missing sources, newer sources, failed applies, and interrupted saves remain diagnosable and recoverable without silently flattening or losing authoring intent.

## Validation and acceptance gate

POC C and domain tests must cover known, renamed, missing, newer/version-mismatched records, numeric precision, deterministic output, explicit migrations, atomic-save failure, and recovery behavior. POC F must additionally cover deterministic `dpe.scene` v2-to-v3 migration, canonical `dpe.prefab` v1 hashing, three-level nesting, duplicate sources, stable mappings, every override class, cycles/guards, missing/newer-source fallback, rebase conflicts, and injected failure at each multi-document staging/commit boundary.

Current evidence (2026-07-25): Windows and Ubuntu now pass deterministic `dpe.scene` version 3 and `dpe.prefab` version 1 coverage in addition to older scene migration, known/renamed/missing/newer component records, enabled-state persistence, opaque preservation, atomic single-document recovery, and full project save/close/reopen. Current POC F tests exercise canonical prefab revisions, three-level materialization, duplicate nested sources, stable mappings, normalized overrides, cycles/guards, missing-source fallback, rebase, apply/revert, both unpack modes, and recoverable prefab writes. The full matrices pass on Windows Release (36/36 in 118.39 seconds), Windows MSVC AddressSanitizer (36/36 in 134.93 seconds), Ubuntu Release (36/36 in 102.20 seconds), and Ubuntu Clang AddressSanitizer (36/36 in 101.97 seconds).

The current suite still leaves newer-source fallback, exhaustive migration fixtures, and injected failure at every multi-document staging/commit boundary open, and it has not run on macOS arm64. This ADR remains `Proposed`.

Primary specification: [RFC 8259 JSON](https://www.rfc-editor.org/rfc/rfc8259).
