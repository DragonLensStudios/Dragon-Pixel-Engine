# ADR-0016: 2D Tilemap Authoring and Runtime Lowering

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0015`

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

## DPE-ARCH-0015 expanded decision

- `dpe.tileset` version 2, `dpe.tilepalette` version 1, `dpe.tilemap` version 2, and runtime snapshot version 5 expand the v1 orthogonal proof without invalidating it. Readers preserve v1 and unknown/newer records; writers upgrade only after explicit mutation/save.
- One map retains internal layers and sparse chunks, but supports five grid layouts, multiple TileSets/textures, qualified tile references, rich cell properties, renderer settings, deterministic typed tiles/brushes, layout-aware collision, and universal logical palettes.
- One portable grid implementation owns projection, inverse picking, neighbors, lines, sorting, and collision geometry. MonoGame and KNI consume the same neutral snapshot while keeping framework objects private.
- TileDocumentService remains the sole loaded tile-workspace mutation owner. Palette/Scene/Inspector operations, object-brush scene commands, unified Undo, and atomic Save All cannot become alternate file writers.
- Project-local custom tile/brush modules run only through `dpe_tile_extension_plugin_v1` in disposable workers. Unknown or failed types remain opaque, visible, and non-destructive.
- The isolated Tiled JSON seam may populate v2 documents and palettes for the explicitly accepted subset; Tiled concepts do not enter runtime contracts.

## Validation and acceptance gate

POC K must prove deterministic v1/v2/palette formats, stable IDs across reslicing, contained image intake, all public Qt tools and grid layouts, chunk-boundary and compound Undo/Redo, save/recovery, missing dependency/extension preservation, a representative 1,024-by-1,024 occupied sparse map, real MonoGame/KNI device pixels/picking/animation/rules, and Box2D collision/contact behavior on Windows, macOS, and Linux. The existing 30 FPS and latency thresholds remain unchanged. This ADR remains `Proposed` until that evidence is reviewed.

Current Windows evidence (2026-07-25): native tests prove deterministic TileSet/Tilemap v1 documents, stable tile IDs, sparse 32-by-32 chunks, ordered layers, and command-backed edits. Public Qt tests exercise the contained-PNG wizard, deterministic grid validation, non-overwrite behavior, palette tools, dirty state, Undo/Redo, atomic save/reopen, and external-change diagnostics. Snapshot-v4 flattening embeds immutable PNG bytes and tile source rectangles; both framework adapters load/cache those textures and batch tile draws, while resolved collision cells lower to engine-owned static Box2D data. The current Windows strict Release and MSVC AddressSanitizer matrices both pass 45 of 45 tests.

Explicit device-pixel/picking assertions for authored PNG tiles, collision-contact acceptance through both adapters, broader missing/dependency recovery, and the current Ubuntu/macOS POC K matrices remain outstanding. This ADR remains `Proposed`.

Focused Tiled-import evidence (2026-07-27): `feature/tiled-tilemap-import` adds an isolated Tiled JSON converter for the current orthogonal v1 contract without changing `dpe.tileset`, `dpe.tilemap`, scene, snapshot, or adapter formats. Finite/infinite numeric tile layers, negative chunks, stable IDs, Y-up conversion, and all eight orthogonal Tiled GID transform combinations round-trip through the authoritative native readers. A real-worker Qt workflow publishes the texture/TileSet/tilemap dependency chain and opens it in the existing Tile Palette without mutating the scene. The focused Windows Release matrix passes 9/9 in 6.48 seconds and the corresponding MSVC AddressSanitizer matrix passes 9/9 in 21.05 seconds. Multiple TileSets, XML, non-orthogonal maps, object layers/collision conversion, animation/rules/terrain, reimport, current Ubuntu/macOS POC K matrices, and the full device/collision acceptance gate remain open, so this ADR remains `Proposed`.

Focused tilemap-workspace evidence (2026-07-27): `feature/tilemap-authoring-workspace` adds atomic empty-Tilemap creation from an indexed TileSet, a Transform + Tilemap2D GameObject preset, Project-to-Scene/Hierarchy creation and Inspector reassignment, parsed Project details, real atlas thumbnails/canvas cells, stable-ID layer add/rename/visibility/reorder/remove with Undo, authored flip/quarter-turn brush state, and selected-transform-aware painting in the 2D Scene View. Scene strokes interpolate without gaps, cancel on Escape/mode/Play changes, refuse provisional saves, and coalesce immutable preview refreshes. Existing snapshot-v4 pixels-per-unit lowering is used by the same authoring grid and adapter world sizing. The complete strict Windows Release preset passes 60/60 in 446.31 seconds; the final feature-focused MSVC AddressSanitizer workflow passes 6/6 stages in 42.269 seconds without a sanitizer finding. The broader ASan preset passes 59/60 because the already-open POC J Qt-painted-FPS gate fails repeatedly under instrumentation; both complete editor interaction aliases pass. The 189-record developer bundle hash-verifies and its packaged MonoGame self-test passes. Current Ubuntu/macOS POC K, explicit device-pixel/picking and collision-contact acceptance, multiple TileSets, non-orthogonal/rule/animated/terrain tiles, reimport, and complete accessibility evidence remain open, so this ADR remains `Proposed`.

DPE-ARCH-0015 implementation evidence (2026-07-28): draft PR #6 now covers deterministic v1-to-v2 migration without open-time rewrite, TilePalette v1, multiple TileSets/textures, all five layouts through one projection owner, typed Basic/Animated/Rule/Rule Override/Custom records, full cell/selection properties, built-in and worker-only Custom Extension brushes, stable-ID safe re-slicing, validated per-user shortcuts, expanded Tiled JSON conversion, and snapshot-v5 MonoGame/experimental-KNI rendering and picking. The neutral physics ABI adds size-tagged polygon colliders; Sprite Outlines are transformed and deterministically triangulated, while opt-in composite lowering merges only safe contiguous untransformed rectangular Grid cells. The 1,024-by-1,024 coordinate-span fixture round-trips 4,096 occupied cells across two layers and two TileSets. The complete local Windows matrices pass **63/63 in 570.78 seconds Release** and **63/63 in 895.30 seconds MSVC AddressSanitizer** without a finding; the 189-record production bundle hash-verifies and its packaged MonoGame smoke exits 0. Required hosted Windows/Ubuntu/macOS acceptance, complete real-device/contact POC K evidence, and KNI production conformance remain open, so this ADR remains `Proposed`.
