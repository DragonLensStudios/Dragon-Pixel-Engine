# POC B Worker, Viewport, and Adapter Evidence

> **Evidence status:** Current Windows and Ubuntu real-frame, lifecycle, picking, latency, Release, and sanitizer proof passed; macOS gate failing
> **Recorded:** 2026-07-25
> **Architecture baseline:** `DPE-ARCH-0006`
> **Related ADRs:** ADR-0001, ADR-0004, and ADR-0010

## Purpose

Validate isolated MonoGame and KNI worker processes, scene-driven graphics-device rendering, the revisioned BGRA8 shared-frame transport, correlated input-to-present timing, runtime controls, crash recovery, and the Qt viewport consumer.

## Implemented proof

- Separate .NET 10 MonoGame and KNI worker executables behind one framework-neutral lifecycle host.
- MonoGame 3.8.5 and KNI `nkast.Xna.Framework` 4.2.9001 compatibility execution on .NET 10.
- Real MonoGame and KNI `GraphicsDevice` implementations that draw a sprite with `SpriteBatch`, draw a depth-tested cube with vertex/index buffers and `BasicEffect`, render into framework `RenderTarget2D` resources, and publish their `GetData` BGRA8 readbacks.
- Scene-driven rendering for enabled transforms, sprites, static meshes, materials, cameras, managed Rotators, baseline lights, and edit-mode collider overlays.
- A shared-memory frame layout version 2 carrying snapshot, camera, selection, command, input, and frame revisions; the version-1 prefix retains negotiated compatibility.
- Absolute monotonic frame deadlines with bounded coarse-wait/high-resolution completion instead of accumulated relative sleeps.
- Length-prefixed JSON-RPC 2.0 control over redirected process pipes for handshake, initialize, load snapshot, play, pause, resume, stop, diagnostics, forced crash, and shutdown.
- A Qt 6.11 Widgets viewer that launches either worker, maps and displays frames, exposes runtime controls, and reports worker exit.
- Revision-correlated single-pixel ID picking plus stale-frame and stale-pick rejection.
- Pick requests are queued to the framework graphics thread and read the actual ID `RenderTarget2D` through a one-pixel `GetData`, avoiding a second full-frame readback without substituting CPU geometry.
- The graphics thread services queued GPU operations while frame publication is paused. A regression first receives structured error `-32021` for an unreachable future revision, then pick-clicks the frozen frame successfully, proving a canceled queue entry cannot block the next valid retained ID and the shared-frame sequence does not advance.
- The conformance reader holds one persistent memory mapping, uses seqlock-validated header-only reads for lifecycle/FPS evidence, derives FPS from producer publish timestamps, and retains full BGRA8 pixel hashes for every pixel-mutation assertion.
- Automated checks for frame dimensions/stride/format/content, scene-dependent pixels, pause/stop quiescence, resume, forced nonzero exit, process replacement, clean shutdown, independently measured adapter rates, and correlated input-to-present latency.
- Offscreen Qt smoke tests for both adapter selections.

## Windows evidence

Environment: Windows 11 x64, Qt 6.11.1, MSVC v143 from Visual Studio Build Tools 2022 17.14.37, .NET SDK 10.0.203.

A verbose Release run at 1280x720 measured:

| Adapter | Presented rate | Median input-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 60.0 FPS | 15.6 ms | 466.4 ms | 683.2 ms | 3.1 ms | 0.1 ms | AMD Radeon 8060S / MonoGame DesktopGL | Passed |
| KNI | 40.0 FPS | 46.6 ms | 153.5 ms | 757.7 ms | 23.3 ms | 0.1 ms | AMD Radeon 8060S / KNI SDL2 OpenGL | Passed for this experimental conformance case |

Both adapters independently exceed the unchanged 30 FPS gate and remain below the unchanged 100 ms median correlated input-to-present gate. The control and crash timing columns are separate diagnostics, not substitutes for input-to-present latency; the functional crash/restart assertions pass under their own test contract.

The complete Windows CTest matrices passed on 2026-07-25:

