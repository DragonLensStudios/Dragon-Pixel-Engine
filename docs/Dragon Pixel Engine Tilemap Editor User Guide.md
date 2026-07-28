# Dragon Pixel Engine Tilemap Editor User Guide

> **Architecture revision:** DPE-ARCH-0015
> **Feature branch:** `feature/tilemap-authoring-workspace`
> **Status:** Draft PR #6 implementation guide; behavior remains subject to review

## Overview

Dragon Pixel Engine provides one Tilemap workspace for creating, importing, organizing, painting, selecting, saving, previewing, and running 2D tile content. Its workflow is intentionally familiar to users of component-based 2D editors, while its presentation, durable formats, ownership rules, and runtime architecture are Dragon Pixel-specific.

The durable assets are:

- **TileSet (`dpe.tileset` v2):** owns typed tile definitions and references one or more textures.
- **Tile Palette (`dpe.tilepalette` v1):** a project asset that stores sparse logical palette cells and may reference multiple TileSets.
- **Tilemap (`dpe.tilemap` v2):** owns grid settings, internal layers, multiple TileSet dependencies, and sparse authored cells.
- **Tilemap2D GameObject:** references a Tilemap asset in a scene and is the active Scene View paint target.
- **TilemapCollider2D:** requests neutral runtime collision lowering for collidable tiles.

Opening legacy TileSet v1 or Tilemap v1 content does not rewrite it. A successful explicit edit and save upgrades the document atomically.

Behavioral research was rechecked on 2026-07-28 against Unity's official documentation for [creating Tilemaps](https://docs.unity3d.com/kr/current/Manual/tilemaps/work-with-tilemaps/create-tilemap.html), [creating Tile Palettes](https://docs.unity3d.com/kr/current/Manual/tilemaps/tile-palettes/create-tile-palette.html), [palette tools and Active Targets](https://docs.unity3d.com/cn/2023.2/Manual/tile-palette-ui-ref.html), [Grid Selection](https://docs.unity3d.com/cn/2023.2/Manual/tile-palette-grid-selection.html), [automatic sprite slicing](https://docs.unity3d.com/kr/6000.0/Manual/sprite/sprite-editor/automatic-slicing.html), [Tilemap Extras](https://docs.unity3d.com/cn/6000.0/Manual/com.unity.2d.tilemap.extras.html), and [Tilemap collision](https://docs.unity3d.com/6000.0/Documentation/Manual/tilemaps/work-with-tilemaps/tilemap-collider-2d-reference.html). Tiled compatibility follows the official [JSON map format](https://doc.mapeditor.org/en/latest/reference/json-map-format/) and [global tile ID](https://doc.mapeditor.org/en/stable/reference/global-tile-ids/) references. These sources guide behavior only; Dragon Pixel uses original presentation, contracts, and implementation.

## Create a paint-ready Tilemap from an image

1. Open or create a Dragon Pixel project.
2. Choose **Assets > Create TileSet from Image...**.
3. Browse to a PNG, JPEG, BMP, or GIF image. The editor validates image content rather than trusting only the filename extension; stored texture output is PNG.
4. Choose a slicing mode:
   - **Automatic** finds non-transparent sprite regions.
   - **Cell Size** uses explicit cell width and height.
   - **Cell Count** divides the usable image area into an explicit number of columns and rows.
5. Set offset, padding, empty-cell handling, pivot, pixels per unit, grid layout, and collision generation.
6. Leave **Create a Tilemap and GameObject ready for painting** enabled for the guided workflow.
7. Review the preview and choose **Create**.

Successful guided creation publishes and opens the contained texture, TileSet, palette, Tilemap, and assigned Tilemap2D GameObject. Collision-enabled creation also attaches TilemapCollider2D. Pre-publication failure leaves no partial output. If scene attachment alone fails, the valid assets remain available and the editor reports the recovery action.

## Import a Tiled JSON map

Choose the Tiled JSON import action and select a `.tmj` or JSON map. The isolated importer accepts:

- multiple inline or external atlas TileSets;
- orthogonal, isometric, staggered, and hexagonal maps;
- unencoded integer tile layers;
- tile animation;
- representable terrain or Wang information converted conservatively to Rule Tiles;
- optional isometric-to-Isometric-Z-as-Y interpretation; and
- automatic TileSet, palette, Tilemap, and editor handoff.

The importer intentionally rejects XML TMX/TSX, encoded or compressed layers, object layers, image-collection TileSets, unsupported Wang semantics, and in-place reimport. Rejection occurs before final publication, so unsupported input cannot leave a partial authored map.

## Active Palette, Active Target, and layers

- **Active Palette** identifies the durable logical clipboard used for tile selection.
- **Active Target** identifies the Tilemap2D GameObject receiving Scene View work.
- By default, target selection follows the selected compatible GameObject.
- **Pin** keeps the chosen target while selecting source objects or prefabs for a GameObject Brush. The editor restores the authoring target after Play.
- **Layer** selects one internal Tilemap layer. Layers are the authoritative active targets; they can be added, renamed, shown or hidden, reordered, and removed while preserving at least one layer.

Project Explorer drops accept supported images, TileSets, palettes, Tilemaps, and prefabs. A compatible selected scene-object group can arm the GameObject Brush.

## Paint tools and shortcuts

The Tile Palette toolbar provides:

| Tool | Familiar letters | Legacy numbers | Behavior |
| --- | --- | --- | --- |
| Paint | P | 1 | Paint the active brush. |
| Erase | E | 2 | Remove authored tiles or matching owned GameObject Brush placements. |
| Box | B | 3 | Paint or erase a rectangular region. |
| Line | L | 4 | Follow the active grid topology between endpoints. |
| Flood | F | 5 | Replace a bounded connected region. |
| Pick | I | 6 | Restore tile identity and authored cell properties from a cell. |
| Select | S | 7 | Select a grid rectangle for property or structural editing. |
| Move | M | 8 | Move the selected rectangle without losing cell properties. |

