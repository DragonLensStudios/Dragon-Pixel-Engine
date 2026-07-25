# ADR-0004: MonoGame and KNI Framework Adapter Boundary

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

MonoGame and KNI expose similar XNA-style APIs but have different packages, capabilities, platform behavior, content pipelines, native dependencies, and compatibility constraints. Treating them as one compile-time framework would leak types and conditionals into portable code. The existing lifecycle prototype and direct graphics probes do not yet form the production scene renderer: the continuous shared-memory frame is synthetic and ignores authored scene content. Slice 1 closure and Slice 2 require one real adapter contract whose output is visibly driven by the same flattened snapshot through both frameworks.

## Decision

- Implement MonoGame and KNI in separate `net10.0` assemblies and worker compositions. Neither assembly references the other framework. Both implement the production internal `IFrameworkAdapter`; the prototype-only adapter abstraction is removed once conformance coverage moves to this interface.
- The lifecycle is explicit: query capabilities; initialize device/content services; load or atomically replace a snapshot; apply viewport, editor-camera, selection, and runtime input state; run fixed and variable updates; render; pause/resume/stop; expose diagnostics/counters; and shut down deterministically.
- The flattened runtime snapshot is framework-neutral and revisioned. It includes enabled entities, transforms, cameras, sprites, static meshes, materials, render layers, asset bindings, managed Rotators, ambient light, directional lights, and at most four point lights. Shadows are excluded from this slice.
- Each adapter creates and owns its real framework `GraphicsDevice`, offscreen `RenderTarget2D`, `SpriteBatch`, vertex/index buffers, depth state, effects, textures, and other framework objects on its graphics thread. It renders color plus an entity-ID target, reads BGRA8 output back through the framework texture API, and publishes it through ADR-0010's transport.
- Edit rendering uses the editor camera supplied by the editor. Play rendering uses exactly one enabled primary scene camera. The adapter reports structured diagnostics rather than silently choosing among missing or ambiguous primary cameras.
- Picking uses the same transformed geometry, visibility, depth ordering, snapshot revision, and camera revision as the color frame. Framework-specific numeric/object identifiers are translated back to stable entity UUIDs before crossing the adapter boundary.
- Portable renderer and component contracts contain only engine DTOs and asset handles. Framework objects, content managers, device state, effects, texture instances, input APIs, and coordinate conversions stay private to their adapter.
- Capabilities describe optional features and limits. Missing required capabilities fail before snapshot activation; adapters do not emulate success with a synthetic image.
- MonoGame is the first supported adapter and is measured against the full platform/conformance suite. KNI runs the same tests independently and remains visibly experimental until `.NET 10`, rendering, picking, content, input, shutdown, packaging, and all desktop platforms pass. KNI failure never lowers the engine-wide runtime or platform baseline.

## Consequences

Separate adapters duplicate device/content glue but keep portable contracts clean and make divergences explicit capabilities instead of hidden conditionals. Real CPU readback provides a common first transport and reliable evidence, but it can be expensive; a later GPU-handle transport may replace it without changing scene or adapter contracts. KNI support can lag without blocking MonoGame or misrepresenting compatibility.

## Alternatives considered

- **Compile against a common XNA type surface:** rejected because package identity does not guarantee content, native-library, device, or platform behavior.
- **Render in the native editor:** rejected for the MonoGame/KNI path because it would duplicate framework rendering and make the viewport unlike the actual runtime.
- **Accept a software/synthetic worker frame plus a separate GPU probe:** useful for transport diagnosis but rejected as product or acceptance behavior because authored changes are not proven to reach device-produced pixels.
- **Expose framework types in portable components:** rejected because it prevents adapter substitution and leaks framework dependencies into native, managed, serialized, and future Unity boundaries.

## Validation and acceptance gate

POC B must run lifecycle and end-to-end frame pacing for each adapter independently so an assertion for one adapter cannot suppress the other's measurements. POC E must prove, for both adapters on every baseline platform, that add, move, color, enable/disable, delete, camera, light, and resize changes alter real device-produced pixels and correct picking UUIDs through the shared-memory path. Diagnostics must identify the actual adapter package, graphics device, backend, and snapshot/frame revisions. The acceptance path has no synthetic fallback.

Current evidence (2026-07-25): MonoGame and KNI now pass the current Windows and Ubuntu worker lifecycle, actual framework-device/render-target/readback, real scene snapshot rendering, graphics-thread single-pixel ID-buffer picking, persistent shared-memory publication/consumption, resize, and recovery suites. In combined POC B on Windows, MonoGame records 60.0 FPS and 15.6 ms median input-to-present latency while KNI records 40.0 FPS and 46.6 ms; independent POC E records 60.1 FPS/15.7 ms for MonoGame and 40.7 FPS/46.7 ms for KNI. On Ubuntu, combined POC B records 61.7 FPS/16.3 ms for MonoGame and 32.3 FPS/55.1 ms for KNI; independent POC E records 61.6 FPS/16.4 ms and 33.3 FPS/55.2 ms respectively. Both adapters therefore clear the unchanged 1280x720, 30 FPS, and 100 ms gates on those platforms without a synthetic acceptance fallback.

The full matrices pass on Windows Release (36/36 in 118.39 seconds), Windows MSVC AddressSanitizer (36/36 in 134.93 seconds), Ubuntu Release (36/36 in 102.20 seconds), and Ubuntu Clang AddressSanitizer (36/36 in 101.97 seconds). The available macOS arm64 results remain the earlier 14-of-15 Release and AddressSanitizer runs, where `poc_b.worker_viewport` records approximately 14.2 FPS and 20.8 FPS and fails the unchanged throughput gate. macOS has not rerun the current repairs. MonoGame and KNI evidence remains separately reported, and KNI remains experimental; this ADR remains `Proposed`.

## Primary sources

Verified 2026-07-24:

- [MonoGame render targets and `Texture2D.GetData`](https://docs.monogame.net/articles/getting_to_know/whatis/graphics/WhatIs_Render_Target.html)
- [MonoGame platform guidance](https://docs.monogame.net/articles/getting_started/platforms.html)
- [KNI repository and platform documentation](https://github.com/kniEngine/kni)
- [KNI framework target declarations](https://github.com/kniEngine/kni/blob/main/src/Xna.Framework/Xna.Framework.csproj)
