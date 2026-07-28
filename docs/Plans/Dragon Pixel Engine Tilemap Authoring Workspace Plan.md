# Dragon Pixel Engine Tilemap Authoring Workspace Plan

> **Status:** Image-based TileSet intake implemented; aggregate verification active
> **Disposition:** Implementation correction in progress; not merged
> **Branch:** `feature/tilemap-authoring-workspace`
> **Target:** `develop`
> **Owner:** Codex implementation; human review and merge
> **Started:** 2026-07-27
> **Updated:** 2026-07-27
> **Governing architecture:** `DPE-ARCH-0014`
> **Base commit:** `8aac0e7fbc1affcbaadb26421dce6585df017a4d`

## Goal

Complete a practical orthogonal 2D tilemap-authoring workspace around Dragon Pixel's existing TileSet, Tilemap, palette, Project Explorer, Inspector, Scene View, runtime rendering, picking, collision, save, and recovery owners.

The workflow should feel familiar to users of component-based editors: create or import tile assets in Project Explorer, drag a Tilemap into a scene or compatible Inspector field, choose tiles and layers in a palette, paint in the 2D Scene View, inspect useful asset facts, and save or undo without editing JSON. Dragon Pixel retains its own labels, arrangement, visual treatment, contracts, and implementation.

## User Value

An author can turn an indexed TileSet or imported Tiled map into a working scene object and iterate on it entirely through the editor. The palette shows the real atlas art, layer and brush state are explicit, Scene View painting updates the actual preview path, and Project Explorer explains the selected TileSet or Tilemap instead of exposing only a filename.

## Background

The merged `feature/tiled-tilemap-import` work converts the accepted Tiled JSON subset into `dpe.tileset` v1 and `dpe.tilemap` v1, publishes their asset-v3 sidecars through `AssetService`, and opens the result in the existing Tile Palette. Earlier DPE-ARCH-0008 work already provides deterministic sparse chunks, paint/erase/rectangle/fill/eyedropper/selection tools, tilemap-local undo/redo, atomic scene-plus-tilemap save, `Tilemap2D` and `TilemapCollider2D`, snapshot-v4 lowering, MonoGame/KNI tile drawing and picking, and Box2D collision generation.

The remaining user-facing gaps are connected rather than format-level: the TileSet wizard does not create an empty Tilemap, no Tilemap preset exists, Scene/Hierarchy drops reject tilemaps, the Project details pane reports only kind/status/path, the palette uses generated color blocks rather than atlas pixels, layers cannot be managed publicly, brush transforms cannot be authored, and painting is confined to the palette canvas instead of the 2D Scene View.

## Scope

- Add an `AssetService` operation that creates one empty `dpe.tilemap` v1 and one `dpe.asset` v3 sidecar from one structurally valid indexed TileSet, with a stable generated asset ID, one initial layer, dependency revision binding, collision checks, atomic publication, and post-commit index validation.
- Add **Assets > Create Tilemap from Selected TileSet...** and a matching Project Explorer context action with a validated name prompt, refresh, automatic palette opening, and clear diagnostics.
- Add a Tilemap GameObject preset containing Transform and `Tilemap2D`, plus **Add > Tilemap 2D** and versioned Project-to-Scene/Hierarchy drag creation using the dragged Tilemap asset ID.
- Retain and test compatible Tilemap-to-Inspector assignment through the existing filtered asset-reference command path.
- Replace the minimal Project details label content for tile assets with parsed, read-only facts: stable ID, source, format, dependency/import/structural state, TileSet cell size/pixels-per-unit/tile count/texture dependency, and Tilemap layer/occupied-cell/dependency counts.
- Load the indexed atlas PNG with a TileSet/Tilemap and display cropped nearest-neighbor tile thumbnails and transformed cell art in the Tile Palette. Missing or invalid atlas data remains diagnosable and falls back visibly without discarding tile records.
- Add command-like TileDocumentService operations for add, rename, show/hide, reorder, and remove layer; preserve at least one layer; retain stable layer IDs; and make every successful operation one tile-local Undo item.
- Add brush flip-X, flip-Y, and quarter-turn rotation controls. Painting records those existing `tile_cell` fields; eyedropper restores both tile identity and transform.
- Add a focused tile-edit mode to the 2D Scene View. Pointer strokes map through the selected Tilemap GameObject transform and TileSet cell dimensions, reuse the palette's active layer/tool/brush, support commit/cancel, display a cell overlay, and never mutate a scene or tilemap during Play or 3D view.
- Refresh the existing immutable snapshot/worker preview after tile-document changes through a bounded coalesced path. Use the already flattened `pixelsPerUnit` value so tile visual size agrees with the authoring grid and existing unit-based collision model.
- Preserve the existing atomic scene-plus-dirty-tilemap save and unsaved-document prompts.

