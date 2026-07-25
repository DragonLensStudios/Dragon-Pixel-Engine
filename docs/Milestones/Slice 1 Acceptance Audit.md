# Dragon Pixel Engine Slice 1 Acceptance Audit

> **Audit status:** Windows and Ubuntu passed; macOS arm64 execution pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **Scope:** Slice 1 core, infrastructure, scaffolding, and core editor

## Outcome

The complete locally executable Slice 1 scope is implemented and passes on Windows 11 x64 and Ubuntu 24.04 x64 in both Release and native AddressSanitizer configurations. Slice 1 is not accepted yet because the identical source and 15-test matrix have not executed on the required macOS 14+ arm64 baseline.

KNI remains experimental. A workflow definition or two-platform pass is not three-platform support evidence.

## Acceptance Matrix

| Design acceptance statement | Windows 11 x64 | Ubuntu 24.04 x64 | macOS 14+ arm64 | Evidence |
| --- | --- | --- | --- | --- |
| Same sample project opens | Passed | Passed | Pending | Canonical `DragonPixelProject.json` opens `Scenes/Main.dpescene`; project-root escape is rejected |
| 2D sprite and 3D static mesh/camera/light display | Passed | Passed | Pending | MonoGame and KNI render probes plus Qt shared-frame/editor tests |
| Create/select/rename/reparent entity; native and managed components; edit/save/close/reopen | Passed | Passed | Pending | Atomic editor transaction, deterministic scene-v2 save, full close/reopen, hierarchy selection, two-component Inspector proof |
| MonoGame works; KNI uses the same matrix or remains experimental | Passed | Passed | Pending | Both adapters pass locally; KNI label remains experimental |
| Stop discards runtime state; crash cannot corrupt authoring state | Passed | Passed | Pending | Distinct preview/play PIDs, immutable play snapshot, input-file hashes, forced play crash/restart while preview stays alive |
| Unknown components survive and appear as repairable diagnostics | Passed | Passed | Pending | Native opaque round trip plus read-only `Raw preserved record` Inspector assertion after reopen |

## Implemented Contract Conformance

### Core and durable data

- C++20 portable Core, Scene, Metadata, Serialization, and C ABI modules contain no Qt, framework, managed-runtime, Unity, or Python dependencies.
- Worlds/scenes/entities use persistent UUIDs, hierarchy, names, and enabled state.
- Known component types use stable UUIDs and include qualified name, schema version, runtime owner, enabled state, and deterministic JSON properties.
- Scene format version 2 records `$schema`, `engineVersion`, typed identities, enabled state, and component metadata. Version 1 input migrates explicitly.
- Missing/newer/legacy component records retain their original raw subtree and diagnostic identity.
- Authoritative mutations use validated commands; multi-command transactions roll back completely on failure.
- Atomic save, backup/recovery, deterministic output, known/renamed/missing/newer/version-mismatch, allocation, status, and ownership paths are tested.

### Editor and project lifecycle

- Qt 6.11 Widgets provides the `QMainWindow` shell, Scene, Hierarchy, Project/Assets, Inspector, and Console docks plus an authoring viewport.
- The editor opens the canonical project manifest, constrains its startup scene to the project root, loads assets, saves the scene atomically, closes all authoritative state, and reopens structurally equivalent data.
- Entity/component enabled state, native/managed properties, opaque records, project assets, and selected hierarchy state are asserted after reopen.
- The sample contains framework-neutral transform, camera, sprite, mesh, material, light, native component, managed component, and missing-component data.

### Runtime isolation and adapters

- Preview and play are distinct supervised processes, negotiate role and process identity, and receive separate editor-produced snapshots.
- Control uses versioned length-prefixed JSON-RPC over named pipes on Windows and Unix-domain sockets on Ubuntu. Unix endpoint construction is centralized under a short `/tmp` path and rejects paths above a conservative 100-byte limit to avoid macOS `sockaddr_un` truncation; the macOS path remains to be executed.
- Viewport frames use a versioned replaceable BGRA8 shared-memory/seqlock transport.
- MonoGame and KNI implement the same lifecycle and graphics conformance path. Both render a sprite and static mesh with a normal graphics-device status on Windows and Ubuntu.
- Stop disposes only the play world. A forced play-worker crash restarts a fresh play process without stopping preview or writing the saved scene.

