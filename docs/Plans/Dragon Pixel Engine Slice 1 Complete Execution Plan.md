# Dragon Pixel Engine Slice 1 Complete Execution Plan

> **Plan status:** In progress; macOS CI publication authorization pending
> **Created:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **Goal:** Complete Slice 1: core, infrastructure, scaffolding, and core editor
> **Repository:** `C:\Projects\Github\Engines\Dragon Pixel Engine`

## Outcome

Deliver the complete Slice 1 vertical slice defined by the Design Document. The same sample project must open on Windows, macOS, and Linux; display editable 2D and 3D entities; expose native and managed components; save and reload without data loss; and run through isolated preview/play workers with MonoGame supported and KNI either conformant or explicitly experimental with evidence.

This plan preserves the full Slice 1 objective. Work is sequenced through the architecture gates so short-term implementation does not silently replace the accepted process, ownership, serialization, or framework boundaries.

## Delivery Sequence

### Gate 1: Foundational decisions

- Create ADRs 0001-0007 for process topology, hybrid component ownership, stable C ABI, MonoGame/KNI adapters, metadata generation, JSON/versioning, and Qt/licensing.
- Keep ADRs `Proposed` while their risk prototypes are incomplete. Promote an ADR to `Accepted` only after its validation evidence is recorded and reviewed.
- Treat design changes discovered by prototypes as architecture changes: update both living documents, their synchronized revision, ADRs, risks, sources, and roadmap before continuing.

### Gate 2: Risk prototypes

- **POC A:** C++ shared library plus .NET 10 worker; negotiate `dpe_api_v1`, exercise handles, UTF-8 buffers, allocation, structured errors, version failure, and 10,000 create/destroy cycles under sanitizer/leak checks.
- **POC C:** one native and one managed component manifest, a shared metadata schema, deterministic scene JSON, stable-ID rename behavior, explicit schema migration, and opaque unknown/newer component round-tripping.
- **POC B:** minimal Qt viewer plus separate MonoGame and KNI workers, one sprite and one lit static mesh, BGRA8 shared-memory frames, play/pause/resume/stop, forced crash, restart, and measured latency/frame rate.
- **POC D:** out-of-process read-only MonoGame/KNI scanner that emits JSON and Markdown evidence while pre/post source-tree manifests remain identical.
- Record per-platform results. A KNI failure preserves experimental status; it must not lower the .NET 10 or platform baseline.

### Gate 3: S1.0 architecture bootstrap

- Establish CMake presets, managed build orchestration, dependency checks, schemas, diagnostics, and CI for Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64.
- Implement portable native `Core`, `Scene`, `Metadata`, `Serialization`, and `CAbi` modules without Qt/framework dependencies.
- Implement `DragonPixel.Contracts` (`netstandard2.1`), `DragonPixel.NativeInterop` (`net10.0`), and the headless runtime worker (`net10.0`).
- Provide one world/scene, UUID identities, hierarchy, transform, one native and one managed component, deterministic JSON, one migration, unknown-component preservation, structured diagnostics, and automated ABI/schema/round-trip tests.

### Gate 4: Slice 1 runtime and editor vertical slice

- Add versioned length-prefixed JSON-RPC over named pipes/Unix-domain sockets, handshake/capabilities/cancellation/errors/audit events, immutable snapshots, and preview/play worker supervision.
- Add separate MonoGame and KNI adapter packages behind the common lifecycle/capability contract. MonoGame is the first supported path; KNI uses the same conformance suite and retains an experimental label until every gate passes.
- Add the Qt 6.11 Widgets editor shell with Scene, Hierarchy, Project/Assets, Inspector, and Console docks plus edit/play/pause/stop controls.
- Keep authoring state in the editor; all mutations use commands/transactions; Stop discards runtime-only changes; worker crashes cannot write saved scenes.
- Add framework-neutral transform, camera, sprite, mesh, material, and basic light records and render one 2D sprite plus one 3D static mesh in the sample.
- Support selection, entity rename/reparent, component add/remove/property edit, project open/save/reopen, diagnostics, and unknown-component presentation.

### Gate 5: Cross-platform acceptance

- Run native, managed, ABI, metadata, serialization, protocol, process-recovery, adapter, and editor smoke suites on all three baseline platforms.
- Prove known, renamed, missing, and version-mismatched component round trips; atomic save/recovery; allocation/error handling; crash/restart; play isolation; and scanner no-write behavior.
- Validate the same sample project and structural JSON fixtures on every platform.
- Record exact KNI support status, Qt deployment/license inventory, unresolved risks, and measured POC B performance.
- Complete Slice 1 only when every acceptance statement has direct evidence; a workflow file or unexecuted test is not proof.

## Public Boundaries Introduced

- `dpe_api_v1`: version/capability query, opaque handles, fixed-width values, explicit UTF-8 views/buffers, paired allocation/free, and stable status/error records.
- `DragonPixel.Contracts`: stable IDs, metadata DTOs, command/result envelopes, snapshot/serialization contracts, and portable attributes targeting `.NET Standard 2.1`.
- Framework adapter lifecycle: capability query, initialize, load snapshot, fixed/variable update, render/frame production, pause/resume/stop, diagnostics, and shutdown.
- Metadata manifests: shared versioned schema for native and managed component/property exposure.
- Durable JSON: project, scene, asset metadata, editor/user state, migration reports, explicit document/component versions, and opaque-record preservation.
- Local control protocol: handshake, inspection, snapshot loading, command application, runtime control, cancellation, diagnostics subscription, and structured errors.

## Verification Commands and Evidence

