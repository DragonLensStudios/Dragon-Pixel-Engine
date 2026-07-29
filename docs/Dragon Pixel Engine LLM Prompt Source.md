# Dragon Pixel Engine LLM Prompt Source

> **Document role:** Prompt and generated-result provenance  
> **Design revision:** `DPE-ARCH-0015`
> **Result revision:** `RESULT-DPE-ARCH-0015`
> **Last reviewed:** 2026-07-28
> **Design paths:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine Design Document.md` and `C:\Projects\Github\Engines\Dragon Pixel Engine\docs\Dragon Pixel Engine Design Document.md`  
> **Documentation-system path:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine LLM Prompt Source.md`  
> **Repository mirror:** `C:\Projects\Github\Engines\Dragon Pixel Engine\docs\Dragon Pixel Engine LLM Prompt Source.md`

This is the second Dragon Pixel Engine living document. It preserves the request that initiated the architecture work, records the research inputs, and captures the current generated result. Its documentation-system and repository copies are required byte-identical mirrors. The design document is authoritative when implementation needs an exact decision, contract, risk gate, or acceptance criterion. This document is authoritative for prompt/result provenance.

## Maintenance Contract

- The original prompt section is immutable after `DPE-ARCH-0001`. Corrective context belongs in a dated annotation, not an edit to the prompt.
- Any accepted architecture change updates the design document first, then updates the result, source manifest, result revision, and revision history here.
- `Design revision` must match the design document exactly.
- Compatibility facts are reverified before dependency upgrades and whenever their review date is older than 90 days.
- The semantic content of the original first-structure prompt remains unchanged. Its final newline was normalized when physical mirrors were created; that normalization is recorded in the provenance manifest. Notes may accumulate user-supplied planning context, but every change must be mirrored and recorded.
- Malformed smart-quote byte sequences from the historical prompt were normalized to plain ASCII quotation marks/apostrophes in the transcription below. No semantic wording was changed.
- Every durable documentation artifact, plan, architecture/research result, and substantive generated response managed under the paired documentation roots is stored in the external documentation directory and the repository `docs` directory under the same filename. Both copies are updated and hash-verified in the same work item. Repository control files such as root `README.md`, `LICENSE.md`, and `AGENTS.md` remain outside this mirror set unless explicitly added.

## Provenance Manifest