## Non-Goals

- Copying Unity branding, artwork, proprietary behavior, exact layouts, menu text, or serialized formats.
- Isometric, hexagonal, staggered, 3D, rule, terrain, animated, or procedural tiles.
- Multiple TileSets per Tilemap in the runtime or editor.
- Tile animation, arbitrary collision shapes, object/image/group layers, sorting groups, or custom grid swizzles.
- TMX/TSX XML, additional Tiled encodings, in-place reimport, merge/conflict handling, source watching, or new third-party importers.
- A standalone Tile asset durable format, tile inheritance, or project-authored editor widgets.
- Promotion of POC K, POC O, ADR-0016, ADR-0020, Slice 2, Slice 3, KNI production support, or any platform support claim.
- Completing the full three-platform designer/accessibility matrix in this Windows implementation feature.

## Existing Architecture

- `dragonpixel::tiles` owns `dpe.tileset` v1 and `dpe.tilemap` v1 parsing, deterministic writing, stable IDs, layers, sparse 32-by-32 chunks, and cell transforms.
- `TileDocumentService` owns the currently loaded Tilemap/TileSet, stroke previews, tile-local Undo/Redo, dirty state, and atomic preparation/save.
- `TilePaletteWidget` owns public palette selection and tools but currently draws generated colors and has no layer mutations or Scene View tool state.
- `AssetService` owns final authoritative asset publication, containment, collision checks, operation identity, recovery, and project-index validation.
- `ProjectIndexService` and `ProjectModel` own discovered paths, stable IDs, dependencies, status, previews, filtering, and versioned drag payloads.
- `SelectionService`, Scene commands, and `EditorWindow::apply_authoring_transaction` own scene selection and all GameObject/component mutations.
- `AuthoringViewport` owns editor-camera pointer mapping and overlays; it does not own saved tile or scene data.
- `EditorWindow::runtime_snapshot_json` resolves the current in-memory tile document into immutable snapshot-v4 TileSet/Tilemap data; adapters never parse authoring files.

## Ownership and State Boundaries

- `AssetService` is the only final writer for the new empty-Tilemap asset operation. Menus and context actions only collect intent and display results.
- `TileDocumentService` remains the only mutable owner of the loaded Tilemap document. Palette and Scene View route all cell/layer edits through it.
- Scene creation and asset assignment remain validated scene command transactions. A drag payload never writes component properties directly.
- Project Explorer and Project details are read-only projections of the detached project index plus bounded parsing of structurally valid tile source documents.
- AuthoringViewport emits pointer intent and displays overlays. It receives no filesystem authority and never retains authoritative tile data.
- Preview/Play workers continue to consume immutable snapshots only. Stop, crash, reload, and Play cannot write runtime state into the Tilemap or scene.
- Unsupported or unresolved tile, texture, dependency, layer, or transform data remains visible/diagnosable and round-trips through existing native documents.

## Contract, ABI, Protocol, Schema, Format, Platform, and Support Impact

- No revision to `dpe.tileset` v1, `dpe.tilemap` v1, `dpe.asset` v3, scene v3, runtime snapshot v4, C ABI, managed public contracts, worker JSON-RPC, or drag format v1 is planned.
- The new Tilemap preset is an in-process scene command option and serializes to the same existing Transform and `Tilemap2D` component records.
- Layer operations and brush transforms populate fields already present in `dpe.tilemap` v1.
- Snapshot v4 already carries TileSet `pixelsPerUnit`; the managed runtime begins consuming that existing value for cell world sizing. This is an internal conformance repair, not a format addition.
- Windows Release and MSVC AddressSanitizer are the local implementation matrix. Ubuntu/macOS and complete MonoGame/KNI POC K results remain separately required.

## Security and Data-Safety Considerations

