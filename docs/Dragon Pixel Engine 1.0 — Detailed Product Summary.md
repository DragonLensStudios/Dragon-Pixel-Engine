Dragon Pixel Engine is a standalone, cross-platform game engine and visual development environment designed to bring a Unity-familiar workflow to MonoGame and KNI development.

Its purpose is not to replace MonoGame or KNI. Instead, Dragon Pixel Engine adds the systems those frameworks intentionally leave to developers: a scene editor, GameObject and component workflow, Inspector, asset management, prefabs, input configuration, physics authoring, project creation, migration, build orchestration, packaging, recovery, plugins, and automation.

In practical terms, Dragon Pixel Engine operates as both an engine and a framework harness:

|Layer|Responsibility|
|---|---|
|Engine|Scenes, GameObjects, components, prefabs, input, physics, serialization, assets, commands, editor tooling, and portable runtime contracts|
|Framework harness|Starts and supervises MonoGame/KNI workers, supplies scene snapshots, controls Preview and Play, transfers input, receives rendered frames, manages crashes, rebuilds, packaging, and framework capabilities|
|Framework adapters|Translate Dragon Pixel’s portable rendering and runtime data into real MonoGame or KNI graphics, content, input, and platform operations|

The result is a visual, component-oriented development environment without forcing projects to abandon the flexibility and direct framework access that make MonoGame and KNI attractive.

## Unity-familiar editor experience

Dragon Pixel Engine uses familiar concepts such as GameObjects, scenes, a Hierarchy, Inspectors, assets, components, prefabs, Scene and Game views, and Edit/Play modes. It is an original Qt-based editor and does not copy Unity branding, proprietary assets, serialization, or implementation details.

Version 1.0 is intended to include:

- A Project Hub with New Project, Open Project, and recent projects.
- Built-in 2D and 3D project templates.
- Clean new-scene creation with appropriate default cameras and lighting.
- A completely repositionable dock-grid workspace.
- Dock splitting, tab groups, floating panels, grouped movement, saved layouts, and deterministic layout reset.
- Independent Scene and Game views that can be tabbed or displayed side by side.
- Built-in 2D, 3D, and debugging workspace arrangements.
- High-DPI, high-contrast, keyboard-only, and screen-reader-aware controls.

The principal editor panels are:

- Scene View
- Game View
- Hierarchy
- Project Browser
- Inspector
- Tile Palette
- Console
- Build and project settings
- Migration tools
- Plugin management and diagnostics

## GameObjects, components, and scenes

“GameObject” is the author-facing name. Internally, Dragon Pixel uses a portable entity record with a stable UUID, name, enabled state, parent, sibling order, and ordered list of components.

The public authoring model supports:

- Spatial and non-spatial GameObjects.
- Parent/child hierarchies.
- Stable object and component identities.
- Enable and disable states.
- Multi-selection.
- Reparenting and sibling reordering.
- Duplicate, delete, group, and subtree operations.
- Atomic multi-object changes.
- Undo and redo across component, hierarchy, prefab, asset, and scene operations.
- Unknown or unavailable component preservation.

All authoritative modifications pass through a validated command system. A failed multi-object operation changes nothing, and Undo restores the complete previous state—including distinct values from mixed selections.

Runtime-oriented subsystems may internally use dense, data-oriented storage, but the durable authoring model remains stable, inspectable, and language-neutral.

## Inspector and multi-object authoring

The Inspector is designed around a Unity-familiar component-card workflow.

Version 1.0 includes:

- A GameObject header with enabled state and object information.
- Searchable, collapsible component cards.
- A searchable Add Component control.
- Clear C#, C++, native, managed, and data-only ownership indicators.
- Add, remove, reset, enable, disable, copy, paste, and reorder operations.
- Typed fields for numbers, strings, enums, vectors, colors, booleans, rotations, lists, objects, dictionaries, asset references, and entity references.
- Range, nullability, reference-type, and custom validation.
- Specialized Transform and collider controls.
- Read-only preservation of missing, newer, or incompatible components.
- Safe mixed-value editing across multiple selected GameObjects.
- Custom metadata-driven property drawers.
- Recursive structured editing instead of raw JSON editing.

Authors can create multiple Inspector panels. Each Inspector can independently follow the active selection or lock onto specific GameObjects. A locked Inspector remains focused on its targets while the user selects other objects elsewhere.

## C# and C++ scripting

Dragon Pixel Engine supports C# and C++ components inside the same scene without maintaining separate GameObject models.

### C# authoring