- Windows environment: `scripts\dev\Test-WindowsEnvironment.ps1`.
- Native configure/build/test: repository CMake configure, build, and CTest presets once introduced.
- Managed build/test: pinned `dotnet build` and `dotnet test` entry points once introduced.
- End-to-end verification: one repository script that runs schema checks, native/managed tests, POCs, sample round-trip, and editor/worker smoke tests.
- CI evidence: completed jobs for Windows, macOS, and Ubuntu using the same presets and fixtures.
- Documentation evidence: recursive relative-path and SHA-256 equality audit for every Markdown file in both documentation roots.

## Current State and Work Record

- Windows environment setup is complete and independently smoke-tested: MSVC v143, AddressSanitizer, CMake, Ninja, Qt 6.11.1, .NET 10, and vcpkg pass.
- The repository has no production engine/editor implementation at plan start.
- ADRs 0001-0007 and their mirrored index are drafted as `Proposed`, with prototype evidence gates recorded.
- POC A passes its native suite and .NET worker in Windows and Ubuntu Release/native AddressSanitizer configurations, including 10,000 ownership cycles. macOS remains pending, so ADR-0003 remains `Proposed`.
- POC C passes its shared-schema, native-manifest, C# source-generation, headless Inspector, deterministic round-trip, opaque-preservation, stable-ID rename, explicit migration, and atomic-save/recovery tests on Windows and Ubuntu. macOS remains pending, so ADR-0005 and ADR-0006 remain `Proposed`.
- POC B passes its Windows and Ubuntu worker lifecycle, BGRA8 shared-frame, forced-crash/restart, MonoGame/KNI adapter-load, offscreen Qt viewport, and direct real-graphics-device tests in Release and AddressSanitizer configurations. Windows reads back 5,608 distinct colors per adapter; Ubuntu reads back 5,589 with a normal device status and measures 53.9 FPS for MonoGame and 54.9 FPS for KNI in the current Release run. KNI remains experimental and ADR-0001/0004 remain `Proposed` pending macOS conformance evidence.
- POC D passes its Windows and Ubuntu out-of-process JSON/Markdown scan and input-tree no-write proof in Release and AddressSanitizer configurations. macOS and real-project coverage remain pending.
- Detailed proof is recorded in the four mirrored `Prototypes/POC * Evidence.md` files.
- The bounded production S1.0 bootstrap is implemented: CMake/.NET orchestration, native core/scene/metadata/serialization/C ABI, portable managed contracts, interop, headless worker, schemas, sample project, deterministic/opaque/migration behavior, and automated tests.
- The Windows and Ubuntu Slice 1 editor/runtime vertical slices pass: Qt shell and required docks, metadata-driven Inspector, command-routed authoring changes, project/scene loading and atomic save, 2D/3D viewport content, separate MonoGame/KNI workers, authenticated named-pipe/Unix-socket JSON-RPC, shared frames, runtime controls, play isolation, and forced-crash recovery.
- Fifteen registered tests pass in Release and native AddressSanitizer configurations on both Windows and Ubuntu. The set covers native and managed production foundations, POCs A-D, direct MonoGame/KNI graphics probes, editor workflows for both adapters, isolated crash recovery, and the Python automation contracts. Every editor smoke path now proves a transaction-driven entity/component edit, project-root traversal rejection, deterministic scene-v2 save, full project close/reopen, asset rediscovery, visible read-only opaque data, native/managed Inspector exposure, external Python dry-run/validation/audit, distinct preview/play process IDs, and immutable play isolation.
- Final post-macOS-hardening CTest times are Windows Release 29.40 seconds, Windows MSVC AddressSanitizer 29.79 seconds, Ubuntu Release 25.66 seconds, and Ubuntu Clang AddressSanitizer 25.33 seconds.
- Unix control endpoints now use a centralized short `/tmp` socket path with a conservative 100-byte guard, avoiding the long per-user temporary paths that can exceed macOS `sockaddr_un` limits. The resulting worker and automation paths pass the Ubuntu editor tests; execution on macOS remains pending.
- The checked-in Ubuntu 24.04 Docker environment provides a clean local Linux build using Clang 18, Qt 6.11.1, .NET SDK 10.0.203, Xvfb, and Mesa. The GitHub Actions matrix remains unexecuted, and macOS 14+ arm64 is the final platform acceptance blocker; KNI remains experimental.
- **Verified fact (2026-07-24):** the pending workflow uses GitHub's `macos-15` arm64 M1 runner label, which satisfies the macOS 14+ arm64 baseline. [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- The pending workflow passes `actionlint` 1.7.12, pins explicit Qt package architectures, asserts an arm64 macOS host, verifies arm64 code in QtCore and every discovered adapter native library, fails if those libraries are absent, and uploads JUnit plus CTest logs even on failure. Local inspection also confirms the restored MonoGame and KNI macOS SDL/OpenAL dylibs contain both x86_64 and arm64 slices; this is packaging preflight evidence, not executed macOS support evidence.
- The complete local worktree has not been staged, committed, branched, or pushed. GitHub-hosted macOS cannot execute this source until the user authorizes publishing it (recommended branch: `codex/slice1-complete`) or supplies an equivalent macOS arm64 execution environment. The repository's configured SSH remote currently lacks a usable local SSH key, while authenticated GitHub CLI HTTPS access is available; any authorized push should use the authenticated HTTPS path without exposing credentials.

## Completion Standard

This plan is complete only when the Design Document's Slice 1 acceptance criteria and prototype gates are proven on Windows, macOS, and Linux. Partial editor visuals, Windows-only success, unexecuted CI definitions, skipped KNI evidence, or a passing subset of tests do not complete the goal.