- Resolve the selected TileSet only from the current candidate index, require a structurally valid contained regular source, reparse it with the authoritative native reader, and reject stale/cross-project/ambiguous selection before mutation.
- Validate portable names, normalized contained destinations, canonical generated IDs, dependency identity/revision, case-folded collisions, size bounds, and the complete candidate index before declaring creation successful.
- Publish the Tilemap and sidecar together through one operation; interruption or validation failure must leave no partial authoritative asset.
- Refuse removal of the final layer and bound layer count, names, coordinates, rectangle/fill work, and preview refresh frequency.
- Cancel an active Scene View stroke on Escape, mode change, document replacement, project/scene close, Play, or unavailable selection.
- Never follow linked/reparse atlas paths for editing, never write the atlas during palette display, and never include arbitrary project bytes in diagnostics.

## Implementation Increments

### Increment 1: Authoritative tilemap creation and scene attachment

- Add the narrow AssetService create operation and success/failure/recovery tests.
- Add the native Tilemap preset with scene command/serialization/Undo coverage.
- Add Assets/Project Explorer actions, Scene/Hierarchy drag creation, and compatible Inspector assignment coverage.
- Run focused strict Release tests and commit the coherent increment.

### Increment 2: Tile document and palette editing surface

- Add layer mutation and transformed-brush APIs with deterministic Undo/Redo tests.
- Resolve/load the atlas, draw actual cropped art in the tile list and map canvas, and retain a visible missing-atlas fallback.
- Add layer and brush controls, shortcuts, accessible names, enabled states, and public Qt interaction coverage.
- Run focused strict Release and MSVC AddressSanitizer tests and commit the coherent increment.

### Increment 3: 2D Scene View painting and preview correlation

- Add invertible 2D pointer-to-world mapping, tile-edit pointer signals, cell overlay, commit/cancel behavior, and mode guards to AuthoringViewport.
- Route Scene View strokes through TileDocumentService using the selected Tilemap GameObject transform and active palette state.
- Coalesce real preview reloads and consume existing pixels-per-unit during adapter tile drawing so the displayed grid and runtime tiles agree.
- Test paint/erase/rectangle/fill/eyedropper behavior where applicable, transformed cells, Escape cancellation, 3D/Play rejection, save/reopen, preview refresh, real adapter pixels, and picking regressions.
- Run focused Release and sanitizer verification and commit the coherent increment.

### Increment 4: Evidence, packaging, and review handoff

- Run the accepted focused final Windows Release and MSVC AddressSanitizer matrix, relevant managed adapter tests, Project/Asset/Tile/Scene/Qt aliases, and production-style bundle verification.
- Update README, this plan, the active master tracker, ADR-0016, ADR-0020, and the prior Tiled plan's merge disposition with exact evidence and remaining limitations.
- Verify every changed mirrored Markdown pair, UTF-8/LF/no-BOM, links, JSON fixtures, `git diff --check`, aggregate diff, and focused commit sequence.
- Push the branch and open a draft PR into `develop`; do not merge.

### Increment 5: PNG TileSet intake correction

- Reproduce the reported Create TileSet failure with a valid extensionless PNG that the preview can decode.
- Replace filename-suffix acceptance with authoritative PNG content validation shared by preview and creation, while retaining a `.png` destination inside the project.
- Reject missing, unreadable, mislabeled, and non-PNG sources before any project output is written, and keep full-path feedback visible when the field is clipped.
- Run the focused Release and MSVC AddressSanitizer interaction coverage, the accepted aggregate regression matrix, bundle verification, and update draft PR #6 without merging it.

### Increment 6: Image-based TileSet intake and naming

- Rename the public Assets action, wizard title/form/accessibility text, internal request field, and editor handler from PNG-specific naming to **Create TileSet from Image...**.
- Accept content-validated PNG, JPEG, BMP, and GIF sprite sheets, preserving valid PNG bytes and transcoding other accepted raster formats to an actual contained PNG texture.
- Keep dimensions, slicing, collision, non-overwrite, no-partial-output, and extensionless-PNG behavior unchanged; reject unsupported or malformed image content before project mutation.
- Add public menu/wizard naming and JPEG-to-PNG normalization coverage, rerun focused Release/sanitizer and full Release verification, rebuild/verify the bundle, and update draft PR #6 without merging it.

## Affected Tests and Verification Strategy