The recommended C# API is the `GameObjectController` base class. A controller receives:

- A stable `Guid` identifying its GameObject.
- A shared, non-null Transform.
- Position, rotation, and scale.
- Variable and fixed delta time.
- The current immutable input state.
- Device-independent named input actions.
- Simple lifecycle methods such as `Enabled`, `Disabled`, `Update`, and `FixedUpdate`.

Generated C# components expose only their serialized properties and script source in the Inspector. Internal lifecycle implementation is not presented as fake serialized data.

### C++ authoring

C++ components use a stable, versioned C function-table ABI. C++ exceptions, STL containers, Qt objects, MonoGame objects, KNI objects, and raw owning pointers never cross the public boundary.

Both languages participate in a complete runtime lifecycle:

- Create and initialize
- Enable
- Fixed update
- Variable update
- Late update
- Render submission
- Disable
- Destroy and shutdown

A component failure disables only the affected instance, records a structured diagnostic, and attempts safe cleanup without crashing the editor or corrupting other components.

### Rider integration

Project source files appear directly in the Project Browser. Authors can create C# scripts or C++ components and open them in JetBrains Rider from either the Inspector or Project Browser.

Dragon Pixel generates a disposable Rider solution under the project’s derived metadata area. Editing source does not silently build or execute it. Component compilation is performed by supervised build workers, and successful rebuilds reload Preview or Play through a clean worker restart.

Dragon Pixel 1.0 is not intended to contain a general-purpose embedded IDE.

## Scene and Game views

The Scene View provides the authoring environment, while the Game View displays output from the selected primary scene camera.

Scene authoring includes:

- 2D and 3D camera modes.
- Pan, zoom, orbit, fly, and focus navigation.
- Grid display and snapping.
- Click picking through a runtime ID buffer.
- Selection outlines.
- Move, Rotate, and Scale gizmos.
- Local and global transform orientation.
- Camera, light, and collider overlays.
- Transactional gizmo changes with Escape-to-cancel.
- Drag-and-drop object creation from sprites, meshes, and prefabs.

The Game View displays real framework-generated output. During Play, it owns keyboard, mouse, and gamepad capture. Focus loss, Pause, Stop, worker failure, or capture release clears the input state to prevent stuck buttons or movement.

## Preview, Simulate, and Play isolation

The editor never executes arbitrary project code merely because a project, scene, or Inspector is open.

Dragon Pixel runs separate supervised processes:

- A Preview worker mirrors the current authoring scene.
- Simulate creates disposable physics simulation state without changing saved content.
- Play creates an immutable snapshot and starts an isolated Play worker.
- Pause stops simulation updates while preserving rendering and diagnostics.
- Stop destroys the Play worker and discards runtime-only changes.

If project code crashes, the worker can be restarted without closing the editor or corrupting the authoring scene. Runtime Transform changes are also discarded on Stop, crash, or reload.

An “Apply Play Changes” workflow is deliberately deferred until it can be implemented selectively through the command and validation system.

## MonoGame and KNI framework support

MonoGame is the primary runtime adapter and first production conformance target.

The MonoGame adapter manages:

- Real MonoGame graphics devices.
- Render targets and frame production.
- Sprite and mesh submission.
- Depth, effect, material, and lighting state.
- Asset binding.
- Fixed and variable update scheduling.
- Runtime input.
- Diagnostics and performance counters.
- Deterministic shutdown and worker recovery.

KNI uses a completely separate adapter and worker composition. KNI types are not mixed into the MonoGame adapter, and portable components contain neither framework’s private types.

KNI will remain labeled experimental until it passes the same scene, lifecycle, rendering, input, content, packaging, shutdown, and cross-platform conformance matrix as MonoGame.

Advanced projects are not prevented from using framework-specific features. Dragon Pixel exposes capability and extension boundaries instead of forcing every project into only the lowest common denominator.

## 2D development features

Dragon Pixel 1.0 is intended to provide a complete first-party 2D authoring workflow.

Core 2D support includes:

- Orthographic cameras.
- Sprite rendering.
- Sprite assets and previews.
- 2D transforms and depth ordering.
- Box2D-based physics.
- RigidBody2D.
- BoxCollider2D.
- CircleCollider2D.
- TilemapCollider2D.
- Collider overlays and sensor configuration.
- Layer and mask filtering.
- Density, friction, restitution, gravity, damping, velocity, and continuous-collision settings.

### Complete Tilemap Editor

The Tilemap Editor supports:

