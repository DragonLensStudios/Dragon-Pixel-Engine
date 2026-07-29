# ADR-0020: Asset Import and Cache Integrity

> **Status:** Proposed
> **Design revision:** `DPE-ARCH-0016`
> **Last reviewed:** 2026-07-29

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

Focused importer-isolation evidence (2026-07-27): `feature/tiled-tilemap-import` launches `DragonPixelTiledImporterWorker` without a shell into an operation-owned temporary directory and gives it no project-write path. The editor assigns all three asset identities and validates the result version/importer, exact output inventory, containment, regular-file status, IDs, limits, statistics, source stability, native documents, PNG decode, atlas bounds, and dependency graph before `AssetService` publishes six collision-free files through one operation. Actual-worker success and injected start/crash/timeout/cancel, tampered path/identity, invalid PNG, collision, and no-partial-publication paths are covered. The focused Windows Release matrix passes 9/9 in 6.48 seconds and MSVC AddressSanitizer passes 9/9 in 21.05 seconds. The 189-record production-style bundle contains the importer and hash-verifies with no missing, mismatched, or unlisted files. Reimport, source watching, full cache reconstruction/corruption, broader formats, hard-link/reparse races beyond current owners, and current Ubuntu/macOS/hosted evidence remain open; this does not promote POC O or this ADR.

Focused generated-Tilemap evidence (2026-07-27): `feature/tilemap-authoring-workspace` extends `AssetService` with a narrowly generated empty-Tilemap operation. It re-resolves one structurally valid indexed TileSet, reparses its native document and stable ID, generates stable Tilemap/layer UUIDs, binds the TileSet dependency revision, stages the `dpe.tilemap` v1 source and `dpe.asset` v3 sidecar together, rejects invalid names/types/documents/collisions, validates the complete post-commit project index, and removes only its newly published outputs if that validation unexpectedly fails. Focused success and failure tests prove no partial authoritative files. The complete strict Windows Release preset passes 60/60 in 446.31 seconds; the final feature-focused MSVC AddressSanitizer workflow passes 6/6 stages in 42.269 seconds without a sanitizer finding. The broader ASan preset passes 59/60 with the separate open POC J Qt-painted-FPS gate failing under instrumentation; AssetService, POC O, and both complete editor interaction aliases pass. This does not implement reimport/cache reconstruction, broaden formats, close three-platform POC O, or promote this ADR from `Proposed`.

## DPE-ARCH-0015 Tilemap asset/import expansion

- Asset v3 indexes `tilepalette` documents and the additional TileSet/texture/palette/Tilemap dependency graph without changing its public format version.
- Guided image setup stages the contained texture, TileSet v2, TilePalette v1, Tilemap v2, and their sidecars as one validated asset operation before scene attachment. Pre-publication failure leaves no output; scene-only attachment failure retains valid assets and an actionable recovery path.
- The isolated Tiled JSON worker may emit multiple contained textures/TileSets plus one palette and Tilemap for the accepted v2 subset. The editor assigns and validates every identity, output path, dependency, native document, decoded image, and limit before atomic publication.
- XML, encoded/compressed layers, object/image-collection layers, reimport, and unrepresentable rule/terrain semantics remain fail-before-publication cases. This expanded implementation evidence cannot close POC O without cache reconstruction/reimport and the complete three-platform matrix.

Focused DPE-ARCH-0015 evidence (2026-07-28): the expanded isolated Tiled JSON worker and editor publication path accept multiple inline/external atlas TileSets, orthogonal/isometric/staggered/hexagonal maps, tile animation, conservatively representable terrain/Wang rules, optional Isometric Z-as-Y conversion, and automatic palette handoff. Every staged texture, TileSet, palette, Tilemap, identity, dependency, decoded image, and native document is validated before the existing atomic AssetService commit; unsupported input and scene-only attachment failure retain the documented no-partial-output or retained-valid-asset behavior. The complete local Windows matrices pass 63/63 in 570.78 seconds Release and 63/63 in 895.30 seconds under MSVC AddressSanitizer, and the 189-record production bundle includes/hash-verifies the importer with a passing packaged MonoGame smoke. Reimport/cache reconstruction, the rejected input classes above, and the complete hosted three-platform POC O matrix remain open, so this ADR remains `Proposed`.

## DPE-ARCH-0016 Project View operation refinement

Project View remains a proposal surface over `AssetService`. OS drops import into the validated visible folder; stable asset and folder drags propose contained moves and do not depend on an open Scene. Destination resolution, project/source revision, containment, case/collision, recursive-folder, stable-ID, staging, recovery, and diagnostic rules remain unchanged. The view never moves bytes directly, and a rejected or failed drop cannot partially update the index or authoritative files. This refinement adds Project View usability and regressions but does not implement reimport/cache reconstruction or close POC O; ADR-0020 remains `Proposed`.