- Release: **36/36 tests passed in 118.39 seconds**.
- MSVC AddressSanitizer: **36/36 tests passed in 134.93 seconds**.

The passing registered POC B tests include:

- `poc_b.worker_viewport`
- `poc_b.monogame_graphics`
- `poc_b.kni_graphics`
- `poc_b.qt_monogame`
- `poc_b.qt_kni`

Each offscreen Qt test received a stable real-device BGRA8 frame declaring both sprite and static-mesh content. The combined worker test records MonoGame and KNI independently, so a failure in one adapter cannot suppress the other's measurements.

Both direct graphics tests reported a normal graphics-device status and 5,608 distinct readback colors after rendering their sprite and static cube. This proves that the Windows test reaches each framework's graphics-device, resource, render-target, drawing, and readback path rather than only executing framework math types.

Manual Qt QA used only the disposable writable `out/dev/Slice1Sample` copy. It displayed actual MonoGame and KNI preview/play frames, exercised preset creation as one undoable transaction, edited typed Inspector values, verified Simulate Preview remained isolated from authoring state, and verified KNI pause and stop behavior.

## Ubuntu evidence

Environment: clean Ubuntu 24.04 x64 Docker image, Clang 18, Qt 6.11.1, .NET SDK 10.0.203, and Xvfb with Mesa graphics.

| Adapter | Presented rate | Median input-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 61.7 FPS | 16.3 ms | 306.2 ms | 492.1 ms | 8.5 ms | 0.2 ms | llvmpipe / MonoGame DesktopGL | Passed |
| KNI | 32.3 FPS | 55.1 ms | 315.1 ms | 534.4 ms | 26.6 ms | 0.2 ms | llvmpipe / KNI SDL2 OpenGL | Passed for this experimental conformance case |

The complete Ubuntu matrices passed on 2026-07-25:

- Release: **36/36 tests passed in 102.20 seconds**.
- Clang AddressSanitizer: **36/36 tests passed in 101.97 seconds**.

These current runs cover the real shared-frame renderer, header-v2 correlation, queued graphics-thread picking, collider overlays, absolute pacing, persistent seqlock reader, authenticated Unix-domain-socket control, worker recovery, and both adapters independently. CMake position-independent code is enabled for the native library graph, and the sanitizer runtime environment is propagated through managed framework workers and their native-host child processes.

## macOS open failure

The available macOS 14+ arm64 Release and native AddressSanitizer matrices each pass 14 of 15 tests. `poc_b.worker_viewport` remains failing at 1280x720: recorded presented throughput is approximately 14.2-20.8 FPS, below the unchanged 30 FPS gate. That result has not been replaced by a passing run of the corrected current implementation.

## Evidence boundaries

- Current Windows and Ubuntu acceptance frames are the actual MonoGame/KNI render-target readbacks; no synthetic frame satisfies the renderer or latency assertions.
- The standalone POC control path uses framed process pipes. Production editor/worker tests cover authenticated named pipes on Windows and Unix-domain sockets on Ubuntu; macOS execution remains open.
- File-backed memory mapping is the replaceable first transport. GPU-handle transport remains a measured later optimization.
- KNI remains experimental. Passing Windows and Ubuntu scene/render/lifecycle results are not its complete content, input, packaging, or three-platform compatibility matrix.

## Remaining gate work

- Fix and rerun the unchanged macOS 1280x720 gate with the current absolute pacing and correlation implementation.
- Run the current real-frame, correlation, picking, lifecycle, Qt, Release, and sanitizer matrices on macOS 14+ arm64.
- Expand framework conformance beyond the current sprite/static-mesh fixture to content-pipeline and platform-specific resource cases before any production KNI support claim.
- Execute and validate production Unix-domain-socket control on macOS and extend cancellation/audit coverage beyond the current protocol smoke tests.
- Review complete evidence before accepting ADR-0001 or ADR-0004.

## Conclusion

The production real-frame worker path, correlated input-to-present instrumentation, queued real ID-target picking, and lifecycle controls pass the unchanged Windows and Ubuntu gates for both adapters. macOS still fails POC B, the related ADRs remain `Proposed`, and KNI remains experimental.