- Native scene: Tilemap preset components/defaults, primary asset assignment, transaction Undo/Redo, scene serialization/reopen, and existing preset regressions.
- TileDocumentService: layer add/rename/visibility/reorder/remove, final-layer refusal, stable IDs, cell transform paint/eyedropper, chunk boundaries, dirty state, Undo/Redo, deterministic save/reopen, and cancellation.
- AssetService/ProjectIndex/ProjectModel: empty Tilemap publication, dependency revision, collision/invalid selection/invalid document/no-partial-output failure, discovery, tile filtering/details, and drag payload identity.
- Qt interactions: create action and context action, automatic palette opening, real atlas previews, layer/brush controls, keyboard/focus/accessibility, Tilemap Scene/Hierarchy drop, Inspector assignment, 2D Scene View stroke/overlay/cancel, and no Play/3D mutation.
- PNG TileSet intake: valid `.png` and extensionless PNG content, mislabeled non-PNG rejection, missing-source rejection, normalized contained `.png` output, and zero partial files after rejected input.
- General image TileSet intake: public menu/dialog/accessibility naming, JPEG/BMP/GIF content acceptance, PNG output format/signature, dimensions/tile count, and unsupported/malformed input rejection.
- Managed/runtime: existing snapshot-v4 parsing and both adapter tile-drawing tests, with pixels-per-unit sizing and real tile pixels/picking reported separately.
- Serialization/recovery: scene-plus-tilemap transaction tests, invalid/missing dependency preservation, atomic publication, and no write on rejected operations.
- Packaging: developer bundle manifest/hash verification and packaged MonoGame self-test; no new runtime file is expected.

## Platform Strategy

- Implement and record focused Windows strict Release and MSVC AddressSanitizer results locally.
- Keep C++20/Qt and managed changes platform-neutral and avoid platform-specific pointer, path, or rendering assumptions.
- Report MonoGame and KNI results separately. KNI remains experimental.
- Treat Ubuntu/macOS and hosted results as pending unless they actually run on the pushed branch. Existing open macOS throughput and broader platform gates remain visible.

## Documentation Requirements

- Maintain this plan byte-identically under repository `docs/Plans` and external `Plans` throughout the work.
- Update the active Slices 1-4 tracker with selection, implementation evidence, limitations, and handoff.
- Update ADR-0016 with focused authoring/runtime evidence and ADR-0020 with empty-Tilemap AssetService publication evidence; retain `Proposed` status.
- Correct the merged status of the prior Tiled import plan without rewriting its historical evidence.
- Update README with the implemented create/drag/palette/Scene workflow and exact static-orthogonal limitations.
- No Design or Prompt/Result revision is planned because the feature implements the accepted DPE-ARCH-0014/ADR-0016/ADR-0020 boundary without changing durable formats or process topology.

## Risks and Mitigations

- **Scope inflation from “Unity equivalent”:** deliver the complete accepted orthogonal/static workflow above and state deferred tile classes and formats explicitly; do not imitate proprietary presentation.
- **Second mutation path:** keep cell/layer edits in TileDocumentService and scene edits in validated Scene commands; View widgets emit intent only.
- **Visual/grid mismatch:** use the existing TileSet cell size and pixels-per-unit consistently in palette, Scene coordinate mapping, snapshot parsing, and adapter drawing; add non-default scale tests.
- **High-frequency worker reload:** debounce document-driven preview refresh and keep tile-local preview changes cancelable before commit.
- **Loss during layer removal/reorder:** make each operation one full before-image Undo item, refuse final-layer removal, and test occupied-layer restoration.
- **Atlas path confusion:** resolve the texture through stable indexed dependencies, load read-only, validate image/source rectangles, and fall back diagnostically.
- **Preview/create disagreement:** use the same content-based PNG decoder at both boundaries; do not accept by suffix alone or reject valid PNG bytes because a temporary/downloaded file has no extension.
- **Misleading image support:** publish the exact accepted raster formats in the picker and README, validate by decoded content rather than suffix, and transcode every non-PNG source so a `.png` destination never contains another codec.
- **Stale Project drag/selection:** preserve drag format revision/project identity validation and re-resolve every asset ID in the current candidate index.
- **Cross-platform input differences:** keep pointer mapping in Qt logical coordinates and validate guarded behavior without OS-specific events.

## Definition of Done

