# POC B Worker, Viewport, and Adapter Evidence

> **Evidence status:** Current Windows real-frame/lifecycle/picking/Release/sanitizer proof passed; aggregate action-to-Qt-paint and current Ubuntu/macOS gates open
> **Recorded:** 2026-07-25
> **Architecture baseline:** `DPE-ARCH-0009`
> **Related ADRs:** ADR-0001, ADR-0004, and ADR-0010

## Purpose

Validate isolated MonoGame and KNI worker processes, scene-driven graphics-device rendering, the revisioned BGRA8 shared-frame transport, viewport-command correlation, real action consumption, runtime controls, crash recovery, and the Qt viewport consumer.

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
- The conformance reader holds one persistent memory mapping, retries transient odd/changed seqlock sequences, derives FPS from producer publish timestamps, and retains full BGRA8 pixel hashes for every pixel-mutation assertion.
- Automated checks cover frame dimensions/stride/format/content, scene-dependent pixels, pause/stop quiescence, resume, forced nonzero exit, process replacement, clean shutdown, independently measured adapter rates, viewport-command correlation, and full-state action-driven pixel/pick displacement.
- Offscreen Qt smoke tests for both adapter selections.

## Windows evidence

Environment: Windows 11 x64, Qt 6.11.1, MSVC v143 from Visual Studio Build Tools 2022 17.14.37, .NET SDK 10.0.203.

A verbose Release run at 1280x720 measured:

| Adapter | Presented rate | Median viewport-command-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 64.1 FPS | 15.4 ms | 566.3 ms | 933.0 ms | 2.5 ms | 0.1 ms | AMD Radeon 8060S / MonoGame DesktopGL | Passed |
| KNI | 32.1 FPS | 32.4 ms | 253.2 ms | 1213.4 ms | 22.6 ms | 0.1 ms | AMD Radeon 8060S / KNI SDL2 OpenGL | Passed for this experimental conformance case |

Both adapters independently exceed the unchanged 30 FPS gate. The worker uses a 64 Hz absolute origin-derived lattice; the roughly 22–24 ms KNI render/readback skips one slot and presents near 32 FPS without reducing the acceptance threshold. The median column is viewport-command-to-present transport evidence, not the POC J action-to-Qt-paint gate. Control and crash timings are separate lifecycle diagnostics.

Current Windows CTest evidence on 2026-07-25:

- Release: **45/45 tests passed**; the log records 294.56 seconds of test execution.
- MSVC AddressSanitizer: **45/45 tests passed in 368.80 seconds**.

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

| Adapter | Presented rate | Median viewport-command-to-present | Control diagnostic | Crash-to-new-frame diagnostic | Render/readback | Publish | Device/backend | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| MonoGame | 61.7 FPS | 16.3 ms | 306.2 ms | 492.1 ms | 8.5 ms | 0.2 ms | llvmpipe / MonoGame DesktopGL | Passed |
| KNI | 32.3 FPS | 55.1 ms | 315.1 ms | 534.4 ms | 26.6 ms | 0.2 ms | llvmpipe / KNI SDL2 OpenGL | Passed for this experimental conformance case |

The complete Ubuntu matrices passed on 2026-07-25:

- Release: **36/36 tests passed in 102.20 seconds**.
- Clang AddressSanitizer: **36/36 tests passed in 101.97 seconds**.

These historical pre-DPE-ARCH-0008 runs cover the then-registered real shared-frame renderer, header-v2 correlation, queued graphics-thread picking, collider overlays, absolute pacing, persistent seqlock reader, authenticated Unix-domain-socket control, worker recovery, and both adapters independently. They do not cover the current 45-test implementation.

## macOS open failure

The available macOS 14+ arm64 Release and native AddressSanitizer matrices each pass 14 of 15 tests. `poc_b.worker_viewport` remains failing at 1280x720: recorded presented throughput is approximately 14.2-20.8 FPS, below the unchanged 30 FPS gate. That result has not been replaced by a passing run of the corrected current implementation.

## Evidence boundaries

- Current Windows and historical Ubuntu acceptance frames are actual MonoGame/KNI render-target readbacks; no synthetic frame satisfies the renderer assertions.
- Current Windows full-state `InputMotion2D` tests prove real gameplay consumption through pixel and retained-ID pick displacement. They do not yet measure aggregate Qt key-event-to-first-reflecting-Qt-paint latency.
- The standalone POC control path uses framed process pipes. Production editor/worker tests cover authenticated named pipes on Windows and Unix-domain sockets on Ubuntu; macOS execution remains open.
- File-backed memory mapping is the replaceable first transport. GPU-handle transport remains a measured later optimization.
- KNI remains experimental. Passing Windows and Ubuntu scene/render/lifecycle results are not its complete content, input, packaging, or three-platform compatibility matrix.

## Remaining gate work

- Fix and rerun the unchanged macOS 1280x720 gate with the current absolute pacing and correlation implementation.
- Complete the real Qt-event-to-first-reflecting-Qt-paint POC J median/tail/drop/timeout measurement and shortcut-suppression evidence.
- Run the current real-frame, correlation, picking, lifecycle, Qt, Release, and sanitizer matrices on macOS 14+ arm64.
- Run the expanded current matrix on Ubuntu rather than treating the historical 36-test result as current.
- Expand framework conformance beyond the current sprite/static-mesh fixture to content-pipeline and platform-specific resource cases before any production KNI support claim.
- Execute and validate production Unix-domain-socket control on macOS and extend cancellation/audit coverage beyond the current protocol smoke tests.
- Review complete evidence before accepting ADR-0001 or ADR-0004.

## Conclusion

The production real-frame worker path, viewport-command correlation, full-state action-driven Windows pixel/pick proof, queued real ID-target picking, and lifecycle controls pass their current Windows assertions. Aggregate action-to-Qt-paint latency and current Ubuntu/macOS matrices remain open; macOS still has the historical POC B failure, the related ADRs remain `Proposed`, and KNI remains experimental.
