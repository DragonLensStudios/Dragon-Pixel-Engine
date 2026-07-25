# ADR-0001: Editor and Worker Process Topology

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

Project code can crash, leak, block, or load incompatible managed/native dependencies. The editor must preserve authoritative authoring state, isolate play-mode mutation, support fast revision-correlated preview updates, and render scene-driven worker frames without coupling the editor process to a framework or graphics API. Slice 2 also requires editor-camera rendering, picking, gizmo previews, live diagnostics, and an optional isolated simulation preview without allowing any worker to become the saved-scene authority.

## Decision

- The Qt editor owns authoritative project, scene, asset, selection, command, workspace, and dirty-state services. It supervises separate managed preview and play worker processes through `IRuntimeSessionService`.
- The preview worker consumes a flattened, immutable authoring snapshot. Committed editor commands publish monotonically increasing snapshot and command revisions. The worker validates a replacement completely and acknowledges an atomic swap before frames from that revision are accepted; routine edits reload the preview in place rather than restarting the process.
- Starting Play launches a distinct worker from an immutable run snapshot. Play uses exactly one enabled primary scene camera and reports missing or ambiguous primaries. Stop destroys the play worker and runtime world. Runtime transforms, physics, scripts, and diagnostics never write back to saved authoring records.
- Edit mode renders through the preview worker using editor-owned camera and selection state. The optional `Simulate` action advances only an isolated preview runtime world. Leaving Simulate, stopping, or crashing discards that world without creating authoring commands.
- Framework graphics work remains on the adapter worker's graphics thread. Control I/O, diagnostics, cancellation, and snapshot preparation may be asynchronous but cannot call a framework graphics device from arbitrary threads.
- Worker and frame messages carry a session generation plus snapshot, camera, command, and frame revisions. The editor ignores frames and replies from a retired generation or stale revision.
- Control traffic uses the negotiated local protocol, and image bytes use the separately negotiated frame transport defined by ADR-0010. No worker maps or writes authoritative project files.
- A worker announces protocol/capability versions, adapter/runtime/build identity, graphics backend/device identity, and resource handles before it becomes Ready. Failure retires the generation, releases transport resources, records the last correlated operation, and leaves the editor responsive.
- First-release code reload restarts the affected worker and hydrates it from the latest valid snapshot. Managed assembly unloading and in-editor native library unloading are not correctness dependencies.

## Consequences

The design gains crash isolation, deterministic edit/play separation, revision-safe interactive previewing, and runtime-version independence. It pays process startup, IPC, frame-copy, snapshot-flattening, and supervision complexity. In-process hosting remains outside the supported editor path. A temporary preview can differ from saved state only while a command preview or isolated simulation is active; both have explicit commit/cancel behavior.

## Alternatives considered

- **Embed .NET and project code in the Qt editor:** rejected as the normal path because unloadability, dependency conflicts, blocking calls, and crashes would share the authoring process.
- **Use one worker for Edit and Play:** rejected because runtime mutation could leak into the preview mirror and Stop could not be proven to discard the play world independently.
- **Restart preview after every edit:** retained only as crash/code-reload recovery because it would make property editing and gizmos unnecessarily slow.
- **Let the runtime save scenes:** rejected because it creates two authorities and bypasses commands, validation, undo, and recovery.

## Validation and acceptance gate

POC B must prove play/pause/resume/stop, forced crash/restart, responsive Qt viewing, resource cleanup, correlated input-to-present evidence, and the specified frame targets on Windows, macOS, and Linux. POC E must prove that real MonoGame and KNI renderers consume scene snapshots, reload revisions in place, produce scene-dependent pixels and picking IDs, resize safely, and survive a worker restart. Play-mode isolation tests must show that Stop and crash recovery leave authoritative bytes unchanged. The ADR remains `Proposed` until all corresponding three-platform evidence is reviewed.

Current evidence (2026-07-25): Windows and Ubuntu now pass separate preview/play process-ID checks, immutable snapshot loading, real scene-driven MonoGame/KNI device rendering and readback, graphics-thread single-pixel ID-buffer picking, persistent shared-memory frame consumption, pause/stop, and forced play-worker crash/restart while preview remains alive. The full matrices pass on Windows Release (36/36 in 118.39 seconds), Windows MSVC AddressSanitizer (36/36 in 134.93 seconds), Ubuntu Release (36/36 in 102.20 seconds), and Ubuntu Clang AddressSanitizer (36/36 in 101.97 seconds).

The available macOS arm64 Release and AddressSanitizer matrices remain the earlier 14-of-15 results. Both fail `poc_b.worker_viewport` at 1280x720 because presented throughput is below the unchanged 30 FPS gate; the two recorded failing runs are approximately 14.2 FPS and 20.8 FPS. macOS has not yet rerun the corrected absolute pacing, input-to-present correlation, real renderer, picking, and persistent-reader implementation. That missing current platform evidence keeps this ADR `Proposed` without reducing resolution, frame rate, latency targets, or platform scope.
