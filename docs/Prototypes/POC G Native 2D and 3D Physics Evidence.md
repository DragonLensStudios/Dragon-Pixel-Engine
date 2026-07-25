# POC G Native 2D/3D Physics Evidence

> **Evidence status:** Windows and Ubuntu Release/native AddressSanitizer passed; macOS pending  
> **Recorded:** 2026-07-25  
> **Architecture baseline:** `DPE-ARCH-0006`  
> **Related ADRs:** ADR-0001 and ADR-0012

## Purpose

Prove that engine-owned Box2D and Jolt worlds can cross the versioned native/managed boundary safely, step with the specified bounded fixed schedule, report results through neutral records, and remain isolated from editor-owned authoring state throughout preview, Play, Stop, reload, and crash recovery.

## Implemented proof

- Box2D 3.1.1 and Jolt Physics 5.6.0 are pinned through the repository vcpkg baseline.
- Private 2D and 3D backends implement engine-owned neutral physics interfaces without exposing backend types to Scene, managed contracts, framework adapters, saved JSON, or Qt.
- Each native runtime world owns both backend worlds, UUID mappings, transforms, contacts, and query results.
- The fixed-step accumulator runs at 60 Hz, limits catch-up to four ticks, uses four Box2D solver substeps and one Jolt collision step, and records dropped excess time.
- Built-in neutral records support static, kinematic, and dynamic bodies; Box/Circle 2D shapes; Box/Sphere 3D shapes; damping; gravity scale; initial velocity; sensor state; density; friction; restitution; CCD; layer; and mask.
- `dpe_api_v1` minor 1 negotiates `dpe_physics_api_v1` for world ownership, snapshot rebuild, commands, stepping, transforms, contacts, and queries.
- Managed wrappers use `LibraryImport` and `SafeHandle`; failures remain status/error records and no native exception or owning pointer crosses the ABI.
- Scene-v3 physics settings/components are parsed into neutral DTOs. Snapshot replacement builds a candidate world and swaps it only after successful validation.
- Edit preview displays the authored frame without stepping. Simulate Preview steps an isolated world; Play steps its own world; Pause freezes it; Stop destroys it.
- Simulated transforms overlay only the worker's in-memory render scene. Stopping simulation or Play restores/rebuilds from the retained immutable snapshot and never writes a project scene.
- The real MonoGame/KNI renderer draws Box/Circle 2D and Box/Sphere 3D collider overlays in Edit mode without advancing physics or changing pick identity.
- Until Jolt material/sensor attribution is SubShapeID-aware, a 3D body with more than one collider is rejected atomically with a structured diagnostic; the previous valid runtime world remains usable.

## Windows evidence

Environment: Windows 11 x64, MSVC v143, .NET SDK 10.0.203, Box2D 3.1.1, Jolt 5.6.0, Qt 6.11.1, and the pinned vcpkg manifest.

The complete Windows Release matrix passed **36/36 tests in 118.39 seconds**. The complete MSVC AddressSanitizer matrix passed **36/36 tests in 134.93 seconds**.

The physics-focused portion passes 10 of 10 registered aliases, including:

- `s2.physics_world` and `poc_g.native_facade`;
- `s2.physics_abi` and `poc_g.native_abi`;
- `s2.physics_managed_interop` and `poc_g.managed_interop`;
- `s2.worker_physics` and `poc_g.worker_physics`; and
- the Slice 2 Qt interaction aliases that verify the checkable Simulate Preview control and action state.

A real-path set also passes POC B, both independent POC E adapter runs, MonoGame and KNI editor sessions, play-worker crash recovery, collider-overlay device-pixel checks, and the managed worker suite. AdapterHost and both framework worker assemblies build with zero warnings.

The AddressSanitizer matrix ran from Visual Studio Developer PowerShell so the MSVC AddressSanitizer runtime was on `PATH`; invoking instrumented executables without that runtime is an environment loader failure, not a test assertion.

Manual Qt QA used the disposable writable `out/dev/Slice1Sample` copy. Simulate Preview visibly ran an isolated preview world, Stop restored authoring state, and KNI preview/play pause and stop remained functional. This manual pass supports operability on Windows but does not replace automated physics acceptance or another platform.

## Ubuntu evidence

The complete Ubuntu Release matrix passed **36/36 tests in 102.20 seconds**, and the complete Clang AddressSanitizer matrix passed **36/36 tests in 101.97 seconds**. The same native facade, C ABI, managed interop, worker physics, renderer, editor-session, and POC G aliases pass under Clang 18. `CMAKE_POSITION_INDEPENDENT_CODE` is enabled for native libraries, and the Clang sanitizer runtime is propagated into managed workers and native-host child workers so this is executed instrumentation rather than a loader-only smoke test.

## Behaviors covered

Native and ABI tests cover ownership, stale handles, fixed stepping/catch-up, 2D/3D body construction, transform mapping, contact/query batches, pause/resume behavior, cleanup, and atomic rejection of unsupported multiple 3D colliders. Managed tests cover negotiated ABI access and `SafeHandle` disposal. Worker lifecycle tests cover snapshot construction, edit-mode non-stepping, explicit simulation, Play stepping, Pause, Stop disposal, restart from the retained snapshot, diagnostic state, and unchanged source bytes. Renderer tests prove all four baseline collider overlays change framework-device pixels without corrupting content flags or picking IDs.

## Evidence boundaries

- Current Windows and Ubuntu evidence does not establish macOS arm64 build, sanitizer, replay, performance, or cleanup behavior.
- Same-binary repeatability is the current determinism boundary. Cross-platform bitwise lockstep is explicitly not claimed.
- The requested joints, characters, vehicles, soft bodies, static mesh colliders, and runtime scale animation remain excluded.
- Apply-simulated-transforms-to-authoring is not implemented; Stop intentionally discards runtime state.
- Distribution artifacts and complete packaged third-party notice verification remain open.

## Remaining gate work

- Run identical Release and sanitizer matrices on macOS 14+ arm64.
- Record tolerance-based three-platform drop/rest, contacts, raycasts, filtering, CCD, catch-up, replay, worker crash, and source-byte isolation evidence.
- Enumerate packaged backend artifacts and verify MIT notices in every distributable.
- Complete the end-to-end designer workflow for adding/configuring 2D and 3D physics solely through the Inspector.
- Review all evidence before accepting ADR-0012.

## Conclusion

The native facade, C ABI extension, managed ownership, worker lifecycle, collider overlays, editor Simulate Preview control, and authoring-state isolation pass in the complete Windows and Ubuntu Release/native AddressSanitizer matrices. POC G remains open pending macOS, packaging, and full designer physics-authoring acceptance evidence.