- Rectangular grids.
- Point-top hexagonal grids.
- Flat-top hexagonal grids.
- Isometric grids.
- Isometric Z-as-Y grids.
- Multiple TileSets per map and palette.
- Independent reusable Tile Palette assets.
- Sparse, chunked Tilemaps.
- Ordered layers.
- Per-layer visibility, tint, material, sort order, animation rate, and culling settings.
- Per-cell tint, offset, rotation, scale, elevation, flips, and edit locks.
- Basic tiles.
- Animated tiles.
- Rule tiles.
- Rule Override tiles.
- Custom versioned tiles.
- Stable deterministic random tile results.
- Grid and sprite-outline collision.
- Optional composite collision generation.

Available tools include Select, Move, Paint, Box Fill, Pick, Erase, Flood Fill, flip, and rotation.

Built-in brushes include:

- Random Brush
- Line Brush
- Group Brush
- GameObject Brush

The TileSet wizard supports automatic slicing, cell-size slicing, cell-count slicing, offset, padding, empty-cell handling, pivots, previews, and safe reslicing. A guided workflow can create the texture, TileSet, palette, Tilemap, selected Tilemap GameObject, and optional collider configuration.

Tiled JSON conversion is planned for supported atlas TileSets, tile animation, common layouts, Wang/terrain rules, and palette generation. Unsupported or ambiguous Tiled features are rejected with diagnostics instead of being converted incorrectly.

## Baseline 3D development features

The 1.0 3D workflow targets modest independent and indie-scale projects rather than AAA rendering.

It includes:

- Perspective and orthographic cameras.
- Static mesh rendering.
- Basic materials.
- Ambient lighting.
- Directional lights.
- Up to four point lights in the baseline scene renderer.
- Transform and selection gizmos.
- Real framework-generated Scene and Game output.
- Jolt-based 3D physics.
- RigidBody3D.
- BoxCollider3D.
- SphereCollider3D.
- Collision layers, masks, sensors, damping, gravity, velocity, friction, restitution, and CCD.

Version 1.0 does not promise shadows, terrain, skeletal animation, advanced materials, joints, characters, vehicles, soft bodies, static mesh colliders, global illumination, ray tracing, cinematic pipelines, or VR/AR.

## Prefabs

Prefabs are reusable GameObject subtrees with stable local object and component identities.

The 1.0 prefab workflow includes:

- Create Prefab from Selection.
- Instantiate linked prefabs.
- Nested prefab references.
- Property and structural overrides.
- Apply at a selected nesting level.
- Revert Selected or Revert All.
- Repair and Rebase.
- Unpack.
- Unpack Completely.
- Missing-source fallback preservation.
- Cycle and expansion-limit detection.
- Atomic multi-document saving and recovery.

If a prefab source becomes unavailable or incompatible, the local instance and its overrides remain preserved. Dragon Pixel reports the problem instead of silently deleting authored content.

A dedicated isolated Prefab Mode is not part of the currently defined 1.0 workflow.

## Input maps and rebinding

Input is framework-neutral and action based.

Projects can define:

- Multiple named control maps.
- Button actions.
- One-dimensional axis actions.
- Keyboard bindings.
- Mouse button, movement, and wheel bindings.
- Standard gamepad buttons, sticks, triggers, and directional pad bindings.
- Scale and dead-zone configuration.
- Persistent rebinding.
- Enabled or disabled control maps.
- Custom action names used identically by native and managed components.

The default map includes movement, jump, look, and fire actions. Scripts consume names such as `move.x` and `move.y` and do not need to know whether the value came from WASD, arrow keys, a gamepad stick, or a directional pad.

Touch, text/IME, controller rumble, motion sensors, cloud binding profiles, and locked raw-relative mouse capture are outside the first contract.

## Assets and Project Browser

The two-pane Project Browser provides:

- Folders and breadcrumbs.
- Thumbnail and list views.
- Search and type filters.
- Import and dependency status.
- Asset previews.
- Stable selection.
- Drag-and-drop.
- Create, rename, move, duplicate, remove, restore, and reimport operations.
- Recoverable project trash.
- Reference-impact analysis before removal.

Assets have stable IDs and explicit ownership:

- Copied assets are owned by the Dragon Pixel project.
- Linked assets remain external and read-only.
- Generated assets record their generator and inputs.

Imported content receives deterministic sidecars, hashes, importer versions, dependency revisions, previews, and rebuildable cache entries. The cache is disposable; losing it cannot invalidate the authoritative project.

