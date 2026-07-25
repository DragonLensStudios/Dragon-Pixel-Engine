# POC B Worker, Viewport, and Adapter Evidence

> **Evidence status:** Windows and Ubuntu transport, lifecycle, and direct framework GPU probes passed; macOS pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **Related ADRs:** ADR-0001 and ADR-0004

## Purpose

Validate isolated MonoGame and KNI worker processes, a replaceable BGRA8 shared-frame transport, runtime controls, crash recovery, and a Qt viewport consumer before production IPC and adapter packages are introduced.

## Implemented proof

- Separate .NET 10 MonoGame and KNI worker executables behind one framework-neutral lifecycle host.
- MonoGame 3.8.5 and KNI `nkast.Xna.Framework` 4.2.9001 compatibility execution on .NET 10.
- Framework-specific XNA math adapters projecting the same rotating static cube.
- Direct framework probes that create real MonoGame and KNI `GraphicsDevice` instances, draw a sprite with `SpriteBatch`, draw a cube with `BasicEffect`, render into a `RenderTarget2D`, and inspect readback pixels.
- A shared software fixture renderer producing one animated checker sprite and one shaded static cube.
- A file-backed memory map with a versioned 64-byte header, BGRA8 pixels, content flags, timestamps, and a cross-process seqlock.
- Length-prefixed JSON-RPC 2.0 control over redirected process pipes for handshake, initialize, load snapshot, play, pause, resume, stop, diagnostics, forced crash, and shutdown.
- A Qt 6.11 Widgets viewer that launches either worker, maps and displays frames, exposes runtime controls, and reports worker exit.
- Automated checks for frame dimensions/stride/format/content, visible color variation, pause/stop quiescence, resume, forced nonzero exit, process replacement, clean shutdown, control latency, frame rate, and recovery time.
- Offscreen Qt smoke tests for both adapter selections.

## Windows evidence

Environment: Windows 11 x64, Qt 6.11.1, MSVC v143 from Visual Studio Build Tools 2022 17.14.37, .NET SDK 10.0.203.

A representative direct Release run measured:

| Adapter | Measured frame rate | Initial control response | Crash-to-new-frame recovery | Status |
| --- | ---: | ---: | ---: | --- |
| MonoGame | 32.0 FPS | 75.3 ms | 106.1 ms | Passed |
| KNI | 31.7 FPS | 69.5 ms | 106.4 ms | Passed as experimental |

The complete Release and MSVC AddressSanitizer CTest suites passed:

- `poc_b.worker_viewport`
- `poc_b.monogame_graphics`
- `poc_b.kni_graphics`
- `poc_b.qt_monogame`
- `poc_b.qt_kni`

Each offscreen Qt test received a stable BGRA8 frame declaring both sprite and static-mesh content.

Both direct graphics tests reported a normal graphics-device status and 5,608 distinct readback colors after rendering their sprite and static cube. This proves that the Windows test reaches each framework's graphics-device, resource, render-target, drawing, and readback path rather than only executing framework math types.

## Ubuntu evidence

Environment: clean Ubuntu 24.04 x64 Docker image, Clang 18, Qt 6.11.1, .NET SDK 10.0.203, and Xvfb with Mesa graphics.

| Adapter | Release frame rate | Initial control response | Crash-to-new-frame recovery | Direct graphics readback | Status |
| --- | ---: | ---: | ---: | ---: | --- |
| MonoGame | 53.9 FPS | 114.5 ms | 90.3 ms | 5,589 colors; device normal | Passed |
| KNI | 54.9 FPS | 65.4 ms | 101.8 ms | 5,589 colors; device normal | Passed as experimental |

All five POC B tests pass in both Release and Clang AddressSanitizer configurations. The production editor tests additionally pass authenticated Unix-domain-socket control for both adapters and recovery from a forced worker exit into a genuinely new process generation.

## Evidence boundaries

- This POC validates process isolation, lifecycle, adapter loading, XNA math compatibility, software fixture production, shared-frame transfer, Qt consumption, and direct framework render-target/readback behavior on Windows and Ubuntu.
- The shared viewport frame is still produced by a framework-neutral software fixture. The direct GPU probes independently validate framework graphics devices; connecting their render targets to the shared transport remains later performance/integration work rather than a Slice 1 transport requirement.
- The original POC control path uses framed process pipes. The production Slice 1 editor/worker path passes authenticated named pipes on Windows and Unix-domain sockets on Ubuntu; macOS execution remains pending.
- File-backed memory mapping is the replaceable first transport. GPU-handle transport remains a measured later optimization.
- KNI remains experimental. Compiling and executing its math assembly on .NET 10 is useful evidence but is not the complete KNI compatibility matrix.

## Remaining gate work

- Run identical lifecycle, frame, Qt, and direct graphics suites on macOS 14+ arm64.
- Expand framework conformance beyond the current sprite/static-mesh fixture to content-pipeline and platform-specific resource cases before any production KNI support claim.
- Execute and validate production Unix-domain-socket control on macOS and extend cancellation/audit coverage beyond the current protocol smoke tests.
- Review complete evidence before accepting ADR-0001 or ADR-0004.

## Conclusion

The editor-owned worker topology, replaceable shared-frame interface, and direct MonoGame/KNI graphics paths are feasible on Windows and Ubuntu, including forced crash isolation and rapid process restart. macOS acceptance remains open, so the related ADRs remain `Proposed` and KNI remains experimental.