### Initial external Python boundary

- The editor creates a user-restricted local endpoint and a random inherited capability token that is not placed in command-line arguments or project files.
- The dependency-free Python client negotiates protocol version and capabilities, inspects the scene, dry-runs and applies the permitted command through editor validation, observes invalid-command rejection, and exercises cancellation.
- JSONL audit events distinguish negotiation, inspection, dry-run, execution, and cancellation capabilities and are tested not to contain the session token.

## Final Local Test Record

| Matrix | Result | CTest time |
| --- | --- | ---: |
| Windows 11 x64 Release, MSVC v143 | 15/15 passed | 29.40 s |
| Windows 11 x64 AddressSanitizer, MSVC v143 | 15/15 passed | 29.79 s |
| Ubuntu 24.04 x64 Release, Clang 18 | 15/15 passed | 25.66 s |
| Ubuntu 24.04 x64 AddressSanitizer, Clang 18 | 15/15 passed | 25.33 s |

Registered tests:

1. `s1.native_core`
2. `s1.managed_contracts_worker`
3. `poc_a.native`
4. `poc_a.managed`
5. `poc_c.metadata_serialization`
6. `poc_b.worker_viewport`
7. `poc_b.monogame_graphics`
8. `poc_b.kni_graphics`
9. `poc_b.qt_monogame`
10. `poc_b.qt_kni`
11. `poc_d.read_only_scanner`
12. `s1.editor_monogame`
13. `s1.editor_kni`
14. `s1.editor_crash_recovery`
15. `s1.python_automation_contracts`

## Documentation and Contract Verification

- All schema, sample project, scene, and asset JSON files parse successfully.
- POC C validates the native and generated managed metadata manifests, scene, project, and asset fixtures against their checked-in JSON Schemas. Canonical `$schema`, producer/generator version, UUID identity, and version fields are required rather than advisory.
- `scripts/docs/Test-DocumentationMirrors.ps1` passes 22 same-relative-path Markdown pairs with identical SHA-256 hashes, strict UTF-8 decoding, no BOM, LF-only endings, valid local links, and matching `DPE-ARCH-0005` revisions in the Design, Prompt/Result, and `AGENTS.md`.
- All 28 unique external Markdown links returned successful HTTP responses on 2026-07-24.
- `git diff --check` reports no whitespace errors. The LLM Prompt Source diff contains no change inside the immutable Original Prompt section.

## Remaining Gate

Run `.github/workflows/slice1.yml` on its `macos-15` arm64 runner in Release and AddressSanitizer configurations. Record exact results for all 15 tests, Qt/editor startup, Unix-domain IPC, shared memory, framework graphics, atomic persistence, external Python, project reopen, and crash recovery. Fix any findings without removing macOS or weakening the matrix.

The workflow passes `actionlint` 1.7.12 and pins explicit Qt package architectures. Its macOS job asserts the host is arm64, verifies arm64 code in QtCore and every discovered managed-adapter native dylib, fails when the expected libraries are absent, writes JUnit evidence to an absolute workspace path, and uploads JUnit plus CTest logs even on failure. Local package inspection found both arm64 and x86_64 slices in the restored MonoGame and KNI macOS SDL/OpenAL dylibs. These checks reduce publication risk but do not substitute for executing the tests on macOS.

The macOS workflow cannot inspect the current uncommitted Windows worktree. No staging, commit, branch creation, or push has been performed. Executing this final gate requires user authorization to publish the worktree to a GitHub branch (recommended: `codex/slice1-complete`) or access to an equivalent macOS arm64 machine.

Only after that evidence passes may the plan be marked complete, ADRs 0001-0007 be reviewed for acceptance, and Slice 2 begin.
