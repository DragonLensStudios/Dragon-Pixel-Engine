# Dragon Pixel Engine Architecture Decision Records

> **Design revision:** `DPE-ARCH-0014`
> **Last reviewed:** 2026-07-27

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
| [ADR-0009](ADR-0009-plugin-trust-and-compatibility.md) | Proposed | POC P manifest/integrity/permissions, transactional lifecycle, worker isolation, quarantine, compatibility, and licensing |
| [ADR-0010](ADR-0010-worker-rendering-and-frame-transport.md) | Proposed | Corrected POC B and POC E real-device frames, revisions, resize, picking, and recovery |
| [ADR-0011](ADR-0011-unity-contract-boundary.md) | Proposed | POC Q Unity 6.3 .NET Standard 2.1 bridge through Mono and IL2CPP-compatible paths |
| [ADR-0012](ADR-0012-physics-ownership-and-backends.md) | Proposed | POC G Box2D/Jolt ownership, ABI, fixed-step, event/query, and crash matrix |
| [ADR-0013](ADR-0013-linked-nested-prefabs.md) | Proposed | POC F three-level nesting, overrides/rebase/unpack, fallbacks, cycles, and atomic saves |
| [ADR-0014](ADR-0014-project-components-and-worker-runtime-modules.md) | Proposed | POC I metadata/generator parity, editor exclusion, ordered managed/native v2 lifecycle plus v1 compatibility/cleanup, and unavailable-module preservation |
| [ADR-0015](ADR-0015-embedded-play-input.md) | Proposed | POC J input-map/rebinding persistence, Qt keyboard/mouse plus standard-gamepad capture, neutralization, shared consumed actions, device-pixel changes, and correlation |
| [ADR-0016](ADR-0016-orthogonal-tile-authoring.md) | Proposed | POC K deterministic tile formats/tools, adapter rendering/picking, recovery, and Box2D collision |
| [ADR-0017](ADR-0017-named-scene-game-viewports.md) | Proposed | POC L simultaneous named outputs, primary-camera diagnostics, isolation, input, and recovery |
| [ADR-0018](ADR-0018-project-lifecycle-and-recovery.md) | Proposed | POC M templates, creation, project-v4 upgrades, discovery/settings, archive/restore, containment, and recovery |
| [ADR-0019](ADR-0019-reversible-monogame-kni-migration.md) | Proposed | POC N source-tree integrity, explicit plans, sibling generation, verification, and rollback |
| [ADR-0020](ADR-0020-asset-import-and-cache-integrity.md) | Proposed | POC O source ownership, importer isolation, stable sidecars/cache keys, filesystem safety, recovery, and adapter consumption |
| [ADR-0021](ADR-0021-build-package-install-update-and-rollback.md) | Proposed | POC R relocatable build/package/install/update/rollback, signing/notarization, provenance, notices, and Qt materials |
| [ADR-0022](ADR-0022-version-1-compatibility-policy.md) | Proposed | POCs R-S product/API/ABI/protocol/schema/plugin/package compatibility, upgrades, and deprecation fixtures |
| [ADR-0023](ADR-0023-crash-support-bundles-and-privacy.md) | Proposed | POC S local opt-in crash/support evidence, redaction, consent, retention, recovery, and no-default-telemetry proof |

The last complete Windows 11 x64 strict Release and MSVC AddressSanitizer baseline remains 45/45, with the ASan matrix completing in 368.80 seconds. Focused DPE-ARCH-0010 Windows Release/ASan evidence passes for the full-grid editor workspace, direct generated-script attachment, managed/native-v2 lifecycle, native-v1 compatibility, bounded fixed dispatch, stage-failure containment, and generator/cache integrity. Focused DPE-ARCH-0013 input-map/rebinding evidence passes 5/5 Release in 94.67 seconds and 5/5 MSVC AddressSanitizer in 116.79 seconds; its Inspector/input-settings/MyMover follow-up passes 10/10 Release in 133.55 seconds and 10/10 sanitizer in 157.17 seconds. Focused DPE-ARCH-0014 Windows evidence now passes 18/18 Release aliases, the complete Release editor-interaction test, 9/9 sanitizer service/model/prefab aliases, and five focused sanitizer interaction functions. Its refreshed 188-record production-style bundle hash-verifies, passes the packaged MonoGame self-test, and creates/opens a minimal 2D project outside the source tree. The aggregate DPE-ARCH-0014 sanitizer interaction run is inconclusive at its 480-second harness timeout. An unrelated intermittent atomic-publication recovery defect blocks a new authoritative full-matrix claim. Ubuntu 24.04 x64's latest pre-DPE-ARCH-0008 matrices remain Release 36/36 in 102.20 seconds and Clang AddressSanitizer 36/36 in 101.97 seconds. The available macOS arm64 evidence remains the earlier 14-of-15 result with the unchanged POC B 1280×720/30 FPS gate failing. Physical-gamepad/hot-plug proof, aggregate POC J Qt-paint latency, current Ubuntu/macOS expanded evidence, single-preview-worker simultaneous output, accessibility/compliance review, and POCs M-S remain open. All ADRs remain `Proposed`; the architecture revisions and Windows-only evidence promote none of them.
