I want to use C:\Projects\Documentation\Engines\Dragon Pixel Engine

Design Document, and then the LLM prompt source with original prompt and result these 2 documents are living documents modified as changes are made always should be kept in a Agent.md file and used for all future development for this project.

I want to research and plan the architecture for a new game engine and editor called the Dragon Pixel Engine. Do not attempt to design or implement the entire engine at once. Begin with foundational architectural decisions and divide the work into small, practical milestones.

## Product vision

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
    

“Unity parity” refers primarily to familiar concepts and workflows, not to copying Unity’s proprietary implementation or visual assets.

## Technology direction

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
    

## Architectural principles

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

## Required modules to investigate

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
    

## Existing project migration

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

## Python and AI pipeline

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

## Development slices

Plan development in four major slices. Each slice must produce a usable and increasingly stable project workflow.

### Slice 1: Core, infrastructure, scaffolding, and core editor

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

### Slice 2: Designer-friendly tooling and UI

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

### Slice 3: Project lifecycle and maintenance

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

### Slice 4: Version 1.0 stabilization and polish

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

## Research questions

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
    

## Expected research output

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