| Input | Role | SHA-256 / identity |
| --- | --- | --- |
| `Dragon Pixel Engine First Structure Prompt Main Flow.md` at `DPE-ARCH-0001` | Original detailed request before final-newline normalization | `4479DDF35A3FF515419AA2C5296ED21F6712833226FABA947E9E90767A222291` |
| `Dragon Pixel Engine First Structure Prompt Main Flow.md` at `DPE-ARCH-0002` | Same prompt content with normalized final LF for exact mirroring | `C4914E9686259E00A051720A7BF289D99A3876E7E17CB8B14F800FF384A215BE` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0001` | Earlier product notes used by initial research | `B63016AC6D7D9876A9B1131D2C3F438AE49A48A6819C8AAA9A9A42AE4C8F4A3C` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0002` | Current notes including the documentation-mirroring request and normalized final LF | `54AF7F76159831B94CBCA5317E05849570CCC3CEA0598119FC5B293248B991B0` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0005` | Living intake including the request to complete Slice 1 | `DA2238DA74375D8A59EA4B313EF435C4DDABB75776702FC853EBA2D868E603C0` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0006` | Mirrored living intake including the functional-editor request | `9C494B09D58E052ED27F73C7B75CC447B7463C7C984F8DCDC40F6DC45C98BC6E` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0008` | Mirrored intake including the structured Inspector, tile, and Scene/Game request | `A88F590FD9441A103FD444B022946DD439F5268F3F59CFB82A346870006E1954` |
| `Plans/Dragon Pixel Inspector Tile Authoring and Scene Game View Plan.md` | Accepted DPE-ARCH-0008 execution plan and Windows execution evidence | `C12638CB6CE108EF91F3FE852C513BA85BDAECAFA6980317279F05651B3C88EC` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0009` | Mirrored intake including the request to complete Slices 1-4 and the full design-defined 1.0 feature set | `7861953CC92A06E0678E7C4099B82825AE25150D1D8A9E7751FF7A84A0020B6E` |
| `Dragon Pixel Engine Notes.md` at `DPE-ARCH-0010` | Mirrored intake including the fluid panel-grid, attachable script, explicit lifespan, object creation, and Scene View manipulation request | `F956A431C0F029C19648CE08CEC72A6C83315D99ADD93491B84AF3A6BE8C839A` |
| `Plans/Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan.md` at creation | Active gate-preserving Slices 1-4 master delivery plan and fresh Windows baseline | `EB9FCF2E261A85D5124C635B6F40B2E88A577E71051A3B13A9F7B8F8E7EC2CEA` |
| Dragon Pixel Engine repository | Repository state inspected before research | Git commit `b48360e59f55fd827d9513405c9fef42178b92be` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0005` | Prior implementation-conformance result | SHA-256 `C1A566DD292ADA58DF328C30DA0EDFF859D26334674C13CF401DFBDB676283AF` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0006` | Expanded authoritative functional-editor architecture with implementation evidence current through 2026-07-25 | SHA-256 `05A47EFE171613AE7301D56F6608981A253535F773F532B3B86202CBCD53EBA4` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0007` | Inspector/component/input architecture and corrected input-evidence interpretation | SHA-256 `44B428F1033ADFC941F97249BD0A7677957FA223908EBABA94A16FB2F0275C24` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0008` | Structured objects/interfaces, worker modules, tile authoring, split Scene/Game architecture, and Windows implementation evidence | SHA-256 `62FE4C17B22857A5E267B72C2A4D67109F372662563CDA26970693BF606F3380` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0009` | Full Slices 1-4/1.0 delivery architecture, project lifecycle/migration/package/plugin/Unity/release contracts, ADRs through 0023, POCs M-S, and fresh Windows evidence | SHA-256 `C02AD39F673AAFAA72D56CBD3B976FE6BB86EE9F6BDAFFCD451718863EE72A6D` |
| `Dragon Pixel Engine Design Document.md` at `DPE-ARCH-0010` | Fluid full-grid Qt workspace plus backward-compatible managed/native full project-component lifecycle contract | SHA-256 `497E246E24FCAD41DF1FDA2E113D6D689CF094B744CDEBFB949B3598176A8A4B` |

At the time of research, the repository contained only `README.md` and `LICENSE.md`; no engine implementation or prior `AGENTS.md` existed.

## Original Prompt

The following is the original detailed request, with only malformed smart quotes normalized as described above.

---

I want to use C:\Projects\Documentation\Engines\Dragon Pixel Engine

Design Document, and then the LLM prompt source with original prompt and result these 2 documents are living documents modified as changes are made always should be kept in a Agent.md file and used for all future development for this project.

I want to research and plan the architecture for a new game engine and editor called the Dragon Pixel Engine. Do not attempt to design or implement the entire engine at once. Begin with foundational architectural decisions and divide the work into small, practical milestones.

### Product vision

The Dragon Pixel Engine will be:

- A standalone game engine and visual editor.
- A tooling and integration layer for existing engines, initially Unity and potentially Unreal Engine.
- Primarily an engine and editor for projects built with MonoGame and KNI.
- Capable of importing or migrating existing MonoGame and KNI projects into a visual, component-based editing workflow.
- Designed to provide an editor experience comparable to Unity while retaining its own modular architecture.

The standalone editor should include:

- A scene/render viewport.
- An entity or hierarchy explorer.
- A project and asset explorer.
- An Inspector for viewing and editing entities and their components.
- A console and logging panel.
- Play, pause, stop, testing, and debugging workflows.
- Component creation and attachment.
- Scene and project management.
- Dockable, designer-friendly editor panels where practical.

"Unity parity" refers primarily to familiar concepts and workflows, not to copying Unity's proprietary implementation or visual assets.

### Technology direction

Research and evaluate the following proposed technology choices:

- The standalone native editor and core native engine should be written in modern C++.
- Game and component scripting should support both C# and C++.
- MonoGame and KNI integrations should use .NET 10 and C# 14 where supported.
- Unity-facing shared libraries should target a compatible framework such as .NET Standard 2.1, but current Unity compatibility must be verified.
- Shared libraries must be divided carefully so that framework-specific dependencies do not leak into portable engine abstractions.
- Python should support AI-assisted workflows, automation, content processing, project analysis, and development tooling.
- Unreal Engine integration is a future-facing consideration and should not distort the initial architecture.

Identify interoperability concerns involving:

- C++ and C# communication.
- Native library loading and stable C APIs.
- Runtime hosting and embedding.
- Managed and unmanaged memory ownership.
- Exception and error boundaries.
- Serialization across runtimes.
- Debugging and hot reload.
- Cross-platform support.
- MonoGame, KNI, Unity, and future Unreal compatibility.

### Architectural principles

Use an interface-first, modular, component-based architecture.

Prefer interfaces and explicit contracts at system boundaries, but avoid creating interfaces that provide no meaningful abstraction. Clearly distinguish among:

- Public engine contracts.
- Internal implementation interfaces.
- Runtime-specific adapters.
- Editor services.
- Project and asset services.
- Extension and plugin APIs.

Nearly every major system should be modular and replaceable where this provides practical value. Apply component-based design to both gameplay and editor functionality.

The Core Engine should use an entity and component model.

At minimum, every entity must have:

- A persistent GUID identifier.
- A string name.
- A collection of components.
- A lifecycle appropriate to scenes and projects.
- Serialization support.
- A clear ownership relationship with its scene or world.

Components may be implemented using C# or C++. Research how mixed-language components can participate in one entity model without creating duplicate concepts or unsafe ownership rules.

Evaluate whether the engine should use:

- A traditional object-oriented entity-component model.
- A data-oriented ECS.
- A hybrid model with authoring components and optimized runtime representations.

Do not select an ECS approach merely because it is fashionable. Recommend the model that best supports editor usability, scripting, serialization, interoperability, and MonoGame/KNI integration.

### Required modules to investigate

Create an initial system map covering:

- Core object and identity model.
- Entities, components, scenes, worlds, and prefabs.
- Engine lifecycle and game loop.
- Rendering abstractions.
- MonoGame and KNI adapters.
- C# scripting.
- C++ scripting and native extensions.
- Serialization and version migration.
- Reflection, metadata, and Inspector property exposure.
- Asset database and content pipeline.
- Project and workspace management.
- Command, undo, and redo systems.
- Selection and editor state.
- Scene viewport and editor camera.
- Hierarchy and project explorers.
- Inspector.
- Console, diagnostics, and structured logging.
- Play mode and edit mode separation.
- Testing and debugging.
- Plugin and extension architecture.
- Python-based automation and AI workflows.
- Importing or migrating existing MonoGame and KNI projects.
- Unity integration.
- Future Unreal integration.
- Packaging, updating, and release management.

### Existing project migration

Research a migration strategy for existing MonoGame and KNI projects.

The process should:

- Inspect the existing project without immediately modifying it.
- Detect project structure, target frameworks, dependencies, content pipelines, game loops, screens, assets, and services.
- Generate a migration report.
- Identify code that can be reused directly.
- Identify code that requires adapters or manual changes.
- Create Dragon Pixel Engine project metadata.
- Preserve the original project or create a reversible migration path.
- Allow migrated content, scenes, entities, and components to be edited visually.
- Avoid promising fully automatic conversion where static analysis cannot determine developer intent.

Explain what can realistically be automated in the first release and what should remain an assisted workflow.

### Python and AI pipeline

Design Python as an external tooling and automation layer rather than an unrestricted dependency inside the real-time game loop.

Research a pipeline that could support:

- Project inspection.
- Code and asset analysis.
- Migration assistance.
- Asset metadata generation.
- Content processing.
- Automated validation.
- Test generation and execution.
- Documentation generation.
- AI-assisted component or scene creation.
- Batch editor operations.
- Communication with the editor through a versioned command API, local service, or message protocol.

The design must include permissions, audit logs, validation, cancellation, reproducibility, and safeguards against AI tools directly corrupting project files.

### Development slices

Plan development in four major slices. Each slice must produce a usable and increasingly stable project workflow.

#### Slice 1: Core, infrastructure, scaffolding, and core editor

Focus on:

- Repository and module structure.
- Architecture boundaries and interface conventions.
- Entity, component, scene, and GUID foundations.
- Serialization foundations.
- Native/managed interoperability proof of concept.
- Basic MonoGame/KNI runtime integration.
- Editor application shell.
- Scene viewport.
- Hierarchy or entity explorer.
- Project explorer.
- Basic Inspector.
- Console and structured logging.
- Edit, play, pause, and stop state model.
- Minimal project loading and saving.
- Testing, diagnostics, and build infrastructure.
- Initial Python automation boundary.

The result should be a minimal vertical slice in which a project can be opened, a scene displayed, an entity selected, components inspected, and the project run.

#### Slice 2: Designer-friendly tooling and UI

Focus on:

- Refined docking and workspace layouts.
- Improved Inspector controls.
- Property metadata and validation.
- Drag-and-drop workflows.
- Scene manipulation tools and gizmos.
- Asset browsing and previews.
- Undo and redo.
- Multi-selection.
- Prefabs or reusable entity templates.
- Designer-friendly error messages.
- Custom editor and property drawer extensions.
- Workflow improvements informed by usability testing.

The result should allow non-programmers or technical designers to assemble and modify a simple project visually.

#### Slice 3: Project lifecycle and maintenance

Focus on:

- Creating projects from templates.
- Importing and migrating MonoGame/KNI projects.
- Opening and editing projects.
- Deleting or archiving projects safely.
- Project settings.
- Dependency and SDK validation.
- Version compatibility checks.
- Project format migrations.
- Backups and recovery.
- Editor and engine updates.
- Plugin management.
- Packaging and build workflows.
- Recent projects and project discovery.

The result should provide a reliable end-to-end project lifecycle.

#### Slice 4: Version 1.0 stabilization and polish

Focus on:

- Runtime and editor stability.
- Performance profiling and optimization.
- Crash handling and recovery.
- Data integrity.
- Backward-compatible project upgrades.
- Documentation and tutorials.
- Accessibility and keyboard workflows.
- Cross-platform verification.
- Packaging and distribution.
- Security review.
- API and plugin compatibility policies.
- Long-running project testing.
- Migration testing against real MonoGame/KNI projects.
- Release criteria for version 1.0.0.

The result should be a stable, documented, distributable 1.0 release rather than merely a feature-complete prototype.

### Research questions

Answer these questions before recommending a detailed implementation:

1. Which responsibilities belong in the portable engine core, native editor, managed runtime, framework adapters, and project tooling?
2. What is the safest and most maintainable C++/C# interoperability model?
3. Should the standalone editor own the engine process, embed it, or communicate with a separate runtime process?
4. What UI framework best supports a modern C++ editor with docking, native rendering integration, accessibility, and cross-platform potential?
5. How should reflection and metadata work across both C# and C++ components?
6. What serialization format should be used for projects, scenes, entities, components, assets, and editor state?
7. How should missing, renamed, or incompatible components be preserved during loading?
8. How should edit mode and play mode be isolated so runtime changes do not unintentionally corrupt saved scenes?
9. How should the MonoGame and KNI adapters differ?
10. Which libraries can genuinely be shared with Unity, and which require platform-specific adapters?
11. How should Python and AI tooling communicate with the editor safely?
12. What is a realistic migration path for existing MonoGame/KNI projects?
13. Which features are essential for Unity-like usability in version 1.0, and which should be deferred?
14. What are the largest technical risks, and which prototypes should be built first to reduce them?

### Expected research output

Produce the following deliverables:

1. A concise product definition and explicit non-goals.
2. A proposed high-level architecture.
3. A module and dependency map.
4. Recommended technology choices with alternatives and tradeoffs.
5. A C++/C# interoperability proposal.
6. An entity/component and scene model.
7. A serialization and versioning strategy.
8. An editor architecture and panel model.
9. A MonoGame/KNI integration strategy.
10. An existing-project migration strategy.
11. A Python and AI tooling architecture.
12. A plugin and extension model.
13. The four-slice roadmap with milestones and acceptance criteria.
14. A risk register.
15. A list of architecture decision records that should be created.
16. A small set of proof-of-concept prototypes needed before full development.
17. A recommended first implementation chunk for Slice 1.

For every major recommendation:

- Explain why it fits the Dragon Pixel Engine.
- Describe important tradeoffs.
- Identify viable alternatives.
- Separate verified facts from assumptions.
- Cite current primary documentation where technology compatibility is involved.
- Avoid premature implementation details unless they are needed to validate an architectural decision.

End with a focused plan for the first small architecture and prototyping iteration. Do not generate the complete engine or an enormous backlog.

---

## Clarifications Locked During Planning

The following product choices were confirmed after inspecting the empty engine repository and historical documentation:

- The two living documents remain authoritative in `C:\Projects\Documentation\Engines\Dragon Pixel Engine`; root `AGENTS.md` points to them.
- Slice 1 validates Windows, macOS, and Linux rather than beginning as Windows-only.
- Qt is used through dynamically linked LGPL modules with an explicit compliance process.
- Version 1.0 treats 2D and 3D as first-class authoring requirements.
- Unity receives portable contracts and a bridge prototype before 1.0, not a supported full integration.

## Documentation Mirroring Request and Result

### Request added 2026-07-24

> I want to mirror the documentation markdown files in the repo so it is in-sync with the design document and LLM prompt source, this directory C:\Projects\Documentation\Engines\Dragon Pixel Engine can be used for documentation and mirrored inside the docs folder in the repo so all these markdowns are stored in the repo and in my document system. add to agents file that all documents and plans and responses will be added as expected.

### Result `RESULT-DPE-ARCH-0002`

- All four Markdown files in the external Dragon Pixel Engine documentation directory are mirrored under the repository `docs` directory with identical filenames and bytes.
- The logical documents no longer rely on one physical path as an automatic winner. A mismatch blocks further work until it is intentionally reconciled.
- `AGENTS.md` lists every pair and requires same-work-item synchronization, UTF-8/LF formatting, and SHA-256 equality checks.
- Future durable documents, plans, architecture/research results, and substantive responses must be written to an appropriate Markdown file in both locations. Chat-only delivery is insufficient for the project record.
- The repository mirror supplies version history while the external mirror remains available to the user's documentation system.

## Functional Editor Expansion Request and Result

### Request accepted 2026-07-24

The user requested implementation of **Dragon Pixel Engine Functional Editor: Slice 1 Closure and Complete Slice 2**. The approved scope requires a genuinely operable editor in which authored GameObjects drive the real MonoGame/KNI viewport and play session, while also delivering the full Slice 2 authoring workflow: command-based hierarchy and Inspector editing, project browsing, undo/redo, drag/drop, multi-selection, unified 2D/3D manipulation, linked nested prefabs, asset previews, baseline lighting, and live Box2D/Jolt physics.

The complete approved execution plan is preserved byte-identically at:

- `docs/Plans/Dragon Pixel Engine Functional Editor Slice 1 Closure and Complete Slice 2 Plan.md`
- `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine Functional Editor Slice 1 Closure and Complete Slice 2 Plan.md`

### Result `RESULT-DPE-ARCH-0006`

- The architecture work boundary expands from Slice 1 platform closure to Slice 1 closure plus complete Slice 2 implementation. The original Slice 1 acceptance gates remain unchanged.
- macOS evidence is corrected: 14 of 15 registered tests pass in available Release and native AddressSanitizer runs, while `poc_b.worker_viewport` fails the 1280×720 30 FPS gate with observed presented rates of 14.2–20.8 FPS.
- The previous POC B control-response timing is not accepted as input-to-present evidence. A revision-correlated measurement must show median latency below 100 ms for MonoGame and KNI independently.
- Durable formats advance to project v2, scene v3, prefab v1, asset v2, metadata v3, and runtime snapshot v3, all with deterministic migrations and unknown-data preservation.
- The editor adopts service-backed Qt models, command-only authoritative mutation, dirty/savepoint behavior, compound undo/redo, typed multi-selection inspection, browse/open/assign assets, unified 2D/3D Scene View interaction, structured diagnostics, workspaces, and Qt Test/accessibility gates.
- Runtime acceptance requires actual framework graphics-device rendering, scene-dependent readback, revision correlation, resize, diagnostics, and ID-buffer picking. Synthetic continuous frames cannot satisfy acceptance.
- Box2D 3.1.1 and Jolt 5.6.0 are selected behind private engine-owned backends with fixed-step world ownership and a negotiated physics C ABI extension.
- Linked nested prefabs use source-plus-overrides ownership, stable path-qualified mappings, canonical source revisions, fallbacks, rebasing, cycle guards, and atomic multi-document saves.
- POCs E-H gate real framework rendering, nested prefabs, physics ownership, and real Qt interactions before their ADRs become Accepted.

### Implementation progress annotation 2026-07-25

This annotation updates evidence for `RESULT-DPE-ARCH-0006`; it does not change the immutable Original Prompt, accepted architecture, design/result revision, 2026-07-24 compatibility review date, ADR gates, or acceptance thresholds.

- The current Windows 11 x64 worktree passes the expanded 42-of-42 strict Release matrix in 183.73 seconds and 42-of-42 MSVC AddressSanitizer matrix in 215.54 seconds.
- The current Ubuntu 24.04 x64 worktree passes 36 of 36 strict Release tests in 102.20 seconds and 36 of 36 Clang AddressSanitizer tests in 101.97 seconds. Its registered POCs E-H also pass.
- Manual Windows QA used a disposable `out/dev` sample and verified the typed Inspector, indexed Project Explorer with thumbnails and status, one-transaction preset creation plus Undo, isolated Simulate, actual MonoGame/KNI preview and play output, pause, and stop.
- The evidence refresh adds queued graphics-thread one-pixel ID readback instead of a full ID-target `GetData` on every frame, a persistent seqlock reader with header-derived presented FPS, Unix position-independent code, and managed-child AddressSanitizer preload handling.

Current verbose viewport measurements are:

| Platform and run | Adapter | Presented FPS | Median viewport-command-to-present | Control response | Crash recovery | Device/readback | Publish |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows combined POC B | MonoGame | 60.0 | 15.6 ms | 466.4 ms | 683.2 ms | 3.1 ms | 0.1 ms |
| Windows combined POC B | KNI | 40.0 | 46.6 ms | 153.5 ms | 757.7 ms | 23.3 ms | 0.1 ms |
| Windows independent POC E | MonoGame | 60.1 | 15.7 ms | 399.7 ms | 638.4 ms | 2.6 ms | 0.1 ms |
| Windows independent POC E | KNI | 40.7 | 46.7 ms | 152.4 ms | 728.5 ms | 23.1 ms | 0.1 ms |
| Ubuntu combined POC B | MonoGame | 61.7 | 16.3 ms | 306.2 ms | 492.1 ms | 8.5 ms | 0.2 ms |
| Ubuntu combined POC B | KNI | 32.3 | 55.1 ms | 315.1 ms | 534.4 ms | 26.6 ms | 0.2 ms |
| Ubuntu independent POC E | MonoGame | 61.6 | 16.4 ms | 349.1 ms | 487.6 ms | 6.8 ms | 0.1 ms |
| Ubuntu independent POC E | KNI | 33.3 | 55.2 ms | 353.1 ms | 544.7 ms | 28.0 ms | 0.1 ms |

- Evidence correction at DPE-ARCH-0007: this probe advances a `viewportInput` revision without changing a camera, selection, gameplay action, or other render-affecting value. The recorded numbers prove viewport-command/frame correlation, not gameplay input consumption. POC J must make a real action alter device pixels/picking before the input-to-present gate can pass.
- macOS remains the last unclosed platform: its prior result is 14 of 15 tests, with the 1280×720 viewport performance gate failing, and it has no current POCs E-H or AddressSanitizer rerun. Full designer, linked-prefab, and accessibility matrices also remain incomplete. Slice 1 and Slice 2 are therefore unaccepted, and KNI remains experimental.

## Inspector, Custom Component, and Input Request and Result

### Request accepted 2026-07-25

> Alright let's work to make the inspector look and feel a lot closer to Unity, and make sure you can add components easily and have custom components, and scripts in C# and C++ also make sure I can have input control.

### Result `RESULT-DPE-ARCH-0007`

- Unity remains an interaction reference rather than a dependency. The Qt Inspector gains a GameObject header, component-card presentation, ownership/runtime badges, searchable Add Component, multi-selection-safe add/remove/reset/reorder, and nullable/filtered reference workflows through validated commands.
- A discovered correctness defect is now a blocker: an unequal multi-selection value was rendered as ordinary `<mixed>` editor text and could be committed without explicit user intent. DPE-ARCH-0007 requires first-class mixed state and mutation-free open/focus/close behavior before visual polish is accepted.
- Project format v3 adds contained component roots; component metadata v4 adds component-level implementation/source/policy fields. The editor reads JSON manifests only and never loads project code.
- Bounded C# and C++ creation may generate source stubs plus metadata. Unbuilt records remain authorable and preserved with honest runtime-unavailable diagnostics. General project module execution is not supported until POC I proves worker-only generated factories/C ABI plugins, lifecycle/error containment, restart cleanup, and editor-process exclusion.
- Embedded Play input is a framework-neutral full action state sent from the focused Qt viewport to the play worker through negotiated `runtimeInput`. Focus loss, Pause, Stop, crash, and capture release neutralize state. A managed Input Mover provides the first visible runtime-only proof through both adapters.
- The older revision-correlated latency result is corrected: it is viewport-command-to-present evidence because no render-affecting input was consumed. POC J must prove real Qt input changes actual MonoGame/KNI pixels and picks before latency acceptance.

## Structured Inspector, Tile Authoring, and Scene/Game Request and Result

### Request accepted 2026-07-25

The user approved the `Dragon Pixel Inspector, Tile Authoring, and Scene/Game View Plan`: implement a Unity-familiar typed component-card Inspector; nested, collection, polymorphic, and interface-filtered values; generated and worker-executed C#/C++ project components; independently dockable Scene and live Game views; and a reusable orthogonal TileSet/Tilemap authoring MVP with contained PNG intake, command-based tools, adapter rendering, and Box2D collision.

### Result `RESULT-DPE-ARCH-0008`

- Metadata v4 is completed with recursive value shapes, object/contract descriptors, component policy/module identity, and language-symmetric generation. Known values use typed drawers and path commands; opaque payloads remain read-only and preserved.
- Explicit project component builds produce content-addressed managed/native worker modules under `.dragonpixel/Cache`. The editor reads manifests but never loads project code; workers load managed factories and `dpe_component_plugin_v1` modules with contained lifecycle/input/diagnostics. Ordinary Windows launches bootstrap the Visual Studio C++ environment automatically.
- Independently dockable Scene and Game panels use named, revisioned outputs and independent mappings. Game uses a live primary-camera preview outside Play and an isolated Play worker during Play; Scene remains available. Focus-owned normalized input is neutralized on focus loss, Pause, Stop, and crash.
- `dpe.tileset` v1 and `dpe.tilemap` v1 define stable tiles, sparse layers/chunks, contained PNG slicing, transactional palette tools, atomic persistence, flattened runtime data, embedded immutable PNG bindings, MonoGame/KNI tile rendering, and static Box2D lowering.
- The Windows implementation adds an `InputMotion2D` proof whose `move.x` action changes actual MonoGame and KNI device pixels and whose neutral revision is presented without changing authoring data.
- Final Windows evidence is **42/42 Release tests in 183.73 seconds** and **42/42 MSVC AddressSanitizer tests in 215.54 seconds**. This validates the registered Windows scope for POCs I/K and the named-view/input implementation, but does not close their three-platform gates. Ubuntu and macOS expanded runs, single-preview-worker simultaneous-output evidence, complete adapter tile-pixel/pick/collision evidence, and full accessibility/manual designer review remain open.

## Slices 1-4 Version 1.0 Completion Request and Result

### Request accepted 2026-07-25

The user requested that `C:\Projects\Documentation\Engines\Dragon Pixel Engine` remain the living-document root, that repository `AGENTS.md` governance be followed, and that the active goal expand from Slice 1/2 closure to finishing Slices 1 through 4 with the complete, fully functional Dragon Pixel Engine 1.0 feature set defined by the Design Document.

### Result `RESULT-DPE-ARCH-0009`

- The active durable tracker becomes `Plans/Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan.md`. Earlier in-progress plans are superseded for active tracking only; no unfinished gate is marked complete or weakened.
- Slice acceptance remains evidence-sequential. Slice 1 retains the unchanged three-platform viewport, sanitizer, lifecycle, ownership, rendering, and data gates. Slice 2 retains the complete real-device, designer, prefab, physics, input, tile, multi-view, accessibility, distribution, and KNI gates.
- Slice 3 is activated with project-v4 lifecycle contracts, declarative 2D/3D templates, project discovery/settings/upgrades/archive/restore, asset-v3 import/cache ownership, reversible sibling MonoGame/KNI migration, declared worker builds, relocatable packages, plugin management, staged updates/rollback, and the bounded Unity bridge.
- Slice 4 is activated with one product-version source, frozen performance/memory budgets, long-running and corruption/recovery qualification, compatibility fixtures and policy, clean install/update/rollback/uninstall, signing/notarization/provenance/license review, accessibility, privacy-safe support bundles, documentation, and final release-candidate evidence.
- Missing ADR-0009 and ADR-0011 are created. Existing ADR-0016/0017 are added to the Design inventory, and ADRs 0018-0023 define lifecycle, migration, asset import, package/update, compatibility, and crash/privacy boundaries. All 23 ADRs remain `Proposed`; scope acceptance promotes none of them.
- POCs M-S gate project lifecycle, migration, asset/cache integrity, plugin trust, Unity AOT interoperability, package/install/update/rollback, and complete 1.0 release qualification.
- A fresh Windows 11 x64 baseline passes **42/42 strict Release tests in 192.18 seconds** and **42/42 MSVC AddressSanitizer tests in 222.45 seconds** with no sanitizer report. This supersedes the prior timing record for current Windows evidence only; Ubuntu/macOS, earlier Slice 1/2 blockers, KNI's experimental status, and POCs M-S remain open.
- `1.0.0` may not be claimed until every applicable slice and release gate has direct durable evidence. Unreal, AAA rendering, networking, a marketplace, visual scripting, general IDE behavior, and full Unity integration remain non-goals.

## Fluid Workspace and Component Lifespan Request and Result

### Request accepted 2026-07-26

The user requested that every editor panel be placeable through a fluid connected grid, specifically reporting that the reserved center area prevents correct window positioning. The user also requested more attachable script-like components, an explicit component lifespan and abstraction, functional object creation, Scene View manipulation, and continued completion of the editor.

### Result `RESULT-DPE-ARCH-0010`

- The empty central placeholder is removed. All built-in panels participate in the full `QMainWindow` dock grid with nested splits, tab groups, floating, grouped drag, animation, all-area placement, deterministic reset, and versioned per-user save/restore.
- Add Component exposes New C# Script and New C++ Component. Contained generation refreshes JSON-only editor metadata and attaches the new stable type to selected GameObjects through one validated transaction; generated records remain editable but explicitly unbuilt until Build Components succeeds in disposable workers.
- Existing `IProjectComponent` and native component ABI v1 remain compatible. The accepted full-lifespan extension adds managed enable/fixed/late/render/disable hooks and a size-tagged native ABI v2 phase dispatcher with v1 fallback.
- Worker dispatch is ordered as create, enable, bounded 60 Hz fixed update, variable update, late update, render submission, disable, and destroy. At most four catch-up steps run per presented update; excess time is diagnosed and dropped. Stage failures disable and clean up only the affected instance.
- Project code remains excluded from the editor process and may not mutate the authoring scene. Editor object creation, component attachment, and Scene View gizmos remain command-backed and undoable; runtime script state remains disposable on Stop/crash.
- Current Windows implementation proves the full-grid layout, deterministic reset/versioned restore, direct selected-GameObject script attachment, generated managed/native lifecycle sources, ordered v2 dispatch, native v1 fallback, bounded fixed-step drop, stage-failure containment, and disable-before-destroy cleanup. The relevant Release and MSVC AddressSanitizer editor/runtime/generator aliases pass; the Windows-only evidence does not close POC I, a slice, or any ADR.
- POC I and ADR-0014 remain open until ordered managed/native lifecycle, compatibility, failure, cleanup, packaging, and three-platform evidence passes. This revision does not promote KNI or any ADR and does not close a slice.

## Rider Script-Editing Request and Result

### Request accepted 2026-07-26

The user requested C# script editing by opening the project solution in JetBrains Rider, a Unity-familiar Inspector right-click edit action, source-file visibility in Project Explorer, and an end-to-end workflow for creating and editing scripts/components.

### Result `RESULT-DPE-ARCH-0011`

- Project Explorer treats stable contained `.cs`, `.cpp`, `.cc`, `.cxx`, `.h`, and `.hpp` files under declared component roots as visible Component Source entries. Newly generated sources appear after the same detached project-index rebuild that activates their metadata.
- A bounded `ScriptEditorService` regenerates a disposable Rider solution/project below `.dragonpixel/Ide/Rider`. It includes all validated component sources, uses the current `DragonPixel.Contracts` reference for managed editing, and is never authoritative project state.
- C# Inspector cards expose **Edit Script in Rider**; C++ cards expose **Edit Component Source in Rider**. Activating a Component Source row in Project Explorer performs the same handoff. The service opens the generated solution and selected file at line 1 through Rider's documented command-line interface.
- Rider resolution supports explicit configuration, platform command/Toolbox discovery, and normal installation locations. Launch uses an argument vector and detached process ownership; missing tools, source containment failures, or workspace-generation failures are structured editor diagnostics.
- Editing never loads, reflects, compiles, or executes project code in Dragon Pixel Editor. Build Components remains explicit and supervised; Preview/Play workers remain the only processes that consume validated runtime modules.
- POC I and ADR-0014 remain `Proposed` until the source-index/workspace/launch actions and the existing component-runtime gates pass on Windows, macOS, and Linux. Focused Windows proof does not close a slice, promote KNI, or constitute general IDE support.

## Managed GameObject Controller Request and Result

### Request accepted 2026-07-26

The user requested a managed C# structure centered on a `GameObjectController` base class and lifespan interfaces, with `Enabled`, `Disabled`, `Update`, and `FixedUpdate`; a stable GUID and `Vector3`-based Transform on every controller; no lifecycle implementation row in Inspector; and a `MyMover` script that moves its attached GameObject with WASD or arrow keys.

### Result `RESULT-DPE-ARCH-0012`

- Add the author-facing `IGameObjectControllerLifecycle` and abstract `GameObjectController` without removing or changing the existing `IProjectComponent` and `IProjectComponentLifecycle` compatibility contracts. The base class adapts worker dispatch to concise parameterless overrides.
- Every controller receives its entity `Guid`, a shared worker-owned `Transform` with `Vector3` position/Euler-rotation/scale, the current immutable input state, and elapsed/variable/fixed timing.
- A worker shares one transform among the managed scripts on an entity and overlays dirty controller transforms onto the disposable runtime render scene after simulation. Real rendering and picking consume the result; saved authoring JSON is never changed and runtime state is discarded on reload/Stop/crash.
- Generated C# scripts derive from `GameObjectController`. The mover implementation consumes existing `move.x`/`move.y` actions (WASD and arrow bindings), normalizes diagonals, reads its serialized Speed property, and applies delta-time-scaled translation.
- Inspector retains Script Source and exposed serialized fields but removes the synthetic Lifecycle row. Rider editing remains on the component context action and Project Explorer source activation.
- Managed transform writes on physics-controlled entities are final render/pick overrides in this increment; an explicit physics-control contract is required before claiming authoritative body motion. Native transform-control parity remains open under POC I.
- POC I and ADR-0014 remain `Proposed` until base dispatch, GUID/transform/input behavior, render/pick displacement, discard/recovery, compatibility, generation, Inspector behavior, packaging, and three-platform evidence pass.

## Configurable Input Map Request and Result

### Request accepted 2026-07-26

The user requested `MyMover` to behave like `Input Motion 2D` by consuming captured input and moving its GameObject, plus a simple configurable input system for keyboard, mouse, and gamepad with persistent rebinding and named control maps.

### Result `RESULT-DPE-ARCH-0013`

- Add project-owned `dpe.inputmap` format version 1 documents referenced by normal `input-map` asset sidecars, so the configuration is visible in Project Explorer and remains language/framework neutral.
- Each input map contains stable named control maps, uniquely named actions, and stable bindings expressed as canonical paths such as `keyboard/w`, `mouse/left`, and `gamepad/left-x`. Scales and dead zones combine deterministically into the existing full-state action protocol.
- Keep the focused Qt Game view as capture owner. Qt provides keyboard and mouse samples; SDL 3 provides standardized hot-plug-aware gamepad buttons, sticks, triggers, and d-pad input on the three desktop baselines. No toolkit or framework input enum crosses the durable or managed boundary.
- Add an Input Map editor that selects and manages maps/actions/bindings and persists rebindings only through a validated, atomic, compare-before-write `InputMapService`. Invalid or incompatible changes preserve both the last valid runtime map and the authoritative file.
- Ship a default Gameplay map where `move.x`/`move.y` combine WASD, arrows, the left stick, and d-pad; `jump`, `look.x`/`look.y`, and `fire` cover representative keyboard, mouse, and gamepad controls. `InputMotion2D` and `MyMover` consume the same `move.x`/`move.y` actions, so scripts remain device-agnostic.
- Retain an in-memory compatibility map for projects without an input-map asset, and neutralize complete input state on focus/capture/lifecycle/device-map boundaries. Text/IME, touch, locked raw-relative pointer mode, rumble/sensors, and cloud profiles remain deferred.
- ADR-0015 and POC J remain `Proposed` until schema/recovery/UI/device/rebinding/runtime/pixel/pick/timing evidence passes on Windows, macOS, and Linux. Focused Windows implementation cannot close a slice or promote KNI.

## New Project and Daily Authoring Workflow Request and Result

### Request accepted 2026-07-27

The user approved implementation of a Unity-familiar New Project and daily-authoring workflow: Project Hub, minimal 2D/3D projects and clean scenes, a two-pane manageable Project Browser, copied/linked image import, recoverable asset removal, linked-prefab drag paths, ordered Hierarchy multi-operations, and multiple independently lockable Inspectors that edit shared components transactionally.

### Result `RESULT-DPE-ARCH-0014`

- Add a no-project Project Hub and a contained `ProjectLifecycleService` for declarative minimal 2D/3D template creation, dry-run validation, deterministic IDs, staging/commit recovery, recent projects, and clean scene creation.
- Implement the accepted project-v4 and template-v1 contracts without silently rewriting project versions 1 through 3. New scenes remain scene v3.
- Expand `AssetService` and asset-v3 for copied/read-only-linked ownership, image import, stable hashes and identities, safe rename/move/duplicate, dependency impact, operation-owned trash, exact restore, and immutable runtime bindings. Initial renderable external content is PNG/JPEG sprites.
- Replace the read-only Project Explorer surface with a service-backed two-pane Project Browser and public command/keyboard/drag actions. Component source identity remains owned by the existing component and Rider workflows.
- Version drag payloads and support OS-to-Project import, Project internal moves, sprite/prefab-to-Scene or Hierarchy creation, compatible asset-field assignment, and locally owned Hierarchy-root-to-Project linked-prefab creation.
- Add ordered `SelectionService` state, atomic top-level Hierarchy multi-drag/reorder, and transactional multi-delete/duplicate/enable/group behavior.
- Make Inspector a reusable dock. Extra Inspectors can be independently locked to stable scene/entity identities; layout/count persists but locks reopen cleared. Shared-component edits retain explicit mixed state, all-target validation, one transaction, and exact Undo.
- Preserve scene-v3, prefab-v1, metadata-v4, runtime snapshot-v4, C ABI, managed lifecycle, Rider, and input-map compatibility. POCs F/H/M/O, all affected ADRs, slice closure, cross-platform support, and KNI remain open until complete evidence passes.

## Complete 2D Tilemap Editor Request and Result

### Request accepted 2026-07-28

The user approved expanding active draft PR #6 into a complete Dragon Pixel-owned 2D Tilemap Editor with Unity-familiar functional behavior: durable palettes, five grid layouts, multi-TileSet maps, typed basic/animated/rule/custom tiles, the documented built-in brush families, full selection editing, image slicing, configurable shortcuts, richer rendering/collision, both runtime adapters, expanded Tiled JSON conversion, and a safe public native extension boundary. The request explicitly retains one Tilemap asset with internal layers, universal logical palettes, worker isolation, original Dragon Pixel presentation, existing recovery guarantees, unchanged platform thresholds, experimental KNI status, and a stop before merge.

### Result `RESULT-DPE-ARCH-0015`

- Add `dpe.tileset` v2, `dpe.tilepalette` v1, and `dpe.tilemap` v2 with deterministic v1 migration, stable qualified tile references, multiple TileSets/textures, five grid layouts, sparse universal palette cells, typed/opaque definitions, rich per-cell state, and per-layer renderer settings.
- Add runtime snapshot v5 while retaining explicit snapshot-v4 compatibility. MonoGame and KNI consume one framework-neutral grid, animation, rule, rendering, picking, and collision contract; KNI remains experimental and separately reported.
- Evolve the existing TileDocumentService into the single Tile workspace mutation owner with unified validated commands, compound Undo/Redo, atomic Save All, and exact dirty/recovery behavior across TileSet, palette, Tilemap, and scene documents.
- Expand Create TileSet from Image with automatic/cell-size/cell-count slicing, offset/padding/empty policy/pivot, safe reslicing, layout selection, and a default full texture/TileSet/palette/Tilemap/GameObject/collider handoff.
- Replace the current combined palette/live-map surface with active palette and pinned/following target selection, neutral multi-cell clipboard, separate palette organization, configurable tool shortcuts, brush inspection, dedicated TileSet editors, full Grid Selection properties, and transactional structural edits.
- Add deterministic Rule/Override and Animated tiles; Random, Line, Group, and GameObject brushes; stable per-cell randomness; linked-prefab or command-backed clone placement; layout-aware None/Grid/Sprite Outline collision; and per-layer renderer modes.
- Add worker-only, size-tagged `dpe_tile_extension_plugin_v1` batch evaluation/proposal calls. Project-local modules cannot load into the editor or write project files; missing/crashed implementations preserve opaque records and degrade locally.
- Expand the isolated Tiled JSON converter to multiple atlas TileSets, orthogonal/isometric/staggered/hexagonal layouts, animation, representable Wang/terrain rules, palette publication, and an explicit isometric-Z-as-Y interpretation. Continue rejecting XML, compressed/encoded, object/image-collection, reimport, and unrepresentable semantics before publication.
- Require focused/full Windows Release and AddressSanitizer evidence plus the hosted Windows/Ubuntu/macOS Release/sanitizer matrix without changing thresholds. The work cannot promote POC K/O/P, an ADR, a slice, KNI, or a platform from partial evidence.

## Research Manifest

Research used primary vendor/project documentation and source on 2026-07-24:

| Topic | Verified result | Primary source |
| --- | --- | --- |
| .NET lifecycle | .NET 10 is LTS through November 2028 | [.NET support policy](https://dotnet.microsoft.com/en-us/platform/support/policy/dotnet-core) |
| C# language | C# 14 is current and supported on .NET 10 | [C# 14](https://learn.microsoft.com/en-us/dotnet/csharp/whats-new/csharp-14) |
| Native interop | `LibraryImport`, precise C signatures, `SafeHandle`, and unmanaged function pointers are recommended patterns | [.NET interop best practices](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices) |
| Native hosting alternative | `nethost`/`hostfxr` can host a framework-dependent .NET runtime; one compatible runtime is loaded per process | [.NET native hosting](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting) |
| MonoGame | Current guidance permits .NET 10 over its .NET 8 dependency | [MonoGame migration](https://docs.monogame.net/articles/migration/migrate_38.html) |
| KNI | Principal framework projects currently target .NET 8/.NET Standard 2.0 | [KNI project source](https://github.com/kniEngine/kni/blob/main/src/Xna.Framework/Xna.Framework.csproj) |
| Unity | Unity 6.3 supports .NET Standard 2.1 plug-ins and not .NET Core plug-ins | [Unity 6.3 LTS manual](https://docs.unity3d.com/6000.3/Documentation/Manual/dotnet-profile-support.html) |
| Qt version/platforms | Qt 6.11.1 is current and Qt 6.11 supports all target desktops | [Qt releases](https://doc.qt.io/qt-6/qt-releases.html), [platforms](https://doc.qt.io/qt-6/supported-platforms.html) |
| Qt editor needs | Qt exposes docking, render widgets, and widget accessibility | [`QDockWidget`](https://doc.qt.io/qt-6/qdockwidget.html), [`QRhiWidget`](https://doc.qt.io/qt-6/qrhiwidget.html), [accessibility](https://doc.qt.io/qt-6/accessible-qwidget.html) |
| Qt license | Dynamic LGPL use requires notices, source/relinking rights, and full LGPL compliance | [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations) |
| MonoGame offscreen rendering | Render targets can be bound and their texture data read back through `GetData` | [MonoGame render targets](https://docs.monogame.net/articles/getting_to_know/whatis/graphics/WhatIs_Render_Target.html) |
| Qt interaction testing | Qt Test supports GUI event and widget interaction tests | [Qt Test overview](https://doc.qt.io/qt-6/qtest-overview.html) |
| Box2D baseline | Box2D 3.1.1 is the pinned 2D physics release for this revision | [Box2D 3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1) |
| Jolt baseline | Jolt 5.6.0 is the pinned 3D physics release for this revision | [Jolt 5.6.0](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0) |
| Qt input/focus | Focused widgets receive key press/release events and focus loss must be handled explicitly | [Qt `QWidget`](https://doc.qt.io/qt-6/qwidget.html), [Qt `QKeyEvent`](https://doc.qt.io/qt-6/qkeyevent.html) |
| MonoGame input | Framework input APIs expose polled keyboard state, but the embedded Qt surface still owns visible focus | [MonoGame `Keyboard`](https://docs.monogame.net/api/Microsoft.Xna.Framework.Input.Keyboard.html), [input management](https://docs.monogame.net/articles/tutorials/building_2d_games/11_input_management/) |
| Qt mouse input | Widget mouse events expose button, position, motion, and wheel deltas while the focused Game view remains capture owner | [Qt `QMouseEvent`](https://doc.qt.io/qt-6/qmouseevent.html), [Qt `QWheelEvent`](https://doc.qt.io/qt-6/qwheelevent.html) |
| Standard gamepads | SDL 3 exposes positional gamepad buttons/axes, enumeration, hot plugging, and explicit polling across supported desktop platforms | [SDL gamepad API](https://wiki.libsdl.org/SDL3/CategoryGamepad), [`SDL_GetGamepads`](https://wiki.libsdl.org/SDL3/SDL_GetGamepads), [`SDL_UpdateGamepads`](https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads) |
| SDL licensing | SDL uses the zlib license and must be carried into dependency/license inventories and redistributed bundles | [SDL license](https://www.libsdl.org/license.php) |
| C# generation/loading | Roslyn supports compile-time generation; dynamic loading belongs in isolated `AssemblyLoadContext` consumers, not the editor | [Roslyn SDK](https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/), [`AssemblyLoadContext`](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext) |
| Relocatable CMake installation | Current CMake guidance recommends relative install destinations and supports runtime-dependency installation across Windows, Linux, and macOS | [CMake `install()`](https://cmake.org/cmake/help/latest/command/install.html) |
| Windows package integrity | Windows package signing uses SignTool and a valid signing certificate; verification remains part of the clean-package gate | [Microsoft package deployment](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/), [SignTool package signing](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool) |
| macOS direct distribution | Directly distributed macOS software uses distribution signing and the Apple notarization workflow; Dragon Pixel records signing/notarization as POC R evidence | [Distribution signing](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/), [notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) |
| Unity AOT boundary | IL2CPP is Unity's ahead-of-time scripting backend, so the bridge prototype requires generated/AOT-safe behavior and link-preservation evidence | [Unity 6.3 IL2CPP](https://docs.unity3d.com/6000.3/Documentation/Manual/il2cpp-introduction.html) |
| Unity Tilemap workflow | Current Unity documentation separates Grid/Tilemap targets, durable Tile Palettes, active targets, brush tools/inspection, grid selection, slicing, collider modes, and the optional Extras tile/brush package | [Create Tilemap](https://docs.unity3d.com/kr/current/Manual/tilemaps/work-with-tilemaps/create-tilemap.html), [Create Tile Palette](https://docs.unity3d.com/kr/current/Manual/tilemaps/tile-palettes/create-tile-palette.html), [Tile Palette reference](https://docs.unity3d.com/cn/2023.2/Manual/tile-palette-ui-ref.html), [Grid Selection](https://docs.unity3d.com/cn/2023.2/Manual/tile-palette-grid-selection.html), [Sprite slicing](https://docs.unity3d.com/kr/6000.0/Manual/sprite/sprite-editor/automatic-slicing.html), [Tilemap Collider 2D](https://docs.unity3d.com/6000.0/Documentation/Manual/tilemaps/work-with-tilemaps/tilemap-collider-2d-reference.html), [2D Tilemap Extras](https://docs.unity3d.com/cn/6000.0/Manual/com.unity.2d.tilemap.extras.html) |

Additional primary specifications for the selected durable boundaries are JSON-RPC 2.0, RFC 8259 JSON, RFC 9562 UUIDs, and Khronos glTF 2.0. The expanded source list is maintained in the design document.

## Current Generated Result

### Result identity

- **Result:** `RESULT-DPE-ARCH-0015`
- **Design:** `DPE-ARCH-0015`
- **Disposition:** Accepted the complete 2D Tilemap Editor expansion on active draft PR #6: versioned TileSet/palette/Tilemap/snapshot contracts, five layouts, multi-TileSet typed tiles/brushes, full slicing/palette/selection workflows, framework-neutral renderer/collision behavior, worker-only native tile extensions, and expanded Tiled JSON conversion. Existing command/asset/recovery ownership, support thresholds, POC/ADR gates, worker isolation, original presentation, and experimental KNI status remain unchanged.

### Implementation evidence result

The current Windows 11 x64 worktree passes all 45 registered strict Release tests; the persisted CTest log contains 45 pass markers, zero failures, and 294.56 seconds of recorded test execution. The refreshed Windows MSVC AddressSanitizer build passes 45/45 in 368.80 seconds; its duplicate full Qt interaction registrations complete in 95.78 and 91.47 seconds under the sanitizer-only 180-second harness cap. The most recent Ubuntu 24.04 x64 evidence predates DPE-ARCH-0008 and passes all 36 then-registered strict Release tests in 102.20 seconds and all 36 then-registered Clang AddressSanitizer tests in 101.97 seconds. Current POC I/J and project-index/prefab/transaction hardening have Windows evidence only. POCs M-S have no complete implementation evidence.

The current combined POC B and independent POC E measurements are:

| Platform and run | Adapter | Presented FPS | Median viewport-command-to-present | Control response | Crash recovery | Device/readback | Publish |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows combined POC B | MonoGame | 64.1 | 15.4 ms | 566.3 ms | 933.0 ms | 2.5 ms | 0.1 ms |
| Windows combined POC B | KNI | 32.1 | 32.4 ms | 253.2 ms | 1213.4 ms | 22.6 ms | 0.1 ms |
| Windows independent POC E | MonoGame | 64.0 | 15.5 ms | 601.7 ms | 1084.2 ms | 2.9 ms | 0.2 ms |
| Windows independent POC E | KNI | 32.0 | 31.0 ms | 245.2 ms | 778.4 ms | 22.2 ms | 0.1 ms |
| Ubuntu combined POC B | MonoGame | 61.7 | 16.3 ms | 306.2 ms | 492.1 ms | 8.5 ms | 0.2 ms |
| Ubuntu combined POC B | KNI | 32.3 | 55.1 ms | 315.1 ms | 534.4 ms | 26.6 ms | 0.2 ms |
| Ubuntu independent POC E | MonoGame | 61.6 | 16.4 ms | 349.1 ms | 487.6 ms | 6.8 ms | 0.1 ms |
| Ubuntu independent POC E | KNI | 33.3 | 55.2 ms | 353.1 ms | 544.7 ms | 28.0 ms | 0.1 ms |

These values remain classified as viewport-command-to-present evidence. Separately, Windows POC J now forwards immutable full-state Game-view snapshots with focus/capture and edge counters, validates them transactionally, neutralizes on every lifecycle boundary, fails closed through a Play-worker restart on rejected state, and proves `InputMotion2D` changes actual MonoGame/KNI pixels and retained-ID pick location without changing authoring JSON. The remaining latency gate is the aggregate real Qt key-event-to-first-reflecting-Qt-paint median/tail measurement. Control response and crash recovery remain lifecycle timings; MonoGame and KNI continue to run independently.

The implemented foundation includes portable C++ core, scene, metadata, serialization, command/transaction, and C ABI modules; `.NET Standard 2.1` contracts; .NET 10 interop and worker packages; deterministic scene and metadata format-version-2 schemas; UUID component identities; entity/component enabled state; canonical project/scene/asset filenames; separate MonoGame and KNI adapters; authenticated named-pipe/Unix-socket JSON-RPC control; shared-memory BGRA8 frames; and a Qt 6.11 Widgets editor with Scene, Hierarchy, Project/Assets, Inspector, and Console docks.

The foundational Windows and historical Ubuntu editor tests load the same sample, reject project-root traversal, execute an atomic multi-command edit, save and fully close/reopen a project, expose native/managed metadata plus read-only opaque diagnostics, display 2D and 3D content, supervise distinct preview and play processes, preserve authoring data across play/stop, and recover from a forced play-worker crash without losing preview. They also run an external Python client through capability negotiation, inspection, dry-run, validated mutation, rejection, cancellation, and token-redacted audit paths. Windows exercises authenticated named pipes and Ubuntu exercises Unix-domain sockets. Direct framework probes create real MonoGame and KNI graphics devices, render a sprite and cube to a render target, and verify readback content.

Current Windows hardening validates the originally selected project location before recovery, preserves the active session on candidate failure, applies one portable path/alias registry to roots, documents, and asset sources, and requires `startupScene` to lie under a declared scene root. Native multi-file saves use a SHA-256-bound crash journal with rollback/startup recovery under hostile fixtures. Prefabs preserve missing/newer/incompatible fallbacks and reject cycles/depth/entity explosions. Component generation is rooted and build-identity-addressed; workers require exact manifests, placement/tool/contracts/artifact hashes, contain initialization/property/lifecycle-stage failures, publish factories atomically, expose immutable input, dispatch the optional managed/native-v2 full lifespan, and preserve native v1 compatibility. The Qt editor now uses the entire nested/tabbed/floating dock grid and directly attaches generated scripts/components to selected GameObjects through validated commands.

DPE-ARCH-0011 Windows implementation indexes stable contained C#/C++ source/header and component-metadata files in Project Explorer, filters them as Scripts / Components, and refreshes immediately after generation. `ScriptEditorService` regenerates `.dragonpixel/Ide/Rider/DragonPixel.ProjectComponents.sln` plus its project atomically, references `DragonPixel.Contracts`, includes all validated sources/manifests, rejects missing/outside/linked inputs, discovers the local Rider executable, and routes the solution plus selected `--line 1` file through detached argument-vector launches. Qt automation injects the launcher, so it proves Project Explorer and Inspector actions without opening Rider. The five focused aliases pass Release in 83.89 seconds and MSVC AddressSanitizer in 104.67 seconds. This is Windows-only partial POC I evidence.

DPE-ARCH-0012 Windows implementation adds `IGameObjectControllerLifecycle`, `GameObjectController`, `GameObjectInput`, and the shared `Transform`; preserves the legacy runtime interfaces; binds stable GUID identity; and overlays only dirty managed transforms onto the disposable render/pick scene after physics. Generated scripts and the user's exact `MyMover` read `move.x`/`move.y`, normalize diagonal movement, parse Inspector Speed, and translate by delta time. Inspector no longer renders lifecycle as serialized data. Focused Release runtime/generator/UI evidence passes, with the registered UI run at 83.13 seconds; the matching five MSVC AddressSanitizer aliases pass 5/5 in 143.15 seconds. The exact writable project builds with zero warnings/errors, the refreshed 157-entry developer bundle hash-verifies, and its packaged MonoGame self-test exits zero. A newly observed locked-Rider publication failure was repaired without terminating Rider: default bundle refresh now preserves the writable sample and updates engine/runtime payloads in place, while `-ResetSample` remains explicit. Actual-device managed-controller pixel/pick evidence, full Stop/crash lifecycle coverage, native transform parity, packaging, and Ubuntu/macOS remain open.

DPE-ARCH-0013 Windows implementation replaces the Game view's hard-coded binding table with a project-owned `dpe.inputmap` v1 asset, one validated `InputMapService`, Qt keyboard/mouse capture, an SDL 3.4.12 standard-gamepad adapter, compatibility fallback, and an Input Map editor for named maps, actions, bindings, and persistent rebinding. The sample map drives both `InputMotion2D` and `MyMover` through the existing `move.x`/`move.y` action protocol and also defines representative jump, look, and fire actions. Five focused Release aliases pass 5/5 in 94.67 seconds and the matching MSVC AddressSanitizer aliases pass 5/5 in 116.79 seconds, covering deterministic preservation, validation, containment, atomic conflict-safe saves, custom keyboard/mouse/gamepad evaluation, analog dead zones, transient and lifecycle neutralization, project indexing/loading, and dialog interaction. The refreshed 179-entry production bundle hash-verifies and passes the packaged MonoGame self-test while preserving the user's `MyMover.cs`; its editor, manifest, default-map, mover, and SDL hashes are recorded in the active plan. Physical-controller/hot-plug evidence, aggregate Qt input-to-paint timing, current Ubuntu/macOS runs, and complete POC J runtime device-pixel/pick correlation remain open.

The 2026-07-26 Inspector/input-settings follow-up remains within DPE-ARCH-0012/0013. It adds embedded high-contrast checked/mixed Inspector indicators; an explicit **Edit > Project Settings > Input...** route; control-map enabled state, action rename, and full binding path/scale/dead-zone editing; and generated/sample C# movers with editable Horizontal Action, Vertical Action, and Speed fields matching `Input Motion 2D`. C# creation schedules an isolated component build automatically, and the sample no longer writes movement values every frame. A new exact-source .NET test compiles and executes the canonical `MyMover` with custom action names and proves GUID/lifecycle/Transform/reset behavior. Ten focused Release aliases pass 10/10 in 133.55 seconds and their MSVC AddressSanitizer counterparts pass 10/10 in 157.17 seconds; final UI reruns pass in 87.61/108.35 seconds. The writable project builds with zero warnings/errors, the packaged MonoGame self-test passes, and all 183 bundle records verify. Physical-gamepad, aggregate Qt input-to-paint, current Ubuntu/macOS, and remaining POC J gates stay open.

DPE-ARCH-0015 is an accepted implementation contract, not completed evidence. At selection time, draft PR #6 already passes its recorded Windows static-orthogonal Tilemap workflow and atomic save correction, but TileSet v2, TilePalette v1, Tilemap v2, snapshot v5, non-orthogonal layouts, typed tiles/brushes, extension ABI, and broader Tiled conversion have not yet been implemented or verified. The active mirrored plan owns incremental results. No pre-existing Windows, hosted-platform, POC, ADR, slice, release, or KNI claim is promoted by accepting the expansion.

Current manual Windows QA opens a writable disposable sample under `out/dev`, uses the typed Inspector and indexed Project Explorer with thumbnails/import/dependency/structural status, creates a preset as one undoable transaction, runs isolated Simulate, displays actual MonoGame and KNI preview/play output, and exercises pause and stop. DPE-ARCH-0011 additionally requires Project Explorer component-source visibility and a contained detached Rider handoff from Inspector/Project actions; its focused implementation evidence is recorded in the active plan. This useful Windows workflow evidence does not complete the full 2D/3D designer, linked nested-prefab, source-authoring cross-platform, or accessibility acceptance matrices.

macOS 14+ arm64 remains the last unclosed platform. Its prior result is 14 of 15 tests, and `poc_b.worker_viewport` fails at 1280×720 because end-to-end presented throughput is below 30 FPS. There is no current macOS POCs E-H or AddressSanitizer rerun. Consequently Slice 1 is not accepted. Slice 2 also remains unaccepted because the current macOS gate and the complete designer, linked-prefab, and accessibility scenarios are unfinished. KNI remains experimental; passing Windows and Ubuntu evidence is not its required three-platform support matrix.

### Product and scope result

Dragon Pixel Engine is a C++20/Qt standalone editor and native engine core for Windows, macOS, and Linux. It provides first-class 2D and 3D authoring, one language-neutral entity/component model, C# and C++ component implementations, and managed runtime adapters for MonoGame and KNI. Unity is limited to AOT-safe `.NET Standard 2.1` contracts and a small bridge prototype; Unreal implementation, broad Unity parity, automatic semantic migration, unrestricted embedded Python, and AAA rendering are excluded from 1.0.

Version 1.0 is defined by a stable end-to-end project workflow, not raw feature count. A user must be able to create or migrate a project, author scenes visually, run and debug it, save/recover it safely, build/package it, and upgrade it across supported versions.

### Full version 1.0 delivery boundary result

The active plan now covers all four slices while preserving their acceptance order. Slice 3 introduces `IProjectLifecycleService`, `IMigrationService`, `IBuildService`, `IPackageService`, `IPluginService`, `IUpdateService`, and `ISupportBundleService`, plus project-v4, template-v1, asset-v3, migration-plan-v1, plugin-v1, build-result-v1, release/update-v1, and support-bundle-v1 contracts. Each filesystem/process workflow uses dry-run validation, resolved containment, isolated staging, cancellation, structured diagnostics, audit correlation, and recoverable commit/rollback boundaries.

Built-in 2D/3D templates are declarative and non-executable. Migration preserves the original MonoGame/KNI tree and commits only to a new sibling destination. Importers, project builds, plugins, migration evaluation, and packagers run out of process. Installed resources are relocatable, packages carry dependency/license/provenance identities, and updates are applied by an external helper using verified side-by-side staging and last-known-good rollback.

Unity remains a `.NET Standard 2.1` contracts-and-bridge prototype tested through Mono and IL2CPP-compatible paths; it is not a supported full integration. Crash/support evidence is local and opt-in by default, previewed and redacted before export, with no default telemetry channel. The `1.0.0` version, compatibility policy, performance/memory budgets, clean packages, security/license/privacy/accessibility reviews, reference projects, and documentation-reproduction evidence are release gates rather than implementation aspirations.

### Architecture result

The Qt editor owns authoritative authoring state and child worker processes. A preview worker mirrors edit state for the viewport. A separate play worker starts from an immutable snapshot and is discarded on Stop, preventing runtime changes or crashes from corrupting scenes. The editor does not host normal project .NET code in-process. Worker restarts are the first-release hot-reload mechanism.

Portable C++ modules contain identity, scenes, metadata, commands, serialization, asset contracts, and diagnostics. They have no Qt, .NET, MonoGame, KNI, Unity, or Python dependencies. Qt appears only in editor modules. Managed packages split portable `DragonPixel.Contracts` (`netstandard2.1`) from `.NET 10` interop/runtime/framework packages. MonoGame and KNI live in separate adapters behind a shared internal lifecycle/capability contract.

Editor/worker/tool control traffic uses length-prefixed JSON-RPC 2.0 over user-restricted named pipes on Windows and Unix-domain sockets on macOS/Linux. Viewport bytes use a separate, replaceable shared-memory frame transport. Slice 1 begins with BGRA8 CPU frames; D3D, IOSurface/Metal, or Vulkan external-resource transports can be added only after measurement.

Managed workers call the native runtime through `dpe_api_v1`, a stable C function table using opaque handles, fixed-width values, explicit UTF-8 buffers, paired allocation, status/error records, and version/capability negotiation. Exceptions, C++ classes/STL values, GC objects, owning raw pointers, and framework objects never cross the ABI.

### Entity, component, and metadata result

The engine uses a hybrid model: an editor-friendly object graph and durable component records are authoritative; hot runtime systems may compile records into dense data-oriented stores internally. A world owns scenes, a scene owns entities, and an entity has a persistent UUID, name, hierarchy position, enabled state, and ordered component records. A component record has a stable type UUID, diagnostic name, schema version, runtime owner, enabled state, and JSON properties.

C++ and C# component factories resolve the same type IDs. Native storage owns native instances; the managed worker owns C# instances. Lifecycle calls are batched by runtime/type and use IDs/handles rather than cross-language inheritance. C# source generation and C++ build-time registration emit the same metadata manifest schema for Inspector properties. The editor reads the manifests without loading arbitrary game code.

The canonical space is right-handed, Y-up, negative-Z-forward, meter/second based, with radians internally. 2D uses X/Y plus Z depth. Adapters perform tested coordinate/matrix conversion.

### Serialization and editor result

Authoritative project, scene, prefab, asset, workspace, and migration data use deterministic UTF-8 JSON with document and component schema versions. Import caches are disposable. Unknown or newer components remain opaque, visible, and structurally preserved during load/save. Renames retain stable IDs; true schema changes use explicit ordered migrations. Saves validate first, write and flush a sibling temporary file, atomically replace the target where possible, and retain bounded recovery data.

The current implemented authoring expansion includes project v3, scene v3, prefab v1, asset v2, component metadata v4, TileSet/Tilemap v1, and runtime snapshot v4 paths within the registered Windows scope. DPE-ARCH-0009 accepts explicit planned migrations to project v4 and asset v3 plus new template, migration-plan, plugin, build-result, release/update, and support-bundle version-1 contracts. DPE-ARCH-0015 accepts TileSet v2, TilePalette v1, Tilemap v2, and runtime snapshot v5 while retaining explicit older-version readers. Known and opaque data must remain lossless through every supported migration; newer/incompatible documents are preserved read-only or rejected without silent rewrite.

The Qt shell uses dockable Scene, Hierarchy, Project/Assets, Inspector, and Console panels backed by project, scene, selection, command, metadata, asset, runtime-session, and diagnostics services. All mutations are commands/transactions. Undo/redo, panels, migration, plugins, Python, and AI use the same validation path. Standard Qt controls are preferred for accessibility; custom viewport/gizmo controls must expose keyboard and assistive behavior.

### Framework and rendering result

The managed framework contract covers capabilities, initialization, snapshot loading, fixed/variable update, real 2D/3D render submission, revision-correlated frame production, pause/resume/stop, resize, picking, diagnostics, and shutdown. MonoGame is the primary adapter. KNI uses the same conformance suite but remains experimental unless `.NET 10` and every desktop target pass; the engine baseline will not be silently downgraded. Acceptance requires actual framework graphics devices and scene-dependent readback; synthetic diagnostic fixtures cannot satisfy the rendering gate.

Portable rendering data includes cameras, transforms, sprites, meshes, materials, lights, render layers, and asset handles. Framework-specific objects remain within adapters, and optional features are declared as capabilities. glTF 2.0 is the preferred portable 3D interchange format.

### Migration result

Migration begins with an out-of-process read-only scan. MSBuild project evaluation, Roslyn analysis, and content parsing identify projects, TFMs, packages, game-loop evidence, assets, processors, platform conditions, native dependencies, and risks. The scanner emits JSON plus Markdown with evidence, confidence, and `reuse`/`adapt`/`manual`/`unsupported`/`unknown` classifications.

Generation defaults to a new sibling Dragon Pixel project and staging area. It never rewrites or deletes original code/assets. Static discovery, inventories, common loop patterns, known content, and initial metadata can be automated. Gameplay intent, dynamic/reflection behavior, custom processors, native/platform services, and behavioral equivalence remain assisted.

### Python, AI, and plugin result

Python/AI tools are external clients of a capability-based automation broker. They receive read-only project access by default, write only to isolated staging, and propose validated editor commands. Every operation supports progress, cancellation, timeouts, and hard worker termination. JSONL audit events record tool/model identity, versions, capability grants, hashes, parameters, approvals, results, and diagnostics. Reproducible tools pin Python/dependencies and input/output hashes; AI output is always an untrusted proposal.

Slice 1 now includes the initial external Python boundary: a dependency-free client and user-restricted editor broker negotiate protocol/capabilities, inspect a scene, dry-run and apply the permitted rename command through editor validation, reject invalid commands, exercise cancellation, and write capability-specific JSONL audit records without persisting the inherited session token. Broader staging, approvals, progress, build/import, and AI workflows remain later-slice expansions of this boundary.

Plugins have versioned manifests, explicit runtime type, platform/architecture support, dependencies, contributions, and requested permissions. Native/managed project plugins load in workers. Declarative editor extensions are preferred. Native Qt editor plugins are trusted, exact-version-bound, restart-required, and do not receive a stable cross-version C++ ABI promise. All plugins mutate through commands.

### Roadmap result

- **Slice 1:** foundational ADRs/POCs, cross-platform builds, core identity/scene/metadata/serialization/commands, C ABI, workers, MonoGame/KNI adapters, Qt shell/panels, basic 2D/3D viewport, and edit/play/pause/stop.
- **Slice 2:** service-backed 2D/3D/Debug workspaces; typed and validated multi-selection Inspector; browse/open/assign asset workflows and previews; drag/drop; a unified 2D/3D Scene View with picking and gizmos; command-based undo/redo; linked nested prefabs; custom drawers; baseline lighting; live Box2D/Jolt physics; structured Console navigation; and real Qt interaction/accessibility evidence.
- **Slice 3:** project-v4/templates, creation/discovery/settings/upgrades/archive/restore, asset-v3 import/cache integrity, read-only migration and reversible sibling generation, SDK/dependency checks, declared worker builds, relocatable packaging, plugin/update management, and the bounded Unity bridge prototype.
- **Slice 4:** one version source, frozen performance/memory budgets, crash/data integrity and privacy-safe support bundles, security/license review, accessibility, public compatibility policy, clean install/update/rollback/uninstall, long-running projects, real migrations, documentation/reference-project reproduction, and final 1.0 release gates.

Current roadmap evidence (2026-07-25): the Windows strict Release and MSVC AddressSanitizer matrices both pass 45/45; ASan completes in 368.80 seconds. The worker uses a 64 Hz absolute pacing lattice and the final 1280×720 combined POC B run measures MonoGame at 64.1 FPS and KNI at 32.1 FPS without changing the ≥30 FPS gate. Windows now automates full-state input with real pixel/pick displacement, hardened project/component paths, SHA-bound transaction recovery, and stronger prefab fallback/guards in addition to the prior editor/tile/multi-view scope. The prior Ubuntu worktree passes its pre-DPE-ARCH-0008 36-test Release/Clang-ASan matrices; current Ubuntu/macOS expanded evidence is absent. Slice 1 remains open because macOS POC B/current platform matrices and aggregate POC J Qt-paint latency are unresolved. Slice 2 remains open because POCs I/K/L across Ubuntu/macOS, full POCs J-L, designer/service/accessibility/distribution work, and several hardening races remain unfinished. Slices 3/4 and POCs M-S remain open.

### Risk and prototype result

The critical risks are cross-process frame transport, mixed-language ownership, KNI compatibility, Qt LGPL compliance, metadata drift, unknown-data loss, edit/play leakage, unsafe migration, plugin/AI mutation, 2D+3D scope, and late platform divergence.

The original four POCs gate the foundation:

1. Native/managed ABI ownership/error testing on all desktop baselines.
2. Qt/worker/shared-frame lifecycle with a sprite and lit mesh through MonoGame and KNI.
3. Cross-language metadata plus scene and unknown-component round-tripping.
4. A read-only MonoGame/KNI scanner whose source-tree hashes do not change.

Historical Windows and Ubuntu evidence recorded on 2026-07-24 passes all four foundational POCs in their then-current Release and native AddressSanitizer configurations. POC B additionally passed direct real-device MonoGame and KNI render-target/readback probes with a normal device status: Windows observed 5,608 distinct colors per adapter and Ubuntu observed 5,589. The recorded Ubuntu shared-frame run measured 54.9 FPS for MonoGame and 54.8 FPS for KNI; its crash-recovery timing is lifecycle evidence, not correlated input-to-present latency. POC D proves its inspected source trees remained unchanged. Equivalent macOS runs and broader real-project scanner fixtures remain required.

DPE-ARCH-0006 adds four focused gates:

5. POC E: scene-driven real MonoGame/KNI rendering, readback, picking, resize, revisions, and unchanged throughput/latency criteria.
6. POC F: three-level linked prefab nesting, stable mappings, overrides, apply/revert/unpack, missing/newer-source recovery, cycles, and deterministic migration.
7. POC G: Box2D/Jolt world ownership, fixed stepping, ABI batches, events/queries, transform mapping, determinism policy, and crash cleanup.
8. POC H: real Qt Test interaction through menus, dialogs, inline editors, drag/drop, keyboard focus, Scene View actions, workspaces, and accessibility.

DPE-ARCH-0007 adds two focused gates:

9. POC I: metadata-v4/project-v3 component discovery, C#/C++ source and generation parity, worker-only module execution, editor-process exclusion, lifecycle ownership, and lossless unavailable-module behavior.
10. POC J: real embedded Play input capture, neutralization, worker consumption, actual MonoGame/KNI pixel/pick changes, and honest correlated latency.

DPE-ARCH-0008 adds two focused gates:

11. POC K: deterministic TileSet/Tilemap formats, contained PNG slicing, public Qt tile tools, save/recovery, adapter pixels/picking, and Box2D collision.
12. POC L: simultaneous named Scene/Game outputs, unique-primary diagnostics, independent resizing/revisions, Play isolation, input ownership, and crash recovery.

DPE-ARCH-0009 adds seven full-lifecycle and release gates:

13. POC M: declarative project templates, creation, discovery/settings, project-v4 upgrades, archive/restore, containment, and injected recovery.
14. POC N: read-only representative MonoGame/KNI migration, explicit decisions, sibling generation, verification, source-tree integrity, and rollback.
15. POC O: copied/linked/generated asset ownership, importer isolation, stable sidecars/cache keys, filesystem safety, recovery, and adapter consumption.
16. POC P: plugin manifests/integrity/permissions, dependency conflicts, transactional lifecycle, worker isolation, quarantine, compatibility, and licensing.
17. POC Q: Unity 6.3 `.NET Standard 2.1` bridge through Mono and IL2CPP-compatible paths with AOT/link preservation and portable-contract purity.
18. POC R: relocatable package/install/update/rollback/uninstall, signing/notarization, provenance, notices, Qt materials, and clean-machine verification.
19. POC S: frozen performance/memory budgets, long-running and corruption recovery, compatibility fixtures, security/privacy, accessibility, and documentation-driven release qualification.

Current POCs E-S status (2026-07-25): registered POCs E-H pass on Windows and historical Ubuntu. Current independent Windows POC E measures MonoGame at 64.0 FPS/15.5 ms viewport-command-to-present and KNI at 32.0 FPS/31.0 ms; the older Ubuntu run measures MonoGame at 61.6 FPS/16.4 ms and KNI at 33.3 FPS/55.2 ms. Windows additionally passes the current Release and MSVC-ASan registrations for POC I generation/cache/runtime modules and partial POC J full-state/pixel/pick/recovery behavior. This does not close any three-platform gate or the POC J Qt-paint latency gate. No current macOS POCs E-H or sanitizer result exists; Ubuntu/macOS have not run the expanded POCs I/K/L suite; full POCs J-L and the linked-prefab designer, keyboard, and accessibility matrices remain unfinished; and POCs M-S are unimplemented. Cross-platform divergence, multi-output topology, tile adapter/collision acceptance, linked-prefab usability, lifecycle/package/update/plugin/Unity/release qualification, and accessibility remain active risks; KNI remains experimental.

The first post-POC implementation is `S1.0 Architecture Bootstrap`: build scaffolding, native core/scene/metadata/serialization/C ABI modules, portable contracts/interops/headless worker, one native and one managed component, one world/scene, JSON round-trip, schemas, and tests. It explicitly excludes the Qt shell and general renderer.

S1.0 and the subsequent editor/runtime vertical slice are implemented on Windows and on the prior Ubuntu baseline. Windows now passes the expanded 45-test strict Release and 45-test MSVC AddressSanitizer matrices; Ubuntu's last evidence is the pre-DPE-ARCH-0008 36-test matrices. DPE-ARCH-0009 authorizes gate-preserving completion of Slices 1-4 and the design-defined `1.0.0`. Slice 1 remains open until the macOS POC B defect, POC J aggregate Qt-paint latency, and current Ubuntu/macOS expanded/AddressSanitizer evidence are resolved. Slice 2 remains open until current Ubuntu/macOS coverage and the complete 2D/3D, linked-prefab, physics, input, tiles, multi-view, designer, accessibility, and distribution workflows pass without JSON editing. Slices 3/4 remain open until POCs M-S and every lifecycle/release acceptance statement pass.

The authoritative design document contains the exact ownership rules, module tables, file conventions, editor/lifecycle/release services, risk register, prototype metrics, 23 ADRs, 19 POCs, four-slice acceptance criteria, and `S1.0` definition of done for this result.

## Revision History

| Result revision | Design revision | Date | Change |
| --- | --- | --- | --- |
| `RESULT-DPE-ARCH-0001` | `DPE-ARCH-0001` | 2026-07-24 | Initial researched architecture result; established living-document governance and bounded the first prototype/implementation iteration |
| `RESULT-DPE-ARCH-0002` | `DPE-ARCH-0002` | 2026-07-24 | Added repository/document-system Markdown mirrors and made durable documentation, plans, and substantive response capture mandatory |
| `RESULT-DPE-ARCH-0003` | `DPE-ARCH-0003` | 2026-07-24 | Recorded passing Windows POC, S1.0, direct framework graphics, and Qt editor/worker evidence without changing the architecture; retained macOS/Linux and KNI support gates |
| `RESULT-DPE-ARCH-0004` | `DPE-ARCH-0004` | 2026-07-24 | Added passing Ubuntu 24.04 Release/Clang-ASan, Unix-socket, shared-frame, graphics, and editor evidence; retained macOS and KNI support gates |
| `RESULT-DPE-ARCH-0005` | `DPE-ARCH-0005` | 2026-07-24 | Recorded scene-v2 and stable-ID conformance, transactions, canonical project lifecycle, distinct preview/play supervision, opaque Inspector diagnostics, initial external Python automation, and the then-current passing 15-test Windows/Ubuntu Release/ASan matrices; retained macOS and KNI support gates |
| `RESULT-DPE-ARCH-0006` | `DPE-ARCH-0006` | 2026-07-24 | Accepted the work scope for Slice 1 closure plus complete Slice 2; recorded macOS 14/15 and POC B measurement defects; added real rendering/picking, command/editor services, format revisions, Box2D/Jolt physics, linked nested prefabs, Qt interaction/accessibility, and POCs E-H |
| `RESULT-DPE-ARCH-0007` | `DPE-ARCH-0007` | 2026-07-25 | Accepted the Unity-familiar Inspector/custom-component/embedded-input increment; corrected unsafe mixed editing and the revision-echo latency claim; added project-v3/metadata-v4 boundaries and POCs I-J |
| `RESULT-DPE-ARCH-0008` | `DPE-ARCH-0008` | 2026-07-25 | Accepted structured object/interface drawers, explicit worker-executed project components, named Scene/Game outputs, orthogonal tile formats/tools/runtime lowering, and POCs K-L; implemented the registered Windows scope and recorded final 42/42 Release plus 42/42 MSVC-ASan evidence while retaining all three-platform gates |
| `RESULT-DPE-ARCH-0009` | `DPE-ARCH-0009` | 2026-07-25 | Activated gate-preserving completion of Slices 1-4 and full 1.0; added project-v4 lifecycle/templates, safe import/migration, build/package/update/plugin/Unity/release contracts, ADRs through 0023, POCs M-S, fresh Windows 42/42 Release plus 42/42 MSVC-ASan evidence, and explicit prohibition on a 1.0 claim before all gates pass |
| `RESULT-DPE-ARCH-0010` | `DPE-ARCH-0010` | 2026-07-26 | Accepted the fluid full-grid dock workspace, direct generated-script/component attachment, backward-compatible managed lifecycle extension, size-tagged native component ABI v2 with v1 fallback, bounded fixed dispatch, and stage-specific cleanup without changing isolation or slice gates |
| `RESULT-DPE-ARCH-0011` | `DPE-ARCH-0011` | 2026-07-26 | Accepted contained component-source indexing, disposable Rider solution generation, Inspector/Project edit actions, deterministic detached Rider launch, and continued worker-only project-code execution |
| `RESULT-DPE-ARCH-0012` | `DPE-ARCH-0012` | 2026-07-26 | Accepted the backward-compatible managed `GameObjectController` lifecycle surface, stable GUID, shared worker-owned `Vector3` Transform, input/time access, disposable runtime render/pick overlays, generated mover behavior, and Inspector lifecycle-row removal |
| `RESULT-DPE-ARCH-0013` | `DPE-ARCH-0013` | 2026-07-26 | Accepted project-owned configurable input maps, named control maps, persistent rebinding, Qt keyboard/mouse capture, SDL 3 standard-gamepad support, and shared device-neutral movement actions for `InputMotion2D` and `MyMover` |
| `RESULT-DPE-ARCH-0014` | `DPE-ARCH-0014` | 2026-07-27 | Accepted the Project Hub and minimal templates/clean scenes, project-v4/template-v1 implementation, asset-v3 and recoverable two-pane Project Browser operations, immutable imported-image bindings, versioned drag/drop, ordered Hierarchy multi-operations, and multiple independently lockable Inspectors |
| `RESULT-DPE-ARCH-0015` | `DPE-ARCH-0015` | 2026-07-28 | Accepted the complete 2D Tilemap Editor expansion with versioned TileSet/palette/Tilemap/snapshot contracts, five layouts, multi-TileSet typed tiles/brushes, full slicing/palette/selection workflows, runtime rendering/collision, isolated native extensions, broader Tiled JSON conversion, and unchanged evidence gates |