PNG and JPEG sprite import form the first guaranteed importer path. glTF is the preferred portable 3D interchange direction, while the exact breadth of built-in 3D, audio, and font importers remains subject to the final 1.0 acceptance scope and plugin ecosystem.

## Existing MonoGame and KNI project migration

Dragon Pixel provides assisted, reversible migration—not a promise of fully automatic code conversion.

The migration workflow:

1. Scans the original solution without modifying it.
2. Identifies frameworks, target platforms, packages, source, assets, content projects, game loops, processors, native dependencies, and risky build behavior.
3. Produces JSON and human-readable reports.
4. Classifies findings as reuse, adapt, manual, unsupported, or unknown.
5. Lets the developer choose the startup project, framework adapter, asset policy, and transformations.
6. Generates a new sibling Dragon Pixel project through a contained staging process.
7. Builds and compares the original and migrated projects when feasible.
8. Provides a removal or rollback manifest for generated content.

The original source tree is never rewritten or deleted by the default workflow.

## Project lifecycle, builds, and distribution

The complete 1.0 workflow is intended to support:

- Project creation and discovery.
- SDK and toolchain validation.
- Project-format upgrades.
- Interrupted-save recovery.
- Archive and restore.
- Supervised component and game builds.
- Structured build diagnostics.
- Cancellable build operations.
- Reproducible build-result manifests.
- Relocatable Windows, Linux, and macOS packages.
- Dependency, license, notices, and SBOM inventories.
- Signed update manifests.
- Side-by-side update staging.
- Last-known-good rollback.
- Clean install, update, rollback, and uninstall verification.

The running editor never overwrites its own installation. Updates are performed by a small external update helper after signature, compatibility, and artifact-hash validation.

## Plugins, Python, and AI automation

Plugins can contribute:

- C# or C++ runtime components.
- Importers.
- Commands.
- Declarative editor panels.
- Property drawers.
- Build targets.
- Python tools.
- Automation methods.
- Tile and brush extensions.

Runtime plugin code executes inside workers. Plugin metadata can be inspected without loading the code. Crashed or incompatible plugins are quarantined while their serialized project data remains preserved.

Python and AI tools run as external, capability-limited processes. They can inspect projects, analyze assets, propose changes, run builds and tests, generate documentation, and stage component, prefab, or scene changes.

They cannot directly rewrite authoritative project files. Proposed mutations pass through the same validation, dry-run, command, approval, undo, and audit systems used by the editor.

## Reliability and data protection

Data safety is a defining feature of the architecture:

- Deterministic, source-control-friendly UTF-8 JSON.
- Stable UUID-based identity.
- Unknown and newer data preservation.
- Atomic file replacement.
- Multi-document transaction journals.
- Recovery copies.
- Compare-before-write conflict detection.
- Save/Discard/Cancel prompts.
- Worker isolation.
- Runtime-state separation.
- Structured diagnostics.
- Crash recovery.
- Sanitizer, leak, corruption, interruption, and long-running-session qualification.
- Local, user-initiated, previewable support bundles.
- No network telemetry by default.

## What Dragon Pixel Engine 1.0 will not be

The 1.0 release is intentionally not:

- A replacement for MonoGame or KNI.
- A complete Unity clone.
- A general-purpose IDE.
- A visual scripting environment.
- A networking engine.
- An asset marketplace.
- A fully automatic arbitrary-project converter.
- An Unreal integration.
- A production Unity integration.
- An AAA renderer.
- A runtime environment for Python or AI models inside the real-time game loop.

Unity support in 1.0 is limited to a `.NET Standard 2.1` contracts-and-bridge prototype tested against Unity’s Mono and IL2CPP-compatible paths.

## Current development status

The design describes the intended Dragon Pixel Engine `1.0.0` contract; it is not a claim that 1.0 has already shipped.

As of the July 28, 2026 design revision:

- The full 1.0 architecture through `DPE-ARCH-0015` is accepted.
- Implementation is in progress across four development slices.
- Windows currently has strong Release and AddressSanitizer evidence, including a 45-of-45 registered test matrix.
- Ubuntu has earlier passing Release and AddressSanitizer evidence but has not completed the expanded suite.
- macOS retains an unresolved viewport-throughput gate.
- Full project lifecycle, migration, packaging, updating, plugin distribution, Unity bridge, accessibility, and release-qualification gates remain open.
- KNI remains experimental until its complete three-platform conformance matrix passes.

The newer Dragon Pixel Engine Design Document.md is the authoritative source for this summary and supersedes the older revision for the expanded 1.0 feature set.
