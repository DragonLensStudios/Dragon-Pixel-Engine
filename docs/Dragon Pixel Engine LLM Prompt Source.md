# Dragon Pixel Engine LLM Prompt Source

> **Document role:** Prompt and generated-result provenance  
> **Design revision:** `DPE-ARCH-0002`  
> **Result revision:** `RESULT-DPE-ARCH-0002`  
> **Last reviewed:** 2026-07-24  
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
| Dragon Pixel Engine repository | Repository state inspected before research | Git commit `b48360e59f55fd827d9513405c9fef42178b92be` |
| `Dragon Pixel Engine Design Document.md` | Expanded authoritative result for this revision | SHA-256 `1277F66438EF9632A4AB4B46EBD861C4CB0F994DB2575B227AD71D9FE866B1D5` |

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

Additional primary specifications for the selected durable boundaries are JSON-RPC 2.0, RFC 8259 JSON, RFC 9562 UUIDs, and Khronos glTF 2.0. The expanded source list is maintained in the design document.

## Current Generated Result

### Result identity

- **Result:** `RESULT-DPE-ARCH-0002`
- **Design:** `DPE-ARCH-0002`
- **Disposition:** Accepted as the architecture baseline for POCs and `S1.0`; no engine implementation was generated.

### Product and scope result

Dragon Pixel Engine is a C++20/Qt standalone editor and native engine core for Windows, macOS, and Linux. It provides first-class 2D and 3D authoring, one language-neutral entity/component model, C# and C++ component implementations, and managed runtime adapters for MonoGame and KNI. Unity is limited to AOT-safe `.NET Standard 2.1` contracts and a small bridge prototype; Unreal implementation, broad Unity parity, automatic semantic migration, unrestricted embedded Python, and AAA rendering are excluded from 1.0.

Version 1.0 is defined by a stable end-to-end project workflow, not raw feature count. A user must be able to create or migrate a project, author scenes visually, run and debug it, save/recover it safely, build/package it, and upgrade it across supported versions.

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

The Qt shell uses dockable Scene, Hierarchy, Project/Assets, Inspector, and Console panels backed by project, scene, selection, command, metadata, asset, runtime-session, and diagnostics services. All mutations are commands/transactions. Undo/redo, panels, migration, plugins, Python, and AI use the same validation path. Standard Qt controls are preferred for accessibility; custom viewport/gizmo controls must expose keyboard and assistive behavior.

### Framework and rendering result

The managed framework contract covers capabilities, initialization, snapshot loading, fixed/variable update, 2D/3D render submission, frame production, pause/resume/stop, diagnostics, and shutdown. MonoGame is the primary adapter. KNI uses the same conformance suite but remains experimental unless `.NET 10` and every desktop target pass; the engine baseline will not be silently downgraded.

Portable rendering data includes cameras, transforms, sprites, meshes, materials, lights, render layers, and asset handles. Framework-specific objects remain within adapters, and optional features are declared as capabilities. glTF 2.0 is the preferred portable 3D interchange format.

### Migration result

Migration begins with an out-of-process read-only scan. MSBuild project evaluation, Roslyn analysis, and content parsing identify projects, TFMs, packages, game-loop evidence, assets, processors, platform conditions, native dependencies, and risks. The scanner emits JSON plus Markdown with evidence, confidence, and `reuse`/`adapt`/`manual`/`unsupported`/`unknown` classifications.

Generation defaults to a new sibling Dragon Pixel project and staging area. It never rewrites or deletes original code/assets. Static discovery, inventories, common loop patterns, known content, and initial metadata can be automated. Gameplay intent, dynamic/reflection behavior, custom processors, native/platform services, and behavioral equivalence remain assisted.

### Python, AI, and plugin result

Python/AI tools are external clients of a capability-based automation broker. They receive read-only project access by default, write only to isolated staging, and propose validated editor commands. Every operation supports progress, cancellation, timeouts, and hard worker termination. JSONL audit events record tool/model identity, versions, capability grants, hashes, parameters, approvals, results, and diagnostics. Reproducible tools pin Python/dependencies and input/output hashes; AI output is always an untrusted proposal.

Plugins have versioned manifests, explicit runtime type, platform/architecture support, dependencies, contributions, and requested permissions. Native/managed project plugins load in workers. Declarative editor extensions are preferred. Native Qt editor plugins are trusted, exact-version-bound, restart-required, and do not receive a stable cross-version C++ ABI promise. All plugins mutate through commands.

### Roadmap result

- **Slice 1:** foundational ADRs/POCs, cross-platform builds, core identity/scene/metadata/serialization/commands, C ABI, workers, MonoGame/KNI adapters, Qt shell/panels, basic 2D/3D viewport, and edit/play/pause/stop.
- **Slice 2:** designer-ready layouts, Inspector controls, validation, drag/drop, asset previews, 2D/3D gizmos, multi-selection, undo/redo, prefabs, custom drawers, and baseline lighting/physics authoring.
- **Slice 3:** templates, project lifecycle, read-only migration and reversible generation, SDK/dependency checks, backups/recovery, packaging, plugin/update management, and the Unity bridge prototype.
- **Slice 4:** performance, crash/data integrity, security/license review, accessibility, compatibility policy, packaging/distribution, long-running projects, real migrations, documentation, and 1.0 release gates.

### Risk and prototype result

The critical risks are cross-process frame transport, mixed-language ownership, KNI compatibility, Qt LGPL compliance, metadata drift, unknown-data loss, edit/play leakage, unsafe migration, plugin/AI mutation, 2D+3D scope, and late platform divergence.

Four POCs gate development:

1. Native/managed ABI ownership/error testing on all desktop baselines.
2. Qt/worker/shared-frame lifecycle with a sprite and lit mesh through MonoGame and KNI.
3. Cross-language metadata plus scene and unknown-component round-tripping.
4. A read-only MonoGame/KNI scanner whose source-tree hashes do not change.

The first post-POC implementation is `S1.0 Architecture Bootstrap`: build scaffolding, native core/scene/metadata/serialization/C ABI modules, portable contracts/interops/headless worker, one native and one managed component, one world/scene, JSON round-trip, schemas, and tests. It explicitly excludes the Qt shell and general renderer.

The authoritative design document contains the exact ownership rules, module tables, file conventions, panel services, risk register, prototype metrics, 12 ADRs, slice acceptance criteria, and `S1.0` definition of done for this result.

## Revision History

| Result revision | Design revision | Date | Change |
| --- | --- | --- | --- |
| `RESULT-DPE-ARCH-0001` | `DPE-ARCH-0001` | 2026-07-24 | Initial researched architecture result; established living-document governance and bounded the first prototype/implementation iteration |
| `RESULT-DPE-ARCH-0002` | `DPE-ARCH-0002` | 2026-07-24 | Added repository/document-system Markdown mirrors and made durable documentation, plans, and substantive response capture mandatory |