Choose the shortcut profile in the toolbar. The selection is per-user, non-authoritative editor state. **Reset Keys** returns to the familiar-letter profile. Escape cancels an active stroke, Delete removes the current grid selection, and arrow keys move it one cell.

Scene View painting is enabled only in 2D authoring mode with a valid target, layer, palette brush, and stopped runtime. Play and 3D modes cannot mutate authored Tilemap data.

## Brush behavior

- **Basic:** repeats the active tile or multi-cell palette pattern.
- **Random Selection:** chooses among selected tiles from stable map/layer/cell/type seeds. Repainting the same inputs produces the same output.
- **Group Stamp:** preserves selected palette offsets with a configurable gap and cell limit.
- **GameObject:** places linked prefab instances or deep duplicates of selected scene roots.

GameObject Brush placements become children of the active Tilemap2D target and receive stable map, layer, cell, and source metadata. Erase acts only on matching placement metadata; unrelated scene objects are never deleted by proximity or name.

The Brush Inspector edits tint, local offset, arbitrary rotation, scale, elevation, color lock, and transform lock. Flip X, Flip Y, and quarter-turn rotation remain available in the toolbar.

## Grid Selection editing

Use **Select** to define a rectangular region. The selected cells can be:

- moved with the Move tool or arrow keys;
- deleted;
- replaced with the active tile;
- assigned tint, offset, arbitrary rotation, scale, elevation, and locks from the Brush Inspector; or
- shifted transactionally by inserting or deleting the selected row or column span.

Each accepted operation is one Tile workspace Undo item. Invalid or over-limit operations leave the document unchanged.

## Tile Definition Editor

Select a TileSet tile, expand **Tile Definition Editor**, and edit through **Apply Tile Definition** or the focused typed controls.

### Basic Tiles

Basic Tiles use one sprite region, pivot, and collider mode. Collider choices are None, Grid, or Sprite Outline.

### Animated Tiles

Select tiles in display order, set **Frame duration**, and choose **Use Selected Tiles as Frames**. Animation settings include minimum and maximum speed, start time, start frame, play-once, pause, and controlled physics refresh on frame changes.

### Rule Tiles

Choose topology, match transform, output kind, neighbor condition, and neighbor offset. Select one or more output tiles and choose **Add Rule**. Rules may use fixed, weighted-random, or animated output and fixed, rotated, or mirrored matching. Repeating **Add Rule** appends ordered rules; **Clear Rules** removes them.

Topology-specific rules diagnose incompatible active targets instead of silently changing their semantics.

### Rule Overrides

Select the intended override tile plus one other Rule Tile and choose **Override Selected Rule**. The selected source rule is retained by stable reference and its known output tiles are mapped to the override tile. Missing source content remains diagnosable instead of being discarded.

### Custom Tiles

Custom Tiles retain a stable custom type ID, type version, and opaque JSON payload. Missing or incompatible behavior displays a placeholder and preserves the record. Native behavior is allowed only when an explicitly declared, hash-bound project runtime module exports the accepted tile-extension ABI in a disposable worker.

## Grid and layer renderer settings

The Grid and Layer Renderer panel supports:

- Rectangular, Hex Point Top, Hex Flat Top, Isometric, and Isometric Z-as-Y layouts;
- cell size, gap, and tile anchor;
- layer visibility and tint;
- material reference in the durable layer contract;
- sort order;
- chunk or individual renderer mode;
- animation rate; and
- culling padding.

Projection, inverse picking, neighbors, line traversal, and collision footprints come from one portable native grid owner shared by editor and runtime lowering.

## Saving, Undo, and recovery

- **Save** atomically publishes the scene and every dirty affected TileSet, palette, and Tilemap.
- **Save All** uses the same validated transaction boundary.
- Tile workspace Undo/Redo covers TileSet definitions, palette organization, layers, cells, selections, and structural edits.
- Scene command Undo/Redo covers GameObject Brush placement and scene attachment.
- A transient Windows sharing violation uses bounded retry without in-place overwrite.
- A persistent lock restores the previous valid destination, leaves the editor state dirty, and reports an actionable retry diagnostic.

Opening a file never upgrades it merely because a newer reader exists. Unknown or custom records are preserved across read/edit/write whenever the containing contract remains valid.

## Runtime and support status

Runtime snapshot v5 carries resolved grid projection, multiple TileSets/textures, typed tile results, per-cell properties, animation data, renderer settings, picking data, and neutral collision intent. Snapshot v4 remains explicitly readable.

MonoGame and KNI have separate adapter implementations. KNI remains experimental; local focused evidence does not establish production KNI or complete cross-platform support. Hosted Windows, Ubuntu, and macOS Release/ASan gates and the unchanged frame/input thresholds remain authoritative.

## Current draft limitations

PR #6 remains a draft while final aggregate and hosted evidence is incomplete. The current implementation deliberately does not provide Unity branding, icons, serialized formats, C# TileBase/GridBrush APIs, render-pipeline-specific fields, XML Tiled import, object-layer import, image-collection TileSets, reimport/merge, plugin installation/update, a marketplace, or trusted in-editor native code.

Sprite-outline and non-rectangular composite collision acceptance remains bounded by the engine-owned neutral physics contract and must not be represented as complete Box2D polygon/composite evidence until its named final matrix is recorded. Native brush proposals are validated in the disposable host, but project-authored custom brush UI remains disabled unless an accepted editor command bridge is present. These limits preserve authored data and keep unsupported behavior visible.