- [x] Previous Tiled import PR merge verified and current `develop` inspected at the recorded base.
- [x] Four required mirrors and `DPE-ARCH-0014` revision verified.
- [x] Governing Design/Prompt context, active master, tile plan, affected ADRs, and verification policy reviewed.
- [x] Smallest complete feature, authoritative owners, affected tests, and documentation identified.
- [x] Focused branch created from current `develop`.
- [x] Mirrored feature plan created before source modification.
- [x] Empty Tilemap creation is atomic, indexed, dependency-bound, and failure-covered.
- [x] Tilemap preset and Project-to-Scene/Hierarchy drag creation are command-backed and undoable.
- [x] Compatible Project-to-Inspector assignment remains validated and covered.
- [x] Project details expose useful read-only TileSet/Tilemap facts.
- [x] Palette displays actual atlas art with visible missing-data fallback.
- [x] Layer management and transformed brushes are complete and undoable.
- [x] 2D Scene View painting, overlay, cancellation, and preview refresh are complete.
- [x] Existing pixels-per-unit is consumed consistently by the adapter path.
- [x] Focused Windows strict Release verification passes.
- [x] Focused Windows MSVC AddressSanitizer verification passes.
- [x] Relevant MonoGame and KNI results are reported separately.
- [x] Production-style developer bundle verification passes.
- [x] Documentation/evidence is current and mirrored byte-identically.
- [x] Aggregate diff and commit sequence reviewed.
- [x] Branch pushed.
- [x] Draft PR opened into `develop` and left unmerged for human review.
- [x] Reported extensionless-PNG creation failure is regression-covered and repaired without weakening non-PNG rejection.
- [x] Correction verification, evidence, branch push, and draft PR #6 update are complete.
- [x] Public workflow consistently says **Create TileSet from Image...** and accepts the documented raster formats.
- [x] Non-PNG inputs are normalized to real PNG bytes with focused verification and no regression to extensionless PNG handling.
- [ ] Image-intake evidence, branch push, and draft PR #6 update are complete.
- [ ] Human review and merge occur after this implementation handoff.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Prerequisites verified | PR #5 is merged at `8aac0e7`; local `develop` was fast-forwarded to and matches `origin/develop`. The worktree was clean. All four required mirror pairs exist and have identical SHA-256 hashes. Design and Prompt/Result report `DPE-ARCH-0014`. The active tracker, accepted tile architecture, prior Tile/Tiled plans, ADR-0016, ADR-0020, feature template, and verification checklist were reviewed. |
| 2026-07-27 | Existing owners inspected | Confirmed that TileDocumentService owns loaded mutation/Undo/save; AssetService owns publication; ProjectModel owns versioned drag identity; Scene commands own GameObject/component changes; AuthoringViewport owns editor-camera pointer mapping; and runtime snapshot v4 already carries TileSet pixels-per-unit. Existing palette tools and Inspector assignment are reusable, while empty-map creation, Tilemap preset/drop, atlas previews, public layer/transform controls, rich tile details, and Scene View painting are missing. |
| 2026-07-27 | Scope accepted and branch started | Selected the bounded orthogonal Tilemap authoring workspace above and created `feature/tilemap-authoring-workspace` directly from current `develop`. No durable format, ABI, public managed contract, or process-topology revision is required. |
| 2026-07-27 | Increment 1 complete | Added AssetService empty-Tilemap publication from one indexed TileSet, including generated stable map/layer IDs, one initial layer, asset-v3 dependency revision, two-file staged publication, collision/type/name/native-document checks, and post-publication index validation/removal. Added the Transform + Tilemap2D preset, Assets and Project context creation actions, toolbar/Hierarchy add entries, Project-to-Scene and Project-to-Hierarchy drops, and retained filtered Inspector assignment. Focused Release `s1.native_core`, `s3.asset_service`, and `poc_o.asset_import_cache_integrity` passed 3/3 in 7.51 seconds. The affected public Qt workflow passed 3/3 assertions groups in 13.029 seconds, including two map creations, viewport/Hierarchy attachment, and Inspector reassignment. The first direct build attempt lacked the configured MSVC standard-library environment and failed before compilation; rerunning in the Visual Studio 2022 developer shell succeeded. A 124-second full interaction-alias attempt timed out and is inconclusive, not passing evidence. |
| 2026-07-27 | Increment 2 complete | Added stable-ID layer add/rename/visibility/reorder/remove operations with full before-image Undo, final-layer and count/name guards, transformed brush paint/fill/rectangle/eyedropper state, atlas-cropped nearest-neighbor list/canvas rendering with a visible fallback, tile-focused Project filters/details, and accessible layer/brush controls. The three affected Qt workflows passed 5/5 test stages in 21.911 seconds under strict Windows Release and 5/5 in 35.569 seconds under MSVC AddressSanitizer (`detect_leaks=0`, fail-fast enabled). Both configurations built with `/W4 /WX`; the ASan build completed without sanitizer findings. |
| 2026-07-27 | Increment 3 complete | Added an explicit 2D tile-edit pointer mode to AuthoringViewport with world mapping, transformed cell overlay, left-button stroke signals, camera gesture priority, and Escape/mode/Play cancellation. EditorWindow now resolves the one selected Tilemap2D through its complete parent Transform chain, maps cell dimensions through the TileSet's existing pixels-per-unit, captures brush/layer/tool state for one tile-local transaction, and coalesces immutable preview refreshes at 75 ms. The new Scene View workflow covers transformed paint/rectangle/eyedropper state, rollback, 3D/Play rejection, transformed coordinate inversion, and refreshed snapshot contents. The combined four-workflow Qt set passed 6/6 stages in 28.906 seconds under strict Release and 6/6 in 45.150 seconds under MSVC AddressSanitizer. Existing snapshot-v4 parsing already divides cell pixel dimensions by `pixelsPerUnit`; native tile documents passed 2/2, MonoGame scene/graphics paths passed 2/2, and experimental KNI scene/graphics paths passed 2/2 in a combined 22.74 seconds. These are focused Windows results and do not promote POC K or KNI support. |
| 2026-07-27 | Final Windows verification recorded | The complete strict Release preset passed **60/60 in 446.31 seconds**. After the final gap-free stroke interpolation and active-stroke save guard, the feature-focused MSVC AddressSanitizer Qt set passed **6/6 stages in 42.269 seconds** with no sanitizer finding. The complete ASan preset passed **59/60 in 646.49 seconds**: both complete interaction aliases passed, but existing `poc_j.qt_input_latency` failed the unchanged 30 Qt-painted-FPS gate (MonoGame 28.337 FPS; KNI 21.535 FPS). An immediate unchanged rerun failed the same gate (MonoGame 25.306 FPS; KNI 20.862 FPS) despite 21/21 exact correlated tuples and p50 latency below 100 ms for both adapters. This is recorded as an open POC J aggregate blocker, not hidden or reclassified as feature success. |
| 2026-07-27 | Production bundle verified | `Build-Production-Editor.ps1 -Fast` generated the production-style Windows bundle. All **189** manifest records exist and SHA-256-verify, and the packaged MonoGame self-test exits successfully with development-path overrides removed. Final hashes: editor `EBBDD9273DEF8A32B75309E9A04131366DBCA281ECA2A8E395C5342BF2A6D85F`, Tiled importer worker `4B83ADCA6BD9E8CD81D7E2C7FC66A3455C2759D075CE2A91A42ADDD1489C3B5F`, manifest `BF510DB25EF185250F8DA41387BD3605A1E253AC972936B35C3B9CB01902445E`. This remains a developer bundle, not a signed POC R release package. |
| 2026-07-27 | Aggregate review complete | Reviewed the focused four-commit sequence and the complete branch diff from `develop`. The work remains within the accepted static orthogonal, one-TileSet authoring workflow; no durable tile/asset/scene/snapshot format, C ABI, managed public contract, worker protocol, support claim, threshold, or unrelated file changed. `git diff --check develop...HEAD` is clean before the final evidence commit. |
| 2026-07-27 | Draft PR handoff | Pushed `feature/tilemap-authoring-workspace` and opened draft PR [#6](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/6) into `develop`. The PR includes the exact complete Release result, focused and aggregate ASan results including the repeated POC J failure, separate MonoGame/KNI evidence, bundle hashes, unsupported scope, recovery behavior, and remaining platform gates. It is intentionally unmerged for human review. |
| 2026-07-27 | PNG intake correction started | The supplied extensionless file `Tileset_Ground_JMguEL` exists and begins with the standard PNG signature. The preview succeeds because `QImage` detects content, while `TileSetCreationService::create` rejects the same file solely because `QFileInfo::suffix()` is not `png`. The smallest complete correction is content-based PNG acceptance shared by preview/create, explicit rejection of mislabeled non-PNG data, and focused creation/regression evidence on this same active feature branch and draft PR. Required document mirrors and `DPE-ARCH-0014` were reverified before source changes. |
| 2026-07-27 | PNG intake correction implemented and focused verification passed | Added a regression that first failed 2 passed / 1 failed in 9 ms at the exact suffix-only rejection. Preview and Create now share one forced-PNG content decoder over one captured byte sequence, accept a valid extensionless PNG, retain the exact validated bytes in a normalized contained `.png`, reject missing and mislabeled non-PNG input before output, expose the full selected path as a tooltip, and offer an all-files picker fallback. The existing plus new direct/wizard intake workflows pass **4/4 in 36 ms** under Release and **4/4 in 66 ms** under MSVC AddressSanitizer. The expanded tilemap workflow set, including Scene View editing, Project/Hierarchy/Inspector attachment, layer/brush transactions, both PNG intake cases, and recoverable scene-plus-tile save, passes **8/8 in 37.941 seconds** under Release and **8/8 in 63.733 seconds** under AddressSanitizer with no sanitizer finding. Commit: `03267d8`. |
| 2026-07-27 | PNG correction aggregate verification passed | A complete strict Windows Release rebuild succeeded with zero warnings/errors and the unchanged preset passed **60/60 in 482.94 seconds**. The production-style bundle contains **189** manifest records with zero missing, size-mismatched, hash-mismatched, unlisted, or manifest-only files; the packaged MonoGame self-test passes with development-path overrides removed. Final hashes: editor `E1D6BFA7F77705BC4D9C2C5D1210A0A334488362BC7C67ECCF7BB5A698E29E34`, Tiled importer `4B83ADCA6BD9E8CD81D7E2C7FC66A3455C2759D075CE2A91A42ADDD1489C3B5F`, manifest `90FC556D52BC4B40C7B8D3F9AB0200DFF8ED8FE37F8EE922CDE2EA80A5361ABD`. The broader ASan preset was not repeated for this decoder-local correction; its prior 59/60 result and unchanged POC J performance blocker remain recorded, while the complete affected tile workflow passes under sanitizer as reported above. |
| 2026-07-27 | PNG correction review handoff complete | Reviewed the four focused correction commits and five-file correction diff, then re-reviewed the aggregate branch stat and confirmed `git diff --check develop...HEAD` is clean with no unrelated source, format, contract, threshold, or support change. Pushed the correction to `feature/tilemap-authoring-workspace` and updated existing draft PR [#6](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/6) with the root cause, behavior, focused/full verification, recovery coverage, final hashes, unchanged limitations, and prior POC J sanitizer blocker. GitHub reports the PR open, draft, targeting `develop`, and unmerged. |
| 2026-07-27 | Image-based TileSet intake correction started | The user requested that **Create TileSet from PNG...** become **Create TileSet from Image...** and that TileSet creation be corrected consistently. Inspection found PNG-specific public text and internal names across EditorWindow, TileSetWizard, its request DTO, README, and tests. The smallest complete behavior matching the new name is content-validated PNG/JPEG/BMP/GIF input with a guaranteed contained PNG output, consistent preview/create decoding, unchanged slicing and recovery boundaries, and public naming coverage on the same active branch and draft PR. Required mirrors remain synchronized at `DPE-ARCH-0014`; no durable format, ABI, protocol, topology, or support revision is required. |
| 2026-07-27 | Image-based TileSet intake implemented and focused verification passed | The public Assets action, handler, wizard title/form/accessibility text, source object ID, and request field now use image-based naming. Preview/Create share content detection limited to PNG/JPEG/BMP/GIF; PNG bytes are preserved, while JPEG/BMP/GIF decode and transcode into a real PNG before the existing contained four-file publication. A new JPEG regression first failed 2 passed / 1 failed in 11 ms at the prior forced-PNG decoder. The final focused set validates the public action, wizard text, standard and extensionless PNG, JPEG/BMP/GIF PNG signatures/dimensions/tile counts, malformed/missing/SVG rejection, non-overwrite, exact PNG retention, and zero targeted partial outputs: **6/6 in 0.937 seconds** under strict Release and **6/6 in 3.212 seconds** under MSVC AddressSanitizer with no sanitizer finding. Commit: `e3b7104`. |

## Handoff Notes

Draft PR [#6](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/6) remains open and intentionally unmerged while the public TileSet action and intake path are generalized from PNG-specific naming to the documented raster-image workflow. The completed extensionless-PNG correction and prior evidence remain historical baselines for this active correction. The repeated aggregate POC J sanitizer performance failure, current Ubuntu/macOS evidence, broader tile types/importers, complete POC K/O/J acceptance, and KNI production support remain open and unchanged.
