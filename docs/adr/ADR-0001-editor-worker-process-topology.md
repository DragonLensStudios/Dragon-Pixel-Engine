# ADR-0001: Editor and Worker Process Topology

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

Project code can crash, leak, block, or load incompatible managed/native dependencies. The editor must preserve authoritative authoring state, isolate play-mode mutation, support runtime restarts, and render worker-produced frames without coupling control messages to a graphics API.

## Decision

- The Qt editor owns authoritative project/scene state and supervises separate managed preview and play worker processes.
- The preview worker consumes a mirror/snapshot for viewport rendering. Starting play launches a distinct worker from an immutable snapshot; Stop destroys that worker/world and never writes runtime changes into saved scenes.
- Control traffic uses versioned, length-prefixed JSON-RPC 2.0 over user-restricted named pipes on Windows and Unix-domain sockets on macOS/Linux.
- Viewport pixels use a separately negotiated `IFrameTransport`; Slice 1 begins with latest-frame BGRA8 shared memory. GPU-handle transports remain replaceable later implementations.
- Workers negotiate protocol version, capabilities, build/runtime identity, cancellation, and diagnostics. Worker failure releases session resources, reports the last correlated operation, and leaves authoring state intact.
- First-release hot reload restarts the affected worker and reloads a clean snapshot; assembly/native-library unloading is not a correctness dependency.

## Consequences

The design gains crash isolation, deterministic edit/play separation, and runtime-version independence. It pays process startup, IPC, frame-copy, and supervision complexity. In-process hosting remains outside the normal Slice 1 path.

## Validation and acceptance gate

POC B must prove play/pause/resume/stop, forced crash/restart, responsive Qt viewing, resource cleanup, and the specified frame/latency targets on Windows, macOS, and Linux. The ADR remains `Proposed` until that evidence is reviewed.

Current evidence (2026-07-24): Windows and Ubuntu pass separate preview/play process-ID checks, immutable snapshot loading, local JSON-RPC control, shared frames, pause/stop, and forced play-worker crash/restart while preview remains alive. macOS arm64 remains pending.
