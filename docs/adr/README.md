# Dragon Pixel Engine Architecture Decision Records

> **Design revision:** `DPE-ARCH-0005`
> **Last reviewed:** 2026-07-24

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
| [ADR-0001](ADR-0001-editor-worker-process-topology.md) | Proposed | POC B lifecycle, crash recovery, and frame transport |
| [ADR-0002](ADR-0002-hybrid-entity-component-ownership.md) | Proposed | POC C metadata/serialization and S1.0 lifecycle tests |
| [ADR-0003](ADR-0003-stable-native-managed-c-abi.md) | Proposed | POC A ABI ownership/error matrix on all platforms |
| [ADR-0004](ADR-0004-framework-adapter-boundary.md) | Proposed | POC B plus MonoGame/KNI conformance matrix |
| [ADR-0005](ADR-0005-cross-language-metadata.md) | Proposed | POC C shared schema and Inspector-model fixtures |
| [ADR-0006](ADR-0006-json-versioning-and-preservation.md) | Proposed | POC C round trips, migration, atomic save/recovery |
| [ADR-0007](ADR-0007-qt-widgets-and-lgpl.md) | Proposed | POC B editor/viewer usability plus license artifact audit |

Windows 11 x64 and Ubuntu 24.04 x64 now pass the relevant Release and native AddressSanitizer gates. All ADRs remain `Proposed` until the matching macOS arm64 evidence and required review/compliance gates are complete.
