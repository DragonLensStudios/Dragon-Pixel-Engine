# Dragon Pixel Engine Architecture Decision Records

> **Design revision:** `DPE-ARCH-0007`
> **Last reviewed:** 2026-07-25

ADRs preserve the rationale, constraints, consequences, and validation evidence for durable Dragon Pixel Engine architecture choices. The Design Document remains the architecture summary; these records provide decision history.

## Status rules

- `Proposed`: drafted and available for review, but its named evidence gate is incomplete.
- `Accepted`: reviewed and supported by the required prototype/test evidence.
- `Superseded`: replaced by a later ADR with an explicit migration or compatibility plan.
- `Rejected`: evaluated but not selected.

Changing an accepted decision requires updating the Design and Prompt/Result living documents, their shared revision, this index, affected risks/roadmap, and both documentation mirrors.

## Index

| ADR | Status | Evidence gate |
| --- | --- | --- |
| [ADR-0001](ADR-0001-editor-worker-process-topology.md) | Proposed | Corrected POC B lifecycle/input-to-present evidence and POC E runtime reload/recovery |
| [ADR-0002](ADR-0002-hybrid-entity-component-ownership.md) | Proposed | POC C metadata/serialization, S1.0 lifecycle tests, and POC F resolved prefab ownership |
| [ADR-0003](ADR-0003-stable-native-managed-c-abi.md) | Proposed | POC A ABI ownership/error matrix on all platforms |
| [ADR-0004](ADR-0004-framework-adapter-boundary.md) | Proposed | POC E real scene-driven rendering/picking plus MonoGame/KNI conformance |
| [ADR-0005](ADR-0005-cross-language-metadata.md) | Proposed | POC C schema conformance plus POC H typed/mixed Inspector interactions |
| [ADR-0006](ADR-0006-json-versioning-and-preservation.md) | Proposed | POC C round trips plus POC F deterministic scene/prefab migrations and recovery |
| [ADR-0007](ADR-0007-qt-widgets-and-lgpl.md) | Proposed | POC H Qt interactions/accessibility plus license artifact audit |
| [ADR-0008](ADR-0008-command-transactions-and-undo.md) | Proposed | Native command/undo/savepoint tests and POC H real UI command interactions |
| [ADR-0010](ADR-0010-worker-rendering-and-frame-transport.md) | Proposed | Corrected POC B and POC E real-device frames, revisions, resize, picking, and recovery |
| [ADR-0012](ADR-0012-physics-ownership-and-backends.md) | Proposed | POC G Box2D/Jolt ownership, ABI, fixed-step, event/query, and crash matrix |
| [ADR-0013](ADR-0013-linked-nested-prefabs.md) | Proposed | POC F three-level nesting, overrides/rebase/unpack, fallbacks, cycles, and atomic saves |
| [ADR-0014](ADR-0014-project-components-and-worker-runtime-modules.md) | Proposed | POC I metadata/generator parity, editor exclusion, worker-only C#/C++ lifecycle, and unavailable-module preservation |
| [ADR-0015](ADR-0015-embedded-play-input.md) | Proposed | POC J real Qt Play capture, neutralization, consumed actions, device-pixel changes, and correlation |

Windows 11 x64 and Ubuntu 24.04 x64 now pass the current 36-test Release and native AddressSanitizer matrices: Windows Release 36/36 in 118.39 seconds, Windows MSVC AddressSanitizer 36/36 in 134.93 seconds, Ubuntu Release 36/36 in 102.20 seconds, and Ubuntu Clang AddressSanitizer 36/36 in 101.97 seconds. Corrected POC B/POC E evidence on both platforms now uses real MonoGame/KNI device frames, correlated input-to-present timing, graphics-thread ID-buffer picking, and persistent shared-memory reads; current POCs F-H also pass their registered automated coverage on those two platforms. The available macOS arm64 evidence remains the earlier 14-of-15 result with the unchanged POC B 1280×720/30 FPS gate failing, and the current renderer/transport repairs and POCs E-H have not yet run there. All ADRs remain `Proposed` until their named three-platform and review/compliance gates complete.
