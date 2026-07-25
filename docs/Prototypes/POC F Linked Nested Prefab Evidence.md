# POC F Linked Nested Prefab Evidence

> **Evidence status:** Windows and Ubuntu native domain/editor-service proof passed; complete nested/newer-source and macOS matrix pending  
> **Recorded:** 2026-07-25  
> **Architecture baseline:** `DPE-ARCH-0006`  
> **Related ADRs:** ADR-0002, ADR-0006, and ADR-0013

## Purpose

Prove that linked prefab documents can retain deterministic source identity, materialize three-level nested graphs with stable instance identities, carry normalized overrides, recover from missing sources, and preserve opaque future data.

## Implemented proof

- `dpe.prefab` version 1 deterministic UTF-8 JSON serialization.
- Canonical SHA-256 source revisions that exclude the self-referential revision field.
- Prefab-local entities, direct nested instances, sorted dependencies, stable path-qualified entity mappings, normalized overrides, and fallback entities.
- Recursive materialization with direct/indirect dependency-cycle detection plus depth and expanded-entity guards.
- Three-level scene-to-middle-to-leaf resolution with duplicate path-safe identity.
- Entity rename and component-property overrides across a nested instance path.
- Explicit source-level Apply, source rebase with caller-supplied stable allocations, Revert Selected, Revert All, and Unpack Completely.
- Missing-source resolution from last-known fallback entities.
- Round-trip preservation of unknown component schema versions and vendor payloads.
- Editor PrefabService operations for Instantiate, Create from Selection, explicit-level Apply, Revert Selected/All, Repair/Rebase, one-level Unpack, and Unpack Completely, all routed through scene transactions.
- Source before/after journaling tied to scene history positions. Undo and Redo restore exact prefab-source bytes, external source conflicts block an overwrite, and a failed scene rematerialization restores the source before-image.
- Apply translates instance UUID relationships and metadata-declared entity references back to prefab-local identities and rejects scene-local external references without writing the source.

## Windows evidence

The native `s2.linked_prefabs` and `poc_f.linked_prefabs` tests and the editor-service `s2.prefab_editor_service` and `poc_f.prefab_editor_workflows` tests pass under MSVC with warnings treated as errors. The native executable verifies:

- canonical output is byte-stable after read/write;
- opaque component properties, schema versions, and vendor payloads survive;
- the three resolved entity identities match their stable mappings;
- nested rename/property overrides affect the intended leaf only;
- Unpack Completely produces the same ordered flattened records;
- removing a source selects the retained fallback and records that recovery path;
- an indirect source cycle emits `DPE.PREFAB.CYCLE`;
- Apply consumes the targeted normalized override and advances the source revision;
- rebase preserves existing mappings and overrides while using the supplied ID for a new source entity; and
- selected/all override reversion is deterministic.

The editor-service executable additionally verifies persistent linked provenance after save/reload, property override capture and selected reversion, missing-source fallback plus Unpack Completely, source-local relationship/reference translation during Apply, exact source Undo/Redo, Create-from-Selection source Undo/Redo, rejection of external scene references, and source rollback when rematerialization fails.

These tests are included in the complete Windows matrices: Release passed **36/36 tests in 118.39 seconds**, and MSVC AddressSanitizer passed **36/36 tests in 134.93 seconds**.

## Ubuntu evidence

The same native and editor-service aliases pass in both complete Ubuntu matrices: Release passed **36/36 tests in 102.20 seconds**, and Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. The native libraries build as position-independent code, and sanitizer runtime settings propagate through managed workers and native-host child processes.

## Evidence boundaries

- The editor service exists and is tested, but complete public Qt menu/dialog/drag-drop coverage for every prefab command is still required.
- Nested-level Apply does not yet cascade changed child source hashes/revisions through containing prefab sources.
- Create from Selection currently accepts only a locally owned subtree; linked children must be unpacked first.
- Apply deliberately rejects instance-added entities until the command can allocate durable prefab-source IDs safely.
- Injected failure coverage currently proves source restoration when scene rematerialization fails; the full multi-document staging/commit/startup-recovery matrix remains open.
- Not every normalized override class has a dedicated case yet.
- Newer/incompatible-source handling and the complete three-level editor workflow matrix remain incomplete even though the native three-level materialization proof passes.
- No current POC F run exists on macOS, so two-platform evidence does not establish three-platform deterministic behavior.

## Remaining gate work

- Drive every editor-facing linked-prefab workflow through public Qt actions and drag/drop.
- Add cases for duplicate uses of one source, every entity/component override type, explicit nested Apply targets, partial Unpack, newer-source incompatibility, unresolved rebase conflicts, external-reference rejection, guard limits, and injected multi-document failures.
- Run Release and native AddressSanitizer evidence on macOS 14+ arm64.
- Review all evidence before accepting ADR-0002, ADR-0006, or ADR-0013.

## Conclusion

The stable mapping, deterministic document, recursive materialization, source-journaled editor operations, fallback, opaque preservation, and unpack foundation pass in Windows and Ubuntu Release/native AddressSanitizer matrices. POC F is not closed until the deeper/newer-source, complete public Qt, recovery, override, and macOS acceptance matrices pass.
