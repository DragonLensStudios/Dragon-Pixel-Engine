# ADR-0014: Project Components and Worker-Only Runtime Modules

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0007`

## Context

Dragon Pixel already stores language-neutral component records and exposes hard-coded native/managed descriptors, but the production editor cannot discover a project component manifest. The only C# generator is isolated in POC C, no C++ registration generator exists, and workers do not load project assemblies or native component modules. The sample managed Rotator is interpreted by a hard-coded type switch; it is not evidence that arbitrary project C# code executes.

The editor must feel component-oriented without putting untrusted or incompatible game code in the Qt process. A missing build output must not make its authoring data disappear or become destructive.

## Decision

- Project format version 3 declares contained `componentRoots`. Metadata format version 4 describes component category/policy, implementation language, contained source path, optional runtime module identity, stable type/schema identity, and the existing ordered property descriptors.
- The editor metadata service reads and validates only JSON manifests. It rejects unsafe paths, duplicate IDs, invalid defaults, unsupported schema versions, and owner/language mismatch before replacing its active registry. It never loads a project assembly or native library.
- **Create C# Script** and **Create C++ Component** are bounded source-generation workflows. They create contained source stubs and matching metadata entries with new UUID type identity through atomic writes. A generated but unbuilt component remains addable/serializable and is explicitly diagnosed as runtime unavailable.
- Production C# builds use `DragonPixel.Contracts` attributes and a source generator to emit deterministic metadata plus worker-side factory registration. C++ builds use explicit declarations/code generation to emit the same metadata plus a worker-only `dpe_component_plugin_v1` C function table.
- A build-generated runtime-module manifest binds module UUID/build hash/platform/architecture to its component type IDs and entry point. Preview/play workers validate and load it; the editor passes the manifest but never maps its code.
- Managed factories and native C ABI plugins instantiate by stable entity and component type IDs. Lifecycle failures become structured diagnostics. Native exceptions and managed exceptions are contained within their respective worker boundaries.
- Worker restart is the reload mechanism. Stop/crash destroys every component instance and runtime-only transform. No project component can write the editor-owned scene directly.
- Missing, stale, incompatible, or failed modules leave records visible and losslessly preserved. Runtime activation is disabled for that type, and the diagnostic names the required module/build.

## Consequences

The editor remains stable and can inspect a project without executing it. Build tooling and workers gain generators, registrars, version negotiation, module resolution, and lifecycle complexity. Source creation can arrive before arbitrary runtime loading as long as the UI labels unbuilt components honestly and acceptance does not claim execution support.

## Alternatives considered

- **Reflect/load project code in the editor:** rejected because it violates crash, security, compatibility, and AOT boundaries.
- **Treat source filenames or qualified names as identity:** rejected because renames would corrupt durable references.
- **Use a C++ class ABI across plugins:** rejected because compiler/STL/runtime combinations are not a stable cross-platform boundary.
- **Hide records when code is missing:** rejected because it risks data loss and makes repair impossible.
- **Compile on every Inspector edit:** deferred; build orchestration, dependency restore, and arbitrary code execution need separate UX/security work.

## Validation and acceptance gate

POC I must prove metadata-v4/project-v3 validation and deterministic migration, C# and C++ golden parity, editor-process exclusion, worker-only factory/plugin loading, lifecycle/error containment, restart cleanup, and missing-module preservation on Windows, macOS, and Linux. Until that evidence is reviewed, this ADR remains `Proposed`, custom source creation is an authoring preview, and arbitrary project C#/C++ execution is not called supported.

