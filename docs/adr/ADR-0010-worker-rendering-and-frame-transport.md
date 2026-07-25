# ADR-0010: Local Worker Rendering Protocol and Frame Transport

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0006`

## Context

The Qt editor, preview worker, and play worker need debuggable local control without putting image bytes or framework objects into messages. Slice 2 adds atomic scene reload, editor-camera updates, selection, viewport resize, simulation controls, diagnostics, and precise picking. The pre-DPE-ARCH-0006 POC proved process lifecycle, shared memory, and separate framework device probes, but its continuously published image was synthetic and its latency value measured a control response rather than the time from an input event to presentation of the frame that reflects it. Those shortcuts could not close the original POC B gate or validate a real editor.

## Decision

### Separate control and frame planes

Control uses length-prefixed JSON-RPC 2.0 over a user-restricted named pipe on Windows and a user-restricted Unix-domain socket on macOS/Linux. Image bytes never appear in JSON-RPC. A negotiated shared-memory `IFrameTransport` carries color frames; the adapter retains its entity-ID target for single-pixel pick requests.

The first request is `handshake`. It negotiates protocol major/minor, capabilities, frame-header versions, pixel formats, maximum dimensions, adapter, runtime/build identity, and authentication capability. A major mismatch fails the session. Minor/additive features are enabled only when both peers advertise them.

Every request has a JSON-RPC ID and correlation ID. Long operations support explicit cancellation and structured progress. Errors contain stable code, message, subsystem, retryability, correlation ID, and relevant revisions. Authentication uses the protected inherited session capability from the editor; it is not placed on command lines, project files, logs, or frame memory.

The additive control surface includes:

| Method family | Purpose |
| --- | --- |
| `handshake` / `session.describe` | Negotiate versions/capabilities and report process, adapter, build, graphics device, and backend identity |
| `snapshot.load` / `snapshot.replace` | Validate and atomically activate a flattened immutable snapshot with expected/new revision |
| `viewport.configure` / `viewport.resize` | Set frame format/dimensions and negotiate a replacement transport mapping |
| `viewport.setEditorCamera` | Apply revisioned Edit-mode camera state |
| `viewport.setSelection` | Apply revisioned selection/overlay state |
| `viewport.input` | Send runtime or camera input with a correlation token; authoring edits still use ADR-0008 commands/snapshots |
| `viewport.pick` | Read one entity ID from the retained ID target for a specified displayed frame/revision |
| `runtime.play` / `pause` / `resume` / `stop` | Control the isolated runtime lifecycle |
| `runtime.simulatePreview` | Start/stop the disposable Edit-mode simulation world |
| `diagnostics.subscribe` / `unsubscribe` | Stream structured diagnostics and counters |
| `$/cancelRequest` | Cooperatively cancel a request before its atomic commit/activation point |

Worker methods never mutate authoritative project files. A snapshot replacement builds and validates candidate runtime state off to the side, then swaps it at a graphics-thread boundary. Failure retains the last valid snapshot and frame stream.

### Revision and generation correlation

A worker session has a random session ID and a monotonically increasing session generation assigned by the editor. Within a generation, these unsigned revisions are monotonic:

- snapshot revision;
- editor-camera revision;
- selection revision;
- command revision;
- frame revision.

Requests state the revisions they depend on. Replies and diagnostics echo them. The editor discards messages or frames from retired generations and never presents a frame older than the active snapshot/camera requirements after the corresponding operation is acknowledged.

A runtime or preview input also has an opaque input correlation ID. The worker carries the newest applied correlation ID into the first frame whose pixels reflect that input or preview revision. It must not mark a frame as correlated merely because the control request was received.

### Shared frame transport version 2

The version-2 shared-memory header is fixed-width, bounds-checkable, and forward-extensible. It contains at least:

- magic, header version, header byte length, slot count, and slot byte length;
- width, height, stride, BGRA8-sRGB pixel format, and orientation;
- session ID/generation;
- snapshot, camera, selection, command, and frame revisions;
- input correlation ID, when a correlated input is reflected;
- producer monotonic timestamp for diagnostics;
- slot state/completion sequence and dropped/late flags.

All offsets and lengths are validated before access. The writer marks a slot in progress, writes metadata and pixels, then publishes an even completion sequence with release ordering. The reader uses acquire ordering, verifies the sequence before and after copying, and retries or drops the slot if it changed. The editor consumes the newest complete eligible frame and may skip stale frames instead of blocking the graphics worker.

Header version 1 remains available only through explicit negotiation for existing Slice 1 clients; it cannot provide revision-correlated picking or input-to-present acceptance evidence. Unknown header versions and pixel formats fail safely.

Resize allocates a new validated mapping under a new transport generation. The worker begins publishing there only after both peers acknowledge it. The old mapping is retired after the editor releases it or the session is destroyed. Zero, negative, overflowing, over-capability, or unreasonable dimensions fail without affecting the active mapping.

Worker exit, timeout, protocol failure, and editor shutdown close handles/unlink names and retire every mapping. Crash-restart tests verify that a new generation cannot accidentally reuse a stale frame as current.

### Real rendering and picking

Only the real MonoGame or KNI adapter may publish acceptance frames. The adapter creates a framework graphics device and render targets, renders the active flattened scene with depth and lighting, reads the BGRA8 color target through the framework API, and publishes that exact readback. Frame diagnostics identify the adapter assembly/version, graphics device/backend, dimensions, and all revisions. A checkerboard, CPU cube/sprite, cached fixture, or other synthetic fallback is allowed only in an explicitly named transport diagnostic mode and is rejected by product/acceptance tests.

The adapter renders an entity-ID target alongside color using the same snapshot, camera matrices, visibility, transforms, draw ordering, depth state, viewport, and frame revision. `viewport.pick` includes logical and physical pixel coordinates, device-pixel ratio, displayed session/generation, and displayed frame/snapshot/camera revisions. The worker returns a stable entity UUID or no hit. Out-of-bounds, stale, retired, or no-longer-retained frames return a structured result so the editor can retry against the latest frame; they never guess from a newer target.

### Frame pacing and input-to-present measurement

The graphics loop uses absolute frame deadlines. For target period `P`, deadline `n` is derived from one monotonic origin rather than from the completion time of the previous frame. The loop uses a bounded coarse wait followed by a bounded high-resolution completion phase. When late, it reports the overrun and advances to the next future deadline instead of accumulating relative-sleep drift or trying an unbounded catch-up render burst.

Input-to-present latency is measured in the editor's monotonic clock to avoid cross-process clock assumptions:

1. The Qt viewport records `t_input` when it handles the real input event and assigns a correlation ID.
2. The input, camera update, or authoring-preview revision travels through the normal production path.
3. The worker tags the first frame whose rendered pixels include it.
4. The Scene View records `t_present` when the Qt paint/presentation path completes for that tagged frame.
5. The sample is `t_present - t_input`.

Control-response time, worker render time, memory-copy time, and frame-arrival time are reported separately but cannot substitute for this sample. Unreflected inputs, timeouts, dropped correlated frames, warm-up policy, sample count, median, tail latency, FPS, dropped frames, and late deadlines are all included in evidence.

At 1280x720, each adapter is run and reported independently. MonoGame must present at least 30 FPS with median input-to-present below 100 ms on each baseline developer machine. KNI uses the identical gate for conformance; a KNI failure changes only its experimental/support status. No platform-specific threshold, smaller resolution, skipped adapter, or early assertion may hide evidence.

## Consequences

Length-prefixed JSON-RPC remains inspectable and cross-language while shared memory keeps bulk pixels off the control plane. Explicit generations and revisions prevent stale frames, replies, and picks from crossing a restart or reload. CPU BGRA8 readback is portable and testable but may limit throughput; measured D3D shared resources, IOSurface/Metal, or Vulkan external-memory transports may later implement `IFrameTransport` without changing control, scene, or picking contracts.

Absolute deadlines and end-to-end correlation add instrumentation and scheduling complexity. They distinguish the user-visible failure from fast individual stages, which is necessary before optimizing the correct bottleneck.

## Alternatives considered

- **Send images as JSON/base64:** rejected because allocation, encoding, and message size would couple control latency to frame volume.
- **Expose a framework texture/device handle as the public contract:** rejected because it binds Qt and portable protocol code to one graphics API and complicates ownership/restart.
- **Use only worker timestamps for latency:** rejected because clocks can differ and it omits IPC, shared memory, Qt consumption, and presentation.
- **Treat control acknowledgement as input-to-present:** rejected because it proves only request handling.
- **Read the full ID buffer into the editor every frame:** deferred because a revision-checked single-pixel request is sufficient for Slice 2 picking and avoids another full-size readback.
- **Lower the macOS gate:** rejected because macOS is a first-class Slice 1 baseline and the same workflow must remain usable on every supported platform.

## Validation and acceptance gate

POC B must prove version negotiation, both local transports, cancellation, absolute pacing, 1280x720 latest-frame presentation, independently reported MonoGame/KNI measurements, correlated median input-to-present below 100 ms, at least 30 presented FPS, pause/resume/stop, resize, stale-frame rejection, forced crash/restart, and complete resource cleanup on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64.

POC E must prove that real device-produced pixels and ID-buffer picks change correctly after add, move, color, enable/disable, delete, camera, light, snapshot reload, and resize operations through both adapters. It must also prove old-revision frames/picks are rejected and diagnostics name the real adapter/device/backend. No synthetic path may satisfy these assertions.

Current Windows and Ubuntu evidence (2026-07-25): production shared frames are the actual MonoGame/KNI render-target readbacks. Header version 2 carries snapshot/camera/selection/command/input/frame revisions; atomic reload, resize, stale pick rejection, absolute frame deadlines, independently executed adapters, adapter/device/backend diagnostics, and forced worker recovery are automated. Picking is queued to the owning graphics thread and reads the actual ID render target through one-pixel `GetData`. The conformance reader keeps one memory mapping, uses seqlock-validated header-only lifecycle/FPS observation, derives FPS from producer publish timestamps, and still hashes complete BGRA8 payloads for pixel-mutation assertions.

Windows combined POC B measured MonoGame at **60.0 FPS / 15.6 ms median input-to-present / 466.4 ms control / 683.2 ms recovery / 3.1 ms render-readback / 0.1 ms publish** and KNI at **40.0 FPS / 46.6 ms / 153.5 ms / 757.7 ms / 23.3 ms / 0.1 ms**. Independent POC E measured MonoGame at **60.1 FPS / 15.7 ms / 399.7 ms / 638.4 ms / 2.6 ms / 0.1 ms** and KNI at **40.7 FPS / 46.7 ms / 152.4 ms / 728.5 ms / 23.1 ms / 0.1 ms**.

Ubuntu combined POC B measured MonoGame at **61.7 FPS / 16.3 ms / 306.2 ms / 492.1 ms / 8.5 ms / 0.2 ms** and KNI at **32.3 FPS / 55.1 ms / 315.1 ms / 534.4 ms / 26.6 ms / 0.2 ms**. Independent POC E measured MonoGame at **61.6 FPS / 16.4 ms / 349.1 ms / 487.6 ms / 6.8 ms / 0.1 ms** and KNI at **33.3 FPS / 55.2 ms / 353.1 ms / 544.7 ms / 28.0 ms / 0.1 ms**. All results are at 1280x720 and meet the unchanged 30 FPS and 100 ms median gates. Control and recovery diagnostics remain separate from correlated latency.

The complete Windows Release matrix passed **36/36 tests in 118.39 seconds**, and MSVC AddressSanitizer passed **36/36 tests in 134.93 seconds**. Ubuntu Release passed **36/36 tests in 102.20 seconds**, and Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. Position-independent native code and sanitizer-runtime propagation through managed workers/native-host children are included in the Ubuntu proof. Manual Qt QA against the disposable `out/dev/Slice1Sample` copy confirmed actual MonoGame and KNI preview/play output, plus KNI pause/stop, without relying on a synthetic fallback.

The available macOS arm64 Release and AddressSanitizer matrices still pass only 14 of 15 tests. `poc_b.worker_viewport` fails at 1280x720; the two recorded failing runs are approximately 14.2 FPS and 20.8 FPS, both below 30 FPS. The old failure has not been rerun successfully with the corrected transport/reader/picking implementation. The failure remains blocking, thresholds are unchanged, KNI remains experimental, and this ADR remains `Proposed`.

## Primary sources

Verified 2026-07-24:

- [JSON-RPC 2.0 specification](https://www.jsonrpc.org/specification)
- [MonoGame render targets and `Texture2D.GetData`](https://docs.monogame.net/articles/getting_to_know/whatis/graphics/WhatIs_Render_Target.html)
