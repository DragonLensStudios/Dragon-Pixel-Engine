# POC E Real Framework Renderer Evidence

> **Evidence status:** Windows and Ubuntu Release/native AddressSanitizer matrices passed; macOS pending  
> **Recorded:** 2026-07-25  
> **Architecture baseline:** `DPE-ARCH-0006`  
> **Related ADRs:** ADR-0001, ADR-0004, and ADR-0010

## Purpose

Prove that both framework workers render scene-dependent pixels through real graphics-device resources, publish those readbacks through the production shared-memory path, retain revision-correlated entity IDs for picking, and meet the unchanged 1280x720 throughput and latency gates.

## Implemented proof

- Separate .NET 10 MonoGame and KNI worker assemblies implement one framework-neutral lifecycle.
- Each adapter creates its own `GraphicsDevice`, `RenderTarget2D`, `SpriteBatch`, vertex/index buffers, depth state, and `BasicEffect`.
- The worker parses version-1 through version-3 snapshots and renders enabled transforms, sprites, static meshes, materials, cameras, managed Rotators, ambient/directional light, and bounded point-light contributions.
- Framework `GetData` readback publishes BGRA8 device pixels through shared-frame layout version 2.
- Layout version 2 carries snapshot, camera, command, input, and frame revisions while an explicit version-1 compatibility test protects the existing prefix.
- `viewportInput` correlations remain attached until a frame containing the input revision is produced.
- Single-pixel picking is queued to the owning graphics thread, reads the actual ID `RenderTarget2D` through one-pixel `GetData`, and rejects stale or unavailable results.
- Atomic snapshot replacement changes the active rendered revision without restarting the worker.
- Edit-mode outlines for Box/Circle 2D and Box/Sphere 3D colliders alter device pixels without changing runtime content flags or picking identities.
- A persistent memory-map reader uses seqlock-validated headers for lifecycle/FPS observation and producer publish timestamps for frame rate, while complete pixel hashes remain mandatory for mutation assertions.
- Resize, pause/resume/stop, forced crash, restart into a distinct process, diagnostics identity, and transport cleanup are automated.

## Windows evidence

Environment: Windows 11 x64, MSVC v143, Qt 6.11.1, .NET SDK 10.0.203, MonoGame 3.8.5, KNI `nkast.Xna.Framework` 4.2.9001, and AMD Radeon 8060S graphics.

| Adapter | Presented rate | Median input-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 60.1 FPS | 15.7 ms | 399.7 ms | 638.4 ms | 2.6 ms | 0.1 ms | AMD Radeon 8060S / MonoGame DesktopGL | Passed |
| KNI | 40.7 FPS | 46.7 ms | 152.4 ms | 728.5 ms | 23.1 ms | 0.1 ms | AMD Radeon 8060S / KNI SDL2 OpenGL | Passed for this experimental conformance case |

Both results exceed the identical 30 FPS gate and remain below the identical 100 ms median latency gate. The adapters ran independently, so an assertion in one path could not suppress the other path's result.

The explicit `poc_e.monogame_scene_rendering` and `poc_e.kni_scene_rendering` tests pass. Their device-pixel signatures and picking IDs change after adding, moving, recoloring, disabling, re-enabling, and deleting the cube; each 2D/3D collider overlay changes device pixels; and resizing to 640x360 and back to 1280x720 updates frame dimensions and camera revisions. Handshake and diagnostics identify the selected framework, real Radeon device, backend, frame layout, and capabilities. Stale revision picks are rejected.

The production editor tests also pass both adapters, forced play-worker recovery, in-place preview reload, picking routing, and per-session frame/socket cleanup.

The complete Windows Release matrix passed **36/36 tests in 118.39 seconds**. The complete MSVC AddressSanitizer matrix passed **36/36 tests in 134.93 seconds**. The matrices include both POC E adapter tests, POC B lifecycle/correlation coverage, MonoGame and KNI editor sessions, crash recovery, scene/command validation, project/index/preview models, prefabs, physics, and Qt interaction tests.

Manual Qt QA used the disposable writable `out/dev/Slice1Sample` copy and visually confirmed actual MonoGame and KNI preview/play output rather than a diagnostic fixture. It also exercised typed Inspector editing, an undoable preset transaction, isolated Simulate Preview, and KNI pause/stop.

## Ubuntu evidence

Environment: Ubuntu 24.04 x64, Clang 18, Qt 6.11.1, .NET SDK 10.0.203, Xvfb, and Mesa llvmpipe graphics.

| Adapter | Presented rate | Median input-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 61.6 FPS | 16.4 ms | 349.1 ms | 487.6 ms | 6.8 ms | 0.1 ms | llvmpipe / MonoGame DesktopGL | Passed |
| KNI | 33.3 FPS | 55.2 ms | 353.1 ms | 544.7 ms | 28.0 ms | 0.1 ms | llvmpipe / KNI SDL2 OpenGL | Passed for this experimental conformance case |

Both adapters independently exceed the unchanged 30 FPS gate and remain below the unchanged 100 ms median latency gate. Ubuntu Release passed **36/36 tests in 102.20 seconds**, and Ubuntu Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. Position-independent native code and sanitizer-runtime propagation through managed workers/native-host children are part of this executed matrix.

## Evidence boundaries

- Sprite and mesh asset bindings currently resolve to built-in checker and unit-cube resources; the general content importer and production asset pipeline are later work.
- Directional and point-light behavior is deliberately baseline rendering through `BasicEffect`; shadows and advanced materials are excluded.
- CPU readback is the accepted portable first transport. GPU-handle transport remains a measured optimization.
- KNI passing this scene does not complete its content, input, packaging, and full platform compatibility matrix.
- Windows and Ubuntu results do not substitute for the required macOS arm64 run.

## Remaining gate work

- Run the identical Release and native AddressSanitizer tests on macOS 14+ arm64 with its real graphics context; no current corrected POC E run exists for macOS.
- Confirm real-device/backend diagnostics, revision/picking behavior, crash cleanup, 1280x720 frame rate, and correlated latency independently for both adapters on macOS.
- Repair the separately recorded macOS POC B throughput failure rather than using the passing Windows POC E result as a substitute.
- Complete the broader KNI conformance matrix before changing its experimental status.
- Review complete evidence before accepting ADR-0001, ADR-0004, or ADR-0010.

## Conclusion

The production scene-driven renderer, shared-frame revision contract, graphics-thread ID-buffer picking, resize, in-place reload, and worker recovery pass their unchanged Windows and Ubuntu performance gates through both MonoGame and KNI. POC E remains open pending corrected macOS evidence.
