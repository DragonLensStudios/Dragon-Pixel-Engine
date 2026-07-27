# ADR-0016: Orthogonal Tile Authoring and Runtime Lowering

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0009`

## Context

Dragon Pixel needs a usable 2D tile workflow without storing large cell arrays inside scenes, leaking framework texture types into contracts, or giving tile tools a second mutation path. Tile identities must survive atlas edits and missing dependencies must not destroy painted data.

## Decision

- `dpe.tileset` version 1 owns a stable asset UUID, one texture asset reference, orthogonal slicing settings, pixels-per-unit, stable tile UUIDs, source rectangles, display names, and optional rectangular collision definitions.
- `dpe.tilemap` version 1 owns a stable asset UUID, ordered layers, TileSet dependencies, and sparse deterministic 32-by-32 chunks. Cells reference tile UUIDs, not atlas indexes.
- A `Tilemap2D` component references a reusable tilemap asset. An optional `TilemapCollider2D` component supplies Box2D material/layer/mask settings.
- External PNGs selected by the TileSet wizard are copied into a contained project texture directory; existing contained PNGs remain in place. General asset import remains deferred.
- Palette paint, erase, rectangle, flood fill, eyedropper, and selection operate through preview/commit/cancel tile commands. Dirty tilemap documents join atomic save/recovery and project close prompts.
- Runtime snapshot generation resolves tiles and immutable texture bindings. Rendering batches by texture/layer in MonoGame and KNI. Collision cells lower to static Box2D colliders owned by the tilemap entity, so the public physics ABI does not change.
- Missing textures, TileSets, or tile IDs remain diagnosable and round-trip losslessly. Fill and expansion operations are bounded to loaded/explicit map extents.

## Consequences

Reusable sparse assets keep scenes small and permit chunk-level Undo and runtime work. The editor must coordinate multi-document dirty state, dependency watching, texture intake, and chunk serialization. Isometric, hex, rule, animated, terrain, and 3D tiles remain separate decisions.

## Validation and acceptance gate

POC K must prove deterministic formats, stable IDs across reslicing, contained PNG intake, all public Qt tools, chunk-boundary Undo/Redo, save/recovery, missing dependency preservation, real MonoGame/KNI device pixels and picking, and Box2D collision on Windows, macOS, and Linux. This ADR remains `Proposed` until that evidence is reviewed.

Current Windows evidence (2026-07-25): native tests prove deterministic TileSet/Tilemap v1 documents, stable tile IDs, sparse 32-by-32 chunks, ordered layers, and command-backed edits. Public Qt tests exercise the contained-PNG wizard, deterministic grid validation, non-overwrite behavior, palette tools, dirty state, Undo/Redo, atomic save/reopen, and external-change diagnostics. Snapshot-v4 flattening embeds immutable PNG bytes and tile source rectangles; both framework adapters load/cache those textures and batch tile draws, while resolved collision cells lower to engine-owned static Box2D data. The current Windows strict Release and MSVC AddressSanitizer matrices both pass 45 of 45 tests.

Explicit device-pixel/picking assertions for authored PNG tiles, collision-contact acceptance through both adapters, broader missing/dependency recovery, and the current Ubuntu/macOS POC K matrices remain outstanding. This ADR remains `Proposed`.

Focused Tiled-import evidence (2026-07-27): `feature/tiled-tilemap-import` adds an isolated Tiled JSON converter for the current orthogonal v1 contract without changing `dpe.tileset`, `dpe.tilemap`, scene, snapshot, or adapter formats. Finite/infinite numeric tile layers, negative chunks, stable IDs, Y-up conversion, and all eight orthogonal Tiled GID transform combinations round-trip through the authoritative native readers. A real-worker Qt workflow publishes the texture/TileSet/tilemap dependency chain and opens it in the existing Tile Palette without mutating the scene. The focused Windows Release matrix passes 9/9 in 6.48 seconds and the corresponding MSVC AddressSanitizer matrix passes 9/9 in 21.05 seconds. Multiple TileSets, XML, non-orthogonal maps, object layers/collision conversion, animation/rules/terrain, reimport, current Ubuntu/macOS POC K matrices, and the full device/collision acceptance gate remain open, so this ADR remains `Proposed`.
