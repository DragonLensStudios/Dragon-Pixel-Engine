# Dragon Pixel Engine Tiled Tilemap Import Plan

> **Status:** Implementation complete; draft PR #5 ready for human review
> **Disposition:** Ready for review; not merged
> **Branch:** `feature/tiled-tilemap-import`
> **Target:** `develop`
> **Owner:** Codex implementation; human review and merge
> **Started:** 2026-07-27
> **Updated:** 2026-07-27
> **Governing architecture:** `DPE-ARCH-0014`

## Goal

Add the first importer-backed tile workflow: import a supported Tiled JSON map and its atlas TileSet into validated Dragon Pixel `dpe.tileset` v1 and `dpe.tilemap` v1 assets, then open the imported map in the existing Tile Palette for immediate painting, saving, rendering, picking, and collision authoring.

## User Value

Authors can start from a map made in Tiled instead of rebuilding it cell by cell. The workflow keeps the familiar Unity-style separation between reusable palettes/TileSets and scene-referenced Tilemaps, while remaining smaller and framework-neutral. The importer boundary also gives future tile formats a common isolated process/result seam without coupling Tiled concepts to runtime contracts.

## Background

Dragon Pixel already owns deterministic orthogonal TileSet/Tilemap documents, stable tile UUIDs, sparse 32-by-32 chunks, a Tile Palette, `Tilemap2D`, `TilemapCollider2D`, atomic tilemap saving, runtime snapshot lowering, MonoGame/KNI drawing, picking, and Box2D collision generation. The missing user-facing unit is conversion from an external tile editor into those established owners.

Official Tiled 1.12.2 documentation was checked on 2026-07-27. Tiled JSON maps store tile layers as global tile IDs, may use finite arrays or infinite-map chunks, may reference external JSON TileSets, and encode horizontal, vertical, diagonal, and reserved high-bit flags in each GID. Tiled supports more orientations, layer types, encodings, Tilesets, and image layouts than Dragon Pixel's current orthogonal v1 runtime can represent, so the importer must accept a precise subset and reject unsupported semantics without partial publication.

Primary references:

