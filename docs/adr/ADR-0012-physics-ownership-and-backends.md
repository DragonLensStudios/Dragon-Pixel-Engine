# ADR-0012: Physics Ownership and Backend Boundary

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0009`

## Context

Slice 2 requires live 2D and 3D rigid-body simulation without allowing physics-library types, allocators, threading rules, or coordinate conventions to leak into portable authoring data, framework adapters, managed contracts, or the public native/managed boundary. Preview and play also require strict world isolation: runtime transforms and contacts are transient results and must never become saved authoring state merely because a simulation stopped.

Box2D and Jolt solve different dimensional problems and expose different ownership, stepping, job-system, and query APIs. Dragon Pixel Engine therefore needs one engine-owned semantic boundary while retaining separate private backends and independently testable lifetime rules.

## Decision

### Backends and dependency boundary

- Pin Box2D 3.1.1 for 2D simulation and Jolt 5.6.0 for 3D simulation through the repository's vcpkg baseline. An upgrade requires conformance reruns, dependency/license inventory updates, and architecture review when behavior or public contracts change.
- Expose engine-owned, framework-neutral physics world, body, shape, material, filter, contact, transform, and query records. Box2D and Jolt headers, handles, math types, allocators, callbacks, and enums remain private to their backend modules.
- Do not place backend types in Core, Scene, saved JSON, `DragonPixel.Contracts`, `dpe_api_v1`, MonoGame/KNI adapters, or Qt editor code. Framework workers use the native boundary and consume only neutral snapshot, command, transform, contact, diagnostic, and query records.
- Use the canonical right-handed, Y-up engine space. Two-dimensional simulation uses the XY plane; backend adapters perform every coordinate, angle, unit, and enum conversion at their boundary.

### World ownership and lifecycle

- Each native runtime world exclusively owns its Box2D world, Jolt physics system, UUID-to-body mappings, shape/body resources, transient transforms, contact/event queues, timing accumulator, and backend execution support. No mutable physics world or body is shared between preview, play, editor, or another runtime world.
- Build a physics world from a validated flattened runtime snapshot. Apply later mutations only through ordered, revision-correlated command batches at an owning-thread safe point; never mutate a backend world from an IPC callback or editor thread.
- Box2D stepping runs on the owning runtime thread for the initial implementation. Jolt may use a runtime-world-owned job system, but all scheduled work must complete before step results are published or the world is destroyed.
- Pause retains a runtime world without stepping it. Stop, worker shutdown, failed snapshot load, and crash cleanup destroy the entire runtime world and invalidate all of its handles. Generation checks must reject stale handles rather than aliasing newly created resources.
- Edit mode displays authored collider geometry without advancing physics. `Simulate` creates an isolated preview simulation. Play creates its own simulation and always advances it. Neither path writes simulated transforms, velocities, or contacts into authoritative scene data; a future explicit apply workflow requires a separate decision.

### Fixed-step simulation and result publication

- Advance physics at a fixed 60 Hz (`1/60` second). A render/update iteration may execute at most four catch-up ticks. When accumulated time exceeds that budget, drop the excess deterministically and emit a structured timing diagnostic containing the world, step, and dropped-duration correlation.
- Use four Box2D solver substeps per engine tick and one Jolt collision step per engine tick for the pinned initial configurations. Changes to these values are versioned runtime configuration and require the same conformance evidence as a backend upgrade.
- Consume authoring and runtime commands in a stable order and publish transforms and contact events only after both dimensional backends complete the tick. Results carry snapshot, command, and step revisions so rendering and diagnostics can identify the state that produced them.
- Stable UUIDs remain the engine identity. Backend IDs are replaceable implementation details. Contact pairs and batched outputs use canonical UUID ordering so logs, tests, and managed consumers do not depend on backend iteration order.

### Components and initial capability scope

- Support `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D`, `RigidBody3D`, `BoxCollider3D`, and `SphereCollider3D`.
- Neutral component data covers static/kinematic/dynamic body mode, damping, gravity participation/scale, initial linear and angular velocity, sensor state, density, friction, restitution, continuous collision detection, collision layer, and collision mask. Metadata defines valid ranges, defaults, units, and unsupported combinations before commands reach a backend.
- The initial query surface includes raycasts and the contact stream needed by runtime systems and diagnostics. Query inputs and results use bounded caller-owned or engine-allocated buffers with explicit capacity/count semantics.
- Joints, characters, vehicles, soft bodies, static mesh colliders, runtime collider-scale animation, and cross-platform network lockstep are excluded from this ADR and require later decisions.

### Native/managed ABI extension

- ABI minor version 1 adds a separately negotiated `dpe_physics_api_v1` extension to `dpe_api_v1`. Older hosts can continue using the base v1 table; requesting an unavailable extension or minor capability returns an explicit unsupported-version/capability status.
- The extension provides opaque runtime-world handles and batched operations for validated snapshot rebuild, command application, fixed stepping, transform extraction, contact extraction, and queries. It uses fixed-width values, explicit structure sizes/versions, UTF-8 diagnostics, stable status codes, and the allocator/ownership rules of ADR-0003.
- No exception, callback-owned backend object, STL value, GC object, raw owning pointer, Box2D/Jolt type, or implicit thread-affine resource crosses the ABI. Every export catches native failures and reports structured status/error data; partial snapshot or command-batch failure leaves the previous valid world state intact or invalidates the candidate world atomically.
- Managed wrappers use generated bindings and `SafeHandle`; disposing or losing a worker tears down the owning native runtime world. Calls that violate documented world-thread affinity fail with diagnostics instead of racing backend state.

### Determinism, testing, and licensing

- The supported repeatability claim is same-binary replay for the pinned backend versions, engine build, architecture, configuration, command order, and inputs. It is verified by conformance tests and is not a promise of bit-identical results across operating systems, CPU architectures, compilers, backend upgrades, or different worker counts.
- Cross-platform acceptance compares physically meaningful outcomes with documented tolerances. Cross-platform bitwise lockstep requires a later ADR and must not be inferred from fixed stepping or a backend's own determinism statements.
- Box2D 3.1.1 and Jolt 5.6.0 are distributed under the MIT license. Record each exact version/source, build options, copyright notice, and license text in the third-party dependency and release inventories. Preserve required notices in source and binary distributions; re-run license review for upgrades or optional backend features.

## Consequences

The editor, managed workers, and framework adapters gain one stable physics vocabulary while 2D and 3D implementation details remain independently replaceable. Runtime-world ownership makes Stop, crash recovery, sanitizer analysis, and play-mode isolation testable, and fixed stepping makes replay evidence meaningful.

The cost is a neutral translation layer, two native dependencies, duplicate backend conformance work, explicit result batching, and careful lifecycle/threading enforcement. A feature supported by only one backend cannot silently enter the shared component contract. Runtime simulation remains deliberately separate from authoring state, so workflows that apply simulated values later need an explicit command and ADR.

## Alternatives rejected

- Use one physics library for both dimensions: rejected because it would either compromise the dedicated 2D workflow or force 2D authoring through a 3D abstraction with different behavior and cost.
- Expose Box2D/Jolt directly to managed adapters or scene components: rejected because it leaks unstable backend ABI, ownership, and math types across portable boundaries.
- Run simulation inside the Qt editor process: rejected because backend crashes, jobs, and project code would threaten authoritative authoring state and violate worker isolation.
- Use variable-delta stepping or unbounded catch-up: rejected because it makes behavior frame-rate-dependent and permits a stalled worker to enter a persistent catch-up spiral.
- Promise cross-platform bitwise determinism now: rejected because the pinned libraries, compilers, architectures, and floating-point environments do not establish that product guarantee.

## Validation and acceptance gate

POC G must pass on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64 before this ADR can become `Accepted`. Release and native AddressSanitizer evidence must cover:

- repeated world/body/shape construction and destruction, stale/invalid handles, allocator pairing, failed candidate rollback, and worker crash cleanup;
- 2D and 3D drop-and-rest scenes; static, kinematic, and dynamic bodies; gravity/damping; sensors; layers/masks; CCD; contacts; raycasts; and transform mapping;
- fixed 60 Hz stepping, four-tick catch-up limiting, excess-time diagnostics, pause/resume, preview/play isolation, and Stop without authoring mutation;
- snapshot/command/result revision correlation, stable UUID mapping, bounded batch buffers, ABI version/capability negotiation, and managed `SafeHandle` cleanup;
- same-binary replay for the pinned configurations plus tolerance-based three-platform comparison; and
- complete vcpkg provenance, dependency/license inventory, preserved MIT notices, and backend/device/build identity in diagnostics.

Current Windows and Ubuntu evidence (2026-07-25): the private Box2D 3.1.1/Jolt 5.6.0 facade, fixed-step accumulator, C ABI minor-1 extension, managed `SafeHandle` wrapper, scene-v3 snapshot parsing, worker lifecycle, and isolated Simulate Preview/Play integration are implemented. Tests cover native/ABI/managed ownership, invalid and stale handles, candidate rebuild rollback, fixed stepping and catch-up, 2D/3D transforms, contacts and queries, pause/resume/Stop cleanup, unchanged source bytes, and atomic rejection of multiple 3D colliders until SubShapeID-aware attribution exists. The real adapter renderer also proves device-pixel collider overlays for Box/Circle 2D and Box/Sphere 3D.

The current Windows strict Release and MSVC AddressSanitizer matrices both pass **45/45 tests**; ASan completes in **368.80 seconds**. Ubuntu's latest pre-DPE-ARCH-0008 Release and Clang AddressSanitizer matrices remain 36/36. `CMAKE_POSITION_INDEPENDENT_CODE` is enabled, sanitizer settings propagate through managed/native child workers, and Windows deploys the MSVC ASan runtime beside every instrumented native target. Manual Qt QA used only the disposable writable `out/dev/Slice1Sample` copy and confirmed Simulate Preview remained isolated from authoring state while KNI preview/play pause and Stop stayed functional.

No current POC G Release/sanitizer evidence exists for macOS arm64. Complete three-platform tolerance comparison, packaged-notice enumeration, and the full Qt Inspector designer workflow for adding and configuring all baseline bodies/colliders remain open. Implementation evidence does not promote this decision: ADR-0012 remains `Proposed` until the macOS POC G and designer gates pass.

Primary sources (verified 2026-07-24): [Box2D 3.1.1 release](https://github.com/erincatto/box2d/releases/tag/v3.1.1), [Box2D license](https://github.com/erincatto/box2d/blob/v3.1.1/LICENSE), [Jolt Physics 5.6.0 release](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0), [Jolt architecture and determinism guidance](https://github.com/jrouwe/JoltPhysics/tree/v5.6.0/Docs), and [Jolt license](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE).
