# ADR-0020: Asset Import and Cache Integrity

> **Status:** Proposed
> **Design revision:** `DPE-ARCH-0014`
> **Last reviewed:** 2026-07-27

## Context

Dragon Pixel Engine currently has narrow, contained PNG intake for TileSets, but Slice 3 requires general asset creation, import, reimport, rename, move, and removal. Those operations cross user-selected source locations, project-owned files, importer code, durable sidecars, dependency graphs, and disposable derived data. A failed or malicious importer, a link escape, a case-only collision, or a corrupt cache must not modify an external source or invalidate saved authoring state.

## Decision

- `AssetService` owns asset discovery and dependency state. `CommandService` or an explicit operation transaction owns every authoritative create, import, reimport, rename, move, and remove commit; panels, importers, plugins, Python, and AI may only propose those operations.
- `dpe.asset` version 3 records a stable asset UUID; source ownership as `copied`, `linked`, or `generated`; source and import hashes; importer identity/version and normalized settings; a deterministic cache key; ordered dependency identities/revisions; and recovery state. Stable IDs, rather than paths or cache locations, remain the durable reference boundary.
- Copied sources become project-owned and are committed only into declared asset roots. Linked sources remain explicit read-only external references with portability and availability diagnostics. Generated sources identify their generator and effective inputs.
- Every project-relative destination is normalized and containment-checked after resolving symbolic links, junctions, and reparse points. Escapes, reserved Dragon Pixel metadata locations, duplicate stable IDs, and case-folding collisions fail during validation before any mutation.
- Importer code runs in a disposable worker with read-only access to the selected source and write access only to a per-operation staging directory. It returns structured diagnostics plus deterministic sidecar, preview, dependency, cache-output, and hash manifests. The editor validates those results and performs the recoverable atomic commit.
- A cache key binds every effective input that can change derived output: source/import hash, importer identity and version, normalized settings, ordered dependency revisions, output contract version, and any declared platform/toolchain discriminator. Reimport never replaces a valid sidecar or cache entry until the complete staged result validates.
- `.dragonpixel/Cache` is disposable and non-authoritative. Project documents, source ownership records, and asset sidecars are sufficient to reconstruct it. Missing, stale, corrupt, or interrupted cache entries produce diagnostics and rebuild work, not authoring-data loss.
- External-change watching is advisory. A detected source change marks the asset stale and offers a validated reimport; it does not silently rewrite sidecars, dependencies, or cache state.

## Consequences and tradeoffs

- Deterministic manifests and cache keys make rebuilds, diagnostics, and failure recovery reviewable, at the cost of hashing work and stricter importer reproducibility requirements.
- Copied assets are portable but duplicate storage. Linked assets avoid a copy but carry explicit availability and packaging risks and can never be edited through the import capability.
- Disposable workers and staged commits add process and disk overhead, but importer failure cannot directly corrupt the project.
- Cross-platform case-folding and resolved-link checks reject some layouts that a case-sensitive host filesystem would otherwise permit. This keeps one project safely usable on every supported baseline.

## Security and ownership

- The editor owns stable IDs, validation, the authoritative sidecar/dependency graph, and the final commit. Importer output is untrusted staged data until the editor verifies schema, hashes, declared outputs, containment, collisions, and dependency limits.
- Import workers receive the minimum source-read and staging-write capabilities for one operation. They receive no general project-write capability, and cancellation or hard termination cannot promote partial output.
- A linked source remains owned by the user or external system. Import, reimport, rename, move, removal, cache cleanup, and rollback never authorize a write to that source.
- Rename, move, and removal present affected-reference summaries and preserve stable-ID diagnostics. Audit records contain operation/correlation IDs, capabilities, input/output hashes, decisions, and recovery disposition without secrets or private prompt content.

## DPE-ARCH-0014 implementation boundary

The first POC O implementation increment makes AssetService the owner of Project Browser folder creation, copied/read-only-linked import, rename, move, duplicate, dependency impact, operation-owned trash, restore, and immutable runtime binding. PNG/JPEG inputs become renderable sprite assets; unsupported inputs may be preserved as generic assets with importer diagnostics. Removal moves project-owned bytes to `.dragonpixel/Trash/<operation-id>` with an exact recovery manifest and never writes a linked source. Project Browser is a two-pane service-backed view and every OS/internal drop is only an operation proposal. Imported 3D, audio, font, complete cache reconstruction, and the full three-platform POC O matrix remain open.

## Validation and evidence gate

**POC O: Asset Import and Cache Integrity** must pass on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. It must prove copied, linked, and generated ownership; traversal/link/reparse-point and case-collision rejection; stable sidecars and cache keys; dependency, duplicate, cycle, missing-source, and reimport behavior; injected interruption and corrupt-cache recovery; complete cache reconstruction from authoritative data; and real MonoGame/KNI adapter consumption reported independently.

The existing contained-PNG TileSet workflow and focused Tiled-import worker are useful implementation evidence but do not satisfy general imported media, complete reimport/cache reconstruction, linked/generated ownership across every operation, cross-platform filesystem safety, or the full POC O matrix. POC O remains incomplete, so this ADR remains `Proposed`.

Focused importer-isolation evidence (2026-07-27): `feature/tiled-tilemap-import` launches `DragonPixelTiledImporterWorker` without a shell into an operation-owned temporary directory and gives it no project-write path. The editor assigns all three asset identities and validates the result version/importer, exact output inventory, containment, regular-file status, IDs, limits, statistics, source stability, native documents, PNG decode, atlas bounds, and dependency graph before `AssetService` publishes six collision-free files through one operation. Actual-worker success and injected start/crash/timeout/cancel, tampered path, invalid PNG, collision, and no-partial-publication paths are covered. The focused Windows Release matrix passes 9/9 in 6.34 seconds and MSVC AddressSanitizer passes 9/9 in 21.37 seconds. The 189-record production-style bundle contains the importer and hash-verifies with no missing, mismatched, or unlisted files. Reimport, source watching, full cache reconstruction/corruption, broader formats, hard-link/reparse races beyond current owners, and current Ubuntu/macOS/hosted evidence remain open; this does not promote POC O or this ADR.