- [Tiled JSON Map Format](https://doc.mapeditor.org/en/stable/reference/json-map-format/)
- [Tiled Global Tile IDs](https://doc.mapeditor.org/en/stable/reference/global-tile-ids/)
- [Tiled TMX/TSX Format Reference](https://doc.mapeditor.org/en/latest/reference/tmx-map-format/)

## Scope

- Add a disposable `DragonPixelTiledImporterWorker` process with a versioned internal request/result envelope.
- Import Tiled JSON map files (`.tmj` and compatible `.json`) with orthogonal orientation.
- Support one atlas-based TileSet, either inline or referenced by one external JSON TileSet (`.tsj` or compatible `.json`).
- Require one PNG atlas with positive tile dimensions and valid margin, spacing, columns, and tile-count bounds.
- Support finite tile layers with native numeric GID arrays and infinite tile layers with native numeric chunk arrays.
- Preserve ordered layer names and visibility.
- Convert Tiled global IDs and orthogonal flip flags into stable Dragon Pixel tile UUIDs, flips, and quarter turns.
- Convert Tiled's top-down cell coordinates to Dragon Pixel's Y-up authoring grid deterministically.
- Produce one contained PNG texture, one `dpe.tileset` v1 document, one `dpe.tilemap` v1 document, and three `dpe.asset` v3 sidecars.
- Make the editor assign stable asset identities, validate every untrusted staged output, verify hashes and containment, and publish the complete import atomically through `AssetService`.
- Add **Assets > Import Tiled Tilemap...**, clear success/failure diagnostics, project-index refresh, and automatic Tile Palette opening.
- Define the worker launcher and result validation so another tile importer can use the same editor-owned publication seam later.

## Non-Goals

- TMX/XML or TSX/XML parsing in this feature.
- Isometric, staggered, hexagonal, or 3D tilemaps.
- More than one TileSet/atlas per imported map until runtime lowering resolves cells against multiple TileSets.
- Image-collection TileSets, embedded image bytes, non-PNG atlas images, tile animations, Wang/terrain/rule tiles, or per-tile custom-class conversion.
- Object, image, or group layer conversion; non-empty unsupported layer types fail before publication.
- Base64, CSV strings, gzip, zlib, or zstd tile-layer payloads; native JSON numeric arrays are required in this increment.
- Tiled object-layer collision-shape conversion. Existing Dragon Pixel rectangular collision authoring remains available in the imported TileSet workflow.
- In-place reimport, source watching, merge/conflict UI, or stable-ID rebasing against an already imported map.
- Automatic GameObject or scene mutation. The imported tilemap remains a reusable asset assigned through the existing `Tilemap2D` component path.
- Promotion of POC K, POC O, ADR-0016, ADR-0020, Slice 2, Slice 3, KNI support, or any platform support claim.

## Existing Architecture

- `dragonpixel::tiles` owns the `dpe.tileset` v1 and `dpe.tilemap` v1 value contracts and deterministic readers/writers.
- `TileDocumentService` and `TilePaletteWidget` own loaded tile authoring, preview/commit/cancel tools, Undo/Redo, dirty state, and atomic save.
- `AssetService` owns authoritative import publication, sidecars, source ownership, containment, operation staging, recovery, and immutable runtime bindings.
- `ProjectIndexService` owns post-publication discovery, stable IDs, dependencies, and diagnostics.
- `EditorWindow::runtime_snapshot_json` resolves TileSets/Tilemaps into snapshot v4; workers and adapters never parse Tiled files.
- `ADR-0016` limits the current runtime to static orthogonal TileSets/Tilemaps. `ADR-0020` requires importer process isolation, staged untrusted output, editor validation, and recoverable commit.

## Ownership and State Boundaries

- The Tiled worker owns parsing and conversion only. It has no authority to write into the project.
- The editor supplies the selected source path, an isolated staging directory, stable asset IDs, normalized import settings, and a bounded execution deadline.
- Worker output is untrusted until the editor verifies the result envelope, expected output inventory, file regularity, containment, hashes, assigned IDs, native tile-document parsing, PNG decoding, dependency graph, and output limits.
- `AssetService` is the only owner of final destination validation and atomic publication. No worker or panel writes project files.
- Tiled source data does not enter runtime snapshots. Only resolved Dragon Pixel documents and immutable contained PNG bytes cross into Preview/Play.
- Failed, timed-out, cancelled, crashed, invalid, or colliding imports leave the project index and existing files unchanged and remove only operation-owned staging.

## Contract, ABI, Protocol, Schema, Format, and Support Impact

- No change to `dpe.tileset` v1, `dpe.tilemap` v1, scene v3, runtime snapshot v4, C ABI, managed contracts, or framework adapter contracts.
- `dpe.asset` v3 is used as accepted, with importer identity `dragonpixel.tiled-json`, copied/generated ownership, deterministic hashes, dependency IDs, normalized settings, and ready recovery state.
- Add an internal importer request/result envelope version 1. It is an editor/worker implementation protocol, not a frozen public plug-in ABI.
- Accepted Tiled compatibility is explicitly limited to the scope above. Unsupported inputs produce stable structured diagnostics and no output.
- Future importers may reuse the isolated launcher/result/publication seam but must still receive their own format-specific validation and tests.

## Security and Data-Safety Considerations

- Normalize and resolve the selected map, referenced external TileSet, and PNG; reject missing, linked/reparse, escaping, duplicate, oversized, or non-regular dependencies.
- Bound source file sizes, tile counts, layer counts, chunk counts, cell counts, coordinates, names, and process duration before allocation/publication.
- Pass arguments as a vector; do not construct a shell command.
- Give the worker an operation-owned staging directory and never pass project-write authority.
- Treat result paths and hashes as hostile. Recompute hashes and reject absolute paths, traversal, undeclared outputs, symlinks/reparse points, and inventory mismatches.
- Refuse overwrite/case-folding collisions. A failed validation or commit leaves every pre-existing project byte intact.
- Record actionable diagnostics without embedding arbitrary source contents or secrets.

## Implementation Increments

### Increment 1: Tiled conversion worker

- Add versioned request/result DTO validation and a standalone worker executable.
- Parse the accepted Tiled JSON subset, resolve one inline/external TileSet and PNG, map GIDs/flags, generate deterministic stable tile/layer IDs, and write staged native documents.
- Add fixture-based worker tests for finite/infinite maps, external TileSets, negative chunks, flips/rotations, deterministic output, malformed input, path escape, unsupported format features, limits, and zero partial output.
- Run the focused Release build and worker tests before continuing.

### Increment 2: Editor-owned validation and publication

- Add a tile-import service that launches the worker without a shell, supports cancellation/timeout/crash diagnostics, and validates every staged output.
- Extend `AssetService` with a narrow importer-result publication path that builds v3 sidecars, rejects collisions/escapes, commits all generated files as one operation, and verifies the resulting project index.
- Add service tests for success, assigned identities, hashes/dependencies, worker failure, cancellation/timeout, tampered result/hash/path/ID, collision, and cleanup/recovery.
- Run focused Release and MSVC AddressSanitizer service tests.

### Increment 3: Simple authoring workflow

- Add **Assets > Import Tiled Tilemap...** with `.tmj`/`.json` selection, pixels-per-unit input, cancellation, and concise user documentation of the supported subset.
- Refresh Project Browser, log structured results, automatically load the imported map and TileSet in the Tile Palette, and preserve the existing scene/selection.
- Add Qt interaction coverage for the action, injected source selection/launcher seams, successful palette opening, cancellation, visible diagnostics, keyboard accessibility, and no scene mutation.
- Run the relevant Tile, AssetService, ProjectIndex, ProjectModel, and Qt interaction aliases in Release and AddressSanitizer configurations.

### Increment 4: Packaging, evidence, and review handoff

- Include the importer worker in the production-style developer bundle and validate its manifest path/hash.
- Run the accepted focused final matrix, documentation mirror validation, JSON fixture/schema checks, `git diff --check`, aggregate diff review, and branch-history review.
- Update this plan, the active master plan, ADR-0016, and ADR-0020 with exact evidence and honest remaining gates.
- Push the branch and open a draft PR into `develop`; do not merge.

## Test Strategy

- Native/worker: accepted finite and infinite JSON fixtures, inline/external TileSet, stable output, GID-to-local-ID resolution, all orthogonal flip combinations, negative coordinates, empty cells, and limit enforcement.
- Failure: malformed JSON, wrong orientation, multiple TileSets, unsupported layers/encodings/images, missing dependencies, traversal, linked inputs, invalid dimensions/counts/GIDs, process error, timeout/cancel, output tampering, and collision.
- Serialization: generated TileSet/Tilemap parse and deterministically round-trip through the authoritative native readers/writers.
- Asset/project: sidecar v3 fields, copied/generated ownership, dependency IDs, hashes/cache keys, project-index discovery, no partial commit, and cleanup.
- Qt: public menu action/state/shortcut plus an actual-worker end-to-end import that proves Project Browser refresh, automatic palette loading, and no scene mutation; cancellation/timeout/crash and visible diagnostic values are covered at the service boundary.
- Runtime regression: existing snapshot/adapters/picking/collision paths remain unchanged; run their focused aliases to prove imported native documents use the same path.
- Packaging: bundled importer presence and hash plus packaged editor self-test.

## Affected Tests

- New importer worker fixture tests and import-service tests.
- `s2.tile_documents` / `poc_k.tile_documents`.
- `s3.asset_service` / `poc_o.asset_import_cache_integrity`.
- Project-index and Project-model focused tests.
- `s2.editor_interactions` / `poc_h.qt_interactions` targeted import function and final focused aliases.
- Existing MonoGame/KNI tile snapshot/render tests where available, reported separately.
- Production-style bundle manifest/self-test.

## Platform Strategy

- Implement and run focused Windows Release and MSVC AddressSanitizer evidence locally.
- The standalone parser and fixtures remain QtCore/C++20 and path-portable so hosted Ubuntu/macOS CI can build and execute them without platform conditionals.
- Report hosted Windows, Ubuntu, and macOS results separately when the pushed draft PR runs. Existing unrelated platform/product failures remain visible and are not reclassified as feature success.
- This feature does not claim complete POC K/O or platform support without their full accepted matrices.

## Documentation Requirements

- Maintain this plan byte-identically in both documentation roots throughout the feature.
- Update the active Slices 1-4 plan with feature selection, evidence, limitations, and handoff.
- Update ADR-0016 with imported-authoring evidence and ADR-0020 with importer-isolation/publication evidence; retain `Proposed` status.
- Update user-facing TileSet/Tilemap documentation or README workflow only where the implemented action requires it.
- Record exact commands, pass/fail counts, durations, skips, platform separation, artifact identities, and known limitations before the final response.
- No Design/Prompt/Result revision is planned because this implements the already accepted DPE-ARCH-0014/ADR-0016/ADR-0020 boundaries without changing durable formats or topology.

## Risks and Mitigations

- **Silent semantic loss:** reject unsupported Tiled orientations, layer types, encodings, or atlas layouts before publication and list the precise limitation.
- **Wrong cell identity after edits:** derive tile UUIDs from the editor-assigned TileSet UUID plus Tiled local tile ID; derive layer UUIDs from the Tilemap UUID plus Tiled stable layer ID/order.
- **Coordinate/flip mismatch:** use explicit golden fixtures for top-down-to-Y-up coordinates and all eight orthogonal flag combinations.
- **Partial/corrupt import:** stage outside the project-authoritative paths, validate the complete inventory, commit once through AssetService, and test tampering/failure cleanup.
- **Large hostile maps:** enforce documented byte/count/coordinate limits in both worker and editor validation.
- **Editor coupling:** keep Tiled parsing out of `DragonPixelEditorLibrary`; only the isolated worker contains format-specific code.
- **Future importer over-generalization:** share only the launch/result/publication seam now; keep format behavior in its importer and require explicit support tests.

## Definition of Done

- [x] Mirrored prerequisites and governing architecture verified.
- [x] Current `develop` inspected at `afc9bd4b5643fc5ebe69eefaeda0884d3270b9bb`.
- [x] Focused branch created from current `develop`.
- [x] Smallest complete feature and affected tests/documentation identified.
- [x] Worker implementation and failure coverage complete.
- [x] Editor validation/publication and recovery coverage complete.
- [x] Public import workflow and automatic Tile Palette handoff complete.
- [x] Focused Release tests pass.
- [x] Required MSVC AddressSanitizer tests pass.
- [x] Required hosted platform evidence is recorded honestly as pending post-push CI; no platform claim is promoted.
- [x] Developer bundle contains and verifies the importer worker.
- [x] Documentation and mirrored plan are current and byte-identical.
- [x] Aggregate diff and commit sequence reviewed.
- [x] Branch pushed.
- [x] Draft PR opened into `develop`.
- [ ] Human review occurs before merge.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Prerequisites verified | PR #4 was confirmed merged; local `develop` was fast-forwarded to and matched `origin/develop` at `afc9bd4`. The worktree was clean. All four required documentation pairs existed and matched SHA-256; Design, Prompt/Result, AGENTS, the active master, the superseded tile plan, ADR-0016, ADR-0020, and development handbooks were read. Design and Prompt/Result both report `DPE-ARCH-0014`. |
| 2026-07-27 | Feature selected | Existing code already supplies the core TileSet/Tilemap/palette/runtime/collision engine. The smallest user-complete missing unit is isolated Tiled JSON conversion plus authoritative publication and public Tile Palette handoff. Broader TMX/multi-atlas/reimport work is intentionally left for later focused features. |
| 2026-07-27 | Compatibility research | Official Tiled 1.12.2 JSON, TMX/TSX, global-ID, and layer documentation was checked. The supported subset and rejection rules above are based on those primary specifications. |
| 2026-07-27 | Branch and plan started | Created `feature/tiled-tilemap-import` directly from current `develop` and created this mirrored plan before source changes. |
| 2026-07-27 | Increment 1 complete | Added the separate `DragonPixelTiledImporterWorker` and a QtCore/C++20 conversion library outside `DragonPixelEditorLibrary`. It validates the versioned request, contained inline/external JSON TileSet and PNG dependencies, orthogonal map subset, finite/infinite native arrays, limits, stable IDs, Y-axis conversion, and all Tiled orthogonal GID flip combinations before emitting staged native documents. The first run exposed an existing writer defect where a collision-free tile serialized as `[null]`; `json(nullptr)` plus a permanent native round-trip test repairs it. The focused Release build succeeded and `s2.tile_documents`, `poc_k.tile_documents`, `s2.tiled_tilemap_importer`, and `poc_k.tiled_tilemap_importer` passed 4/4 in 0.16 seconds. Commits: `81a6d17` and `a67d244`. |
| 2026-07-27 | Increment 2 complete | `TileImportService` assigns identities, writes a versioned request into an operation-owned temporary directory, launches the worker without a shell, contains cancellation/timeout/crash, validates the result inventory/paths/IDs/statistics and source stability, and passes immutable bytes to `AssetService`. `AssetService` re-parses native documents, verifies the one-Texture/TileSet/Tilemap contract and atlas bounds, creates the v3 dependency sidecars, refuses collisions, publishes six files through one operation, and removes only newly created outputs if project-index validation unexpectedly fails. Real-worker, tampered-path/identity, worker-lifecycle, invalid-PNG, collision, dependency, and no-partial-publication tests pass. Commits: `fc87ac1`, `d7864d0`, and `e0b70e3`. |
| 2026-07-27 | Increment 3 complete | Added **Assets > Import Tiled Tilemap...** with `Ctrl+Alt+T`, `.tmj`/`.json` selection, pixels-per-unit input, modal progress/cancellation, structured Console/UI diagnostics, Project Explorer refresh, and automatic Tile Palette opening. A public Qt test performs a real worker import into a temporary writable project, confirms the new texture/TileSet/tilemap is indexed and opened, and proves the scene entity set/path is unchanged. Commits: `74682bb` and `89e3514`. |
| 2026-07-27 | Focused Release verified | The final nine-alias Release matrix (`s2.tile_documents`, both POC K document/import aliases, ProjectIndex, both AssetService/POC O aliases, and both TileImportService/POC O aliases) passed **9/9 in 6.48 seconds**. The two affected existing Qt functions passed **4/4 assertions in 3.127 seconds** and the actual-worker UI import passed **3/3 assertions in 3.149 seconds**. An earlier attempt to run both complete interaction aliases in one 120-second orchestration call was killed at 124 seconds before completion and is inconclusive; it is not reported as passing evidence. |
| 2026-07-27 | Focused sanitizer verified | The corresponding MSVC AddressSanitizer build succeeded. The final nine-alias sanitizer matrix passed **9/9 in 21.05 seconds**. The two affected existing Qt functions passed **4/4 assertions in 4.685 seconds**, and the actual-worker UI import passed **3/3 assertions in 3.443 seconds**, with no sanitizer report. |
| 2026-07-27 | Developer bundle verified | The final `Build-Production-Editor.ps1 -Fast` run generated the production-style Windows bundle with the importer adjacent to the editor. The manifest contains **189** file records, zero missing/hash-mismatched/unlisted files. Importer SHA-256 is `D8AC528B99A4D2A4CEF331FDF63691A9B4F61D99B91F2DAF23B901F8575F3A66`; editor SHA-256 is `605AEC7FC8D261919B54B1432AA5BEA3EDB48A524749AECED19A29DA00D23688`; manifest SHA-256 is `49152DACA9AD810818457EBF477ABA9800D3854498F32BDA1241AABE1BD488EC`. An earlier composite follow-up check used a relative path after the build script entered a Visual Studio shell and therefore reported a false missing-manifest error; the final absolute-path build/inventory command completed green. Commit: `3fb419f`. |
| 2026-07-27 | Aggregate review complete | Reviewed the final 24-file, 2,711-insertion/5-deletion aggregate diff, including the focused implementation/test sequence from `54d9474` through `e0b70e3` and its evidence-only follow-ups. Changes remain limited to the Tiled importer, editor supervision/publication/UI, tile writer regression, focused tests, bundle deployment, README, mirrored plan/master tracker, and affected Proposed ADR evidence. `git diff --check develop` is clean; no generated build output, unrelated source, public format/ABI, support threshold, or platform claim changed. |
| 2026-07-27 | Draft PR handoff | Pushed `feature/tiled-tilemap-import` and opened draft PR [#5](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/5) into `develop`. The PR carries the full template summary, exact Windows Release/sanitizer evidence, bundle hashes, unsupported scope, and pending hosted-platform gates. It remains unmerged for human review. |

## Handoff Notes

The scoped Windows implementation and focused Release/AddressSanitizer verification are complete. The accepted Tiled subset works end to end through the public action and existing Tile Palette, while unsupported semantics fail before project publication. Draft PR [#5](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/5) targets `develop` and is intentionally unmerged. Ubuntu/macOS/hosted PR results remain pending; TMX/TSX XML, multiple TileSets, encoded/compressed data, non-orthogonal maps, object layers/collision conversion, animation/rules/terrain, and reimport remain explicit follow-up features. Human review, any requested corrections, approval, merge, and post-merge validation are the remaining lifecycle steps.
