# Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan

> **Plan status:** In progress
> **Disposition:** Active master delivery plan
> **Accepted architecture at creation:** `DPE-ARCH-0008`
> **Governing architecture:** `DPE-ARCH-0014`
> **Current milestone:** Milestone 1 — Close Slice 1
> **Started:** 2026-07-25
> **Last updated:** 2026-07-27
> **Repository:** `C:\Projects\Github\Engines\Dragon Pixel Engine`
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine`

## Objective

Complete all four slices in the Dragon Pixel Engine Design Document and deliver a stable, documented, distributable Dragon Pixel Engine `1.0.0`.

Version 1.0 means the complete design-defined workflow: create or reversibly migrate a project; visually author functional 2D and 3D scenes; build, run, debug, save, recover, package, install, update, roll back, archive, and upgrade the project on Windows, macOS, and Linux.

This plan supersedes the narrower Slice 1 and Slice 2 plans for active tracking only. It does not declare their unfinished gates complete, erase their evidence, weaken their criteria, or make work scheduled later in this plan evidence for an earlier gate.

## Superseded Active-Tracking Plans

The following remain durable historical records. Every unfinished item is carried into this plan:

- `Dragon Pixel Engine Slice 1 Complete Execution Plan.md`
- `Dragon Pixel Engine Functional Editor Slice 1 Closure and Complete Slice 2 Plan.md`
- `Dragon Pixel Inspector Tile Authoring and Scene Game View Plan.md`

## Non-Negotiable Boundaries

- Close every original Slice 1 and Slice 2 gate without reducing platform, performance, sanitizer, accessibility, or real-device requirements.
- Keep authoritative project state in the editor and route every mutation through validated commands and transactions.
- Preserve unknown and incompatible records losslessly.
- Keep project code, Python, AI, framework objects, and native plugin code outside the editor process except through accepted boundaries.
- Keep portable native modules independent of Qt, .NET, MonoGame, KNI, Unity, Python, and operating-system UI APIs.
- Keep KNI experimental until its complete .NET 10, content, input, packaging, and three-platform conformance matrix passes.
- Do not substitute synthetic rendering for scene-driven MonoGame/KNI graphics-device frames.
- Do not implement Unreal before 1.0 or broaden into the documented AAA, marketplace, visual-scripting, networking, or general-IDE non-goals.
- Do not claim `1.0.0` until every applicable acceptance and release gate has direct, durable evidence.

## Prerequisite Governance Sequence

No Slice 3 or Slice 4 implementation begins until this sequence is complete:

1. Create and hash-verify this plan in both documentation roots.
2. Append the user's Slices 1-4 and 1.0 request to both Notes copies.
3. Update both Design Document copies first to `DPE-ARCH-0009`.
4. Create or update the required ADRs and their evidence gates.
5. Update both Prompt/Result copies to `RESULT-DPE-ARCH-0009` without editing the immutable Original Prompt.
6. Update `AGENTS.md` to name this plan as the active boundary and record `DPE-ARCH-0009`.
7. Update the repository `README.md` current-phase statement.
8. Mark narrower in-progress plans as superseded for active tracking, not completed.
9. Verify all mirrored Markdown pairs, UTF-8 without BOM, LF line endings, links, and revision fields.

## Carried-Forward Slice 1 and Slice 2 Gates

The following remain open and blocking:

- Repair and pass the unchanged macOS 14+ arm64 POC B gate at 1280×720, at least 30 presented FPS, and below 100 ms median action-to-first-reflecting-frame latency.
- Run the current complete Release and native AddressSanitizer matrices on macOS.
- Run the expanded post-`DPE-ARCH-0008` matrix on Ubuntu; its latest recorded 36/36 evidence predates that revision.
- Complete current three-platform evidence for POCs A-L and their associated ADRs.
- Complete Ubuntu/macOS POC I project-component generation, loading, lifecycle, failure-containment, and editor-exclusion coverage.
- Complete POC J real Qt key-event latency, picking displacement, shortcut suppression, neutralization, and Ubuntu/macOS evidence.
- Complete POC K real tile pixels, picking, Box2D collision, dependency recovery, and Ubuntu/macOS coverage.
- Resolve and validate POC L's named-output topology, simultaneous Scene/Game frames, independent resize/revisions/picking, and three-platform recovery.
- Complete newer/incompatible-source prefab fallback, deep nesting, target-level Apply, every override form, partial Unpack, and failure-injection/startup-recovery coverage.
- Finish the Project, Scene, Selection, Command, Metadata, Asset, RuntimeSession, Diagnostics, Workspace, Prefab, and AssetPreview service split.
- Complete real designer workflows, keyboard-only operation, screen-reader roles/actions, high-DPI, high-contrast, and accessibility review.
- Complete Qt, Box2D, Jolt, managed-runtime, framework, and transitive dependency distribution inventories and required legal review.
- Keep all affected ADRs `Proposed` until their named evidence gates pass.
- Keep KNI visibly experimental until its complete support gate passes.

## Delivery Milestones

### Milestone 0: Governance and Architecture Lock

- Publish synchronized `DPE-ARCH-0009` Design, Prompt/Result, ADR index, `AGENTS.md`, Notes, and this plan.
- Define Slice 3/4 public contracts, durable formats, ownership, security boundaries, release budgets, and acceptance gates.
- Create missing ADR-0009 and ADR-0011.
- Add existing ADR-0016 and ADR-0017 to the Design Document's ADR inventory.
- Create the required Slice 3/4 ADRs and POC definitions.
- Preserve all current evidence and open failures.

Acceptance: documentation governance is internally consistent and all mirrored Markdown pairs pass the repository validator at `DPE-ARCH-0009`.

### Milestone 1: Close Slice 1

- Repair the macOS viewport/frame-pacing failure.
- Run the current source, fixtures, Release, and native-sanitizer suites on all three platforms.
- Close POCs A-D and every original Slice 1 acceptance statement.
- Review and promote only ADRs whose complete evidence gates pass.
- Record a final mirrored Slice 1 acceptance audit.

Acceptance: every original Design Document Slice 1 criterion has direct three-platform evidence; no failure is hidden by skipping an adapter, test, sanitizer, or platform.

### Milestone 2: Close Slice 2

- Complete POCs E-L.
- Finish the service-backed editor architecture.
- Finish real framework rendering, picking, embedded input, linked prefabs, physics, structured Inspector, tile authoring, Scene/Game views, and recovery.
- Pass complete 2D and 3D designer scenarios without metadata or JSON editing.
- Complete accessibility, keyboard, high-DPI/high-contrast, dependency, and distribution evidence.

Acceptance: a technical designer can assemble, validate, run, save, reopen, and revise representative 2D and 3D projects—including nested prefabs, tiles, custom C#/C++ components, and physics—through the public editor UI.

### Milestone 3: Complete Slice 3 Project Lifecycle

- Implement versioned 2D and 3D project templates.
- Implement project creation, discovery, recent projects, settings, candidate-session open, SDK/dependency validation, and format upgrades.
- Implement contained asset import, reimport, dependency tracking, cache invalidation, and safe filesystem operations.
- Complete read-only MonoGame/KNI inspection and reversible assisted migration into a new sibling project.
- Implement explicit build targets, build diagnostics, packaging, and runnable output.
- Implement safe archive/remove/restore workflows.
- Implement plugin discovery, installation, compatibility checks, permissions, enable/disable, update, quarantine, and removal.
- Implement editor/engine update discovery, staging, validation, installation, and rollback.
- Complete the bounded Unity contracts-and-bridge prototype.

Acceptance: a user can create a project, migrate representative MonoGame and KNI projects without modifying the originals, build/package them, reopen and upgrade them, recover from interrupted operations, and safely archive them.

### Milestone 4: Complete Slice 4 and Release 1.0

- Establish and pass startup, viewport, memory, save, import, build, and packaging budgets.
- Pass long-running authoring, preview, play, rebuild, crash, and worker-restart tests.
- Pass corruption, interrupted-save, interrupted-import, interrupted-build, and interrupted-update recovery tests.
- Freeze and verify public API, ABI, protocol, document, package, and plugin compatibility policies.
- Upgrade every supported public pre-1.0 fixture.
- Complete security, dependency, license, privacy, and supply-chain reviews.
- Complete keyboard, screen-reader, high-contrast, high-DPI, and platform-native accessibility validation.
- Produce installation, update, rollback, uninstallation, notices, source/relinking, and support-bundle artifacts.
- Complete tutorials, API documentation, migration guides, and reproducible reference 2D/3D projects.
- Test real representative MonoGame and KNI migrations.
- Run the final release-candidate matrix on clean supported machines.

Acceptance: every release gate passes; no open data-loss defect exists; supported formats upgrade from every public fixture; installation/update/rollback are verified; and documentation reproduces the reference projects.

## New Proof-of-Concept Gates

### POC M: Project Lifecycle and Recovery

Prove versioned template creation, contained destination validation, discovery/recent projects, settings, SDK/dependency diagnostics, upgrades, archive/restore, and injected-failure recovery without writes outside the selected target.

### POC N: Reversible MonoGame/KNI Migration

Prove read-only scanning leaves source trees byte-identical; generation occurs in staging and commits to a sibling destination; migration decisions remain explicit; generated projects build/run where supported; and rollback/removal leaves the source unchanged.

### POC O: Asset Import and Cache Integrity

Prove contained copy/reference policy, stable IDs and sidecars, deterministic cache keys, dependency/reimport behavior, missing/duplicate/cycle diagnostics, traversal and link-escape rejection, interrupted-import recovery, and real adapter consumption.

### POC P: Plugin Trust and Compatibility

Prove manifest/integrity validation, explicit capabilities, dependency/version conflicts, install/update/disable/remove, worker isolation, direct-write rejection, crash quarantine, compatibility across an engine upgrade, and package/license inventory.

### POC Q: Unity Bridge Prototype

Prove the .NET Standard 2.1 contracts-and-bridge boundary in the selected Unity 6.3 baseline through both Mono and IL2CPP-compatible paths. Verify AOT-safe DTO/metadata/command exchange and the absence of Unity, .NET Core, editor, or framework types from portable contracts.

### POC R: Package, Install, Update, and Rollback

Prove clean-machine packaging, installation, launch, sample build/run, update, interrupted-update recovery, rollback, and uninstall on Windows, macOS, and Ubuntu. Verify hashes, signing/notarization policy, artifact provenance, dependency manifests, notices, Qt relinking materials, and rollback safety.

### POC S: Version 1.0 Release Qualification

Prove the accepted performance and memory budgets, long-running stability, leak checks, repeated worker crashes/restarts, data-integrity failure injection, security review, accessibility matrix, compatibility fixtures, support-bundle privacy, and documentation-driven reproduction of the reference projects.

## Required ADR Work

Create as `Proposed`:

- ADR-0009: Plugin types, trust boundaries, distribution, and compatibility.
- ADR-0011: Unity contract boundary and bridge prototype.
- ADR-0018: Project lifecycle, templates, discovery, settings, archive, and recovery.
- ADR-0019: Reversible MonoGame/KNI migration and generated-project ownership.
- ADR-0020: Asset import, source ownership, sidecars, dependencies, caches, and filesystem safety.
- ADR-0021: Build, package, install, update, rollback, signing, and artifact provenance.
- ADR-0022: Public API/ABI/protocol/document/plugin compatibility and 1.0 versioning.
- ADR-0023: Crash reporting, support bundles, telemetry/privacy, and retention.

Review and extend ADR-0004, ADR-0006, ADR-0007, ADR-0008, and ADR-0014 where Slice 3/4 packaging, upgrades, accessibility, lifecycle commands, or build orchestration touch their decisions.

Scope acceptance alone does not promote an ADR.

## Verification Matrices

### Platform and Build Matrix

| Gate | Windows 11 x64 | macOS 14+ arm64 | Ubuntu 24.04 x64 |
| --- | --- | --- | --- |
| Strict Release | Local CI/GitFlow branch passes 56/56 in 415.61 s with valid JUnit; hosted headless runner passes 44/57 in 804.80 s with 13 product failures | Hosted strict build completes and passes 44/55 in 329.75 s; 11 product/platform failures remain | Hosted current build passes 55/55 in 155.34 s with valid JUnit |
| Native AddressSanitizer | Local CI/GitFlow branch passes 56/56 in 533.20 s with valid JUnit; hosted runner passes 44/57 in 1093.28 s with the same 13 product failures | Hosted strict build completes and passes 41/55 in 97.19 s; 14 product/sanitizer failures remain | Hosted current build passes 50/55 in 544.81 s; 5 product/sanitizer failures remain |
| POCs A-L | Partial/current Windows evidence | Complete current matrix required | Expanded I/K/L and current matrix required |
| POCs M-S | Not started | Not started | Not started |
| MonoGame | Full conformance required | Full conformance required | Full conformance required |
| KNI | Independently reported; experimental until complete | Independently reported | Independently reported |
| Package/install/update/rollback | Required | Required | Required |
| Accessibility/manual designer | Required | Required | Required |

### Cross-Cutting Verification

- Native, managed, schema, Qt interaction, conformance, and end-to-end tests.
- Strict Release and native AddressSanitizer with no unexplained skips.
- Real MonoGame and KNI device frames, picking, input correlation, and packaging reported independently.
- Deterministic format and migration fixtures for every public pre-1.0 revision.
- Atomic save/import/migration/package/update failure injection and recovery.
- Clean-machine install, upgrade, rollback, uninstall, and reference-project build/run.
- Keyboard-only, screen-reader, high-DPI, and high-contrast testing.
- Long-running memory, handle, process, cache, and authoring-state integrity tests.
- Security, dependency, license, signing, provenance, and privacy audits.
- Recursive mirror SHA-256, UTF-8/no-BOM, LF, whitespace, and local-link verification.

## Execution Record

| Date | Status | Evidence / decision |
| --- | --- | --- |
| 2026-07-26 | Managed GameObject controller increment selected | Refine the managed authoring surface around a backward-compatible `GameObjectController` base class with stable `Guid Id`, one worker-owned `Transform` (`Vector3` position/rotation/scale), protected input/time access, and author-facing `Enabled`, `Disabled`, `Update`, and `FixedUpdate` overrides. Share one runtime transform per entity across managed scripts, apply script changes only to the disposable worker scene used by rendering and picking, discard them on Stop/reload/crash, generate a WASD/arrow-key mover that consumes the existing `move.x`/`move.y` actions and the Inspector `Speed` field, and remove the implementation-oriented Lifecycle row from Inspector. Preserve legacy `IProjectComponent` and `IProjectComponentLifecycle` binaries and the editor-process exclusion boundary. Record the public-contract refinement in synchronized `DPE-ARCH-0012`, ADR-0014, Prompt/Result, and focused managed/editor/runtime tests before implementation is considered complete. |
| 2026-07-25 | Started | User expanded the active goal from Slice 1-2 completion to all four slices and a fully functional, design-defined `1.0.0`. Existing gates and non-goals remain unchanged. |
| 2026-07-25 | Baseline verified | Fresh Windows 11 x64 strict Release passed **42/42 tests in 192.18 seconds**. Fresh MSVC AddressSanitizer passed **42/42 tests in 222.45 seconds** with no sanitizer report. These are one-platform regression results only; they do not close Slice 1 or Slice 2, satisfy macOS/Ubuntu gates, promote KNI or any ADR, or validate POCs M-S. |
| 2026-07-25 | Governance completed | Published synchronized `DPE-ARCH-0009` Design (SHA-256 `C02AD39F673AAFAA72D56CBD3B976FE6BB86EE9F6BDAFFCD451718863EE72A6D`), `RESULT-DPE-ARCH-0009` Prompt/Result (SHA-256 `3139F6F87CAF9D73F9A1EBF6B9035822505DB74D12A9F928C0E707E0D908E1DF`), Notes intake, 23 Proposed ADRs, ADR index, `AGENTS.md`, README phase statement, and active-plan handoffs. `scripts/docs/Test-DocumentationMirrors.ps1` passes all 47 UTF-8/LF Markdown pairs and revision fields; `git diff --check` passes. |
| 2026-07-25 | Next increment selected | Preserve the green baseline while repairing the highest-risk remaining Slice 1/2 correctness gaps: candidate project-open isolation, multi-document save atomicity/recovery, and component-generation transactionality, each with failure-injection regression coverage before broader UI or Slice 3 work. |
| 2026-07-25 | Project/save foundation hardened | Candidate project open validates the originally selected location before recovery and preserves the active session on failure. Project indexing enforces one portable path/alias registry across roots, documents, and asset sources and constrains `startupScene` to a declared scene root. Native multi-file saves now use a SHA-256-bound crash journal with injected rollback/startup-recovery coverage. Remaining filesystem risks are handle pinning, hard-link identity, link-swap/enumeration races, metadata-preserving replacement, and one skipped link fixture when the host cannot create it. |
| 2026-07-25 | Prefab and component runtime hardened | Prefab coverage now includes missing/newer/incompatible fallback, cycle/depth/entity guards, injected Apply rollback, startup recovery, and complete unpack. Project component generation requires an explicit declared root when ambiguous and binds cache identity to exact .NET/CMake/compiler/generator/build-program inputs. Worker-only loading validates exact manifests, placement, platform/architecture, contracts/tools/artifact hashes, link safety, lifecycle cleanup, atomic factory publication, immutable input, and missing enabled factories. Native loader races, cache publication, relocation/absolute paths, metadata validation, UI responsiveness, ABI parity, platforms, and full designer workflows remain open. |
| 2026-07-25 | Full-state Play input implemented | The Game view emits complete focus/capture/action snapshots with press/release counters and repeat deduplication. Protocol-v2 updates validate before commit, track command/input revisions separately, reject stale/duplicate/future/malformed correlations, expire unreflected inputs, and fail closed through Play-worker restart plus neutral acknowledgement. `InputMotion2D` changes real MonoGame/KNI pixels and retained-ID picks without mutating authoring JSON. Aggregate Qt key-event-to-first-reflecting-Qt-paint timing, complete shortcut evidence, `Axis2D` exercise, and Ubuntu/macOS runs remain open. |
| 2026-07-25 | Windows viewport performance repaired | The worker keeps origin-derived absolute deadlines and uses a 64 Hz target lattice. At 1280×720, combined POC B measures MonoGame at 64.1 FPS and KNI at 32.1 FPS; the unchanged acceptance threshold remains at least 30 FPS. The persistent frame reader retries transient seqlock write windows, and the retained real ID target is invalidated only by render-affecting state. Three consecutive focused POC B runs passed. |
| 2026-07-25 | Current Windows Release verified | The supported Release build completes with zero managed build warnings/errors and all native targets built. The authoritative CTest log records **45/45 passed**, zero failed markers, and 294.56 seconds of test execution. It includes two complete registrations of the 20-function Qt interaction executable, focused project/component/prefab/input recovery, real-device MonoGame/KNI rendering, and Python automation. |
| 2026-07-25 | Sanitizer harness diagnosis | The refreshed ASan build succeeded, but the first 45-test run was 43/45 because both aliases of the Qt interaction executable hit the 90-second CTest timeout. Direct ASan execution passed with exit code 0 in 93.158 seconds. Sanitizer builds now receive a bounded 180-second allowance while ordinary builds retain 90 seconds; the clean rerun is recorded next. This changes only the harness allowance, not a product performance or acceptance threshold. |
| 2026-07-25 | Current Windows ASan verified | After regenerating the sanitizer build, both interaction registrations advertise the intended 180-second cap. `ctest --preset windows-asan --output-on-failure` passes **45/45 in 368.80 seconds**; `s2.editor_interactions` completes in 95.78 seconds and `poc_h.qt_interactions` in 91.47 seconds. The supporting ASan build completes successfully in 31.425 seconds. |
| 2026-07-25 | POC J next increment specified | The existing GUI monotonic clock and exact-reflected-frame path can support the missing gate. Capture time at accepted Qt key-handler entry, join input revision/frame revision/latency at one post-`QPainter::end()` timestamp, and run a dedicated per-adapter 21-sample 1280×720 test. Report p50/p95/max, drops/expiry/skipped paints, and actual Qt-painted FPS; enforce only the existing p50 below 100 ms and FPS at least 30. Keep POC B viewport-command values separate and do not include pick RPC time because the current retained-pick RPC is not exact-frame-pinned. |
| 2026-07-25 | Handoff remains open | Milestone 1 is not accepted. The next blocking work is the dedicated POC J Qt-event-to-reflecting-paint measurement and current Ubuntu/macOS matrices—especially repair/verification of the historical macOS 1280×720 POC B failure. Slice 2 still requires its complete designer, service, prefab, physics, tile, multi-view, accessibility, and distribution evidence; Slices 3/4 and POCs M-S remain open. |
| 2026-07-25 | Durable record verified | Updated Design, Prompt/Result, affected ADRs, prototype evidence, milestone correction, and this plan in both documentation roots. Repaired the mirror validator's empty-`PSScriptRoot` default-parameter invocation on Windows PowerShell. The default validator passes all **47 UTF-8/LF Markdown pairs at DPE-ARCH-0009**, and `git diff --check` passes (Git emits only existing autocrlf conversion warnings). No files were staged, committed, or pushed. |
| 2026-07-26 | Fluid workspace and component-lifecycle increment selected | The user reported that the center workspace prevents practical panel placement and requested attachable script components with an explicit lifespan. Full-grid docking and direct script attachment implement the existing Qt/component boundaries; the backward-compatible managed lifecycle extension and native ABI v2 are recorded as the new synchronized `DPE-ARCH-0010` public-contract refinement. Remove the reserved center obstruction; enable nested, tabbed, floating, animated dock placement across the full main-window grid; provide deterministic reset/save/restore behavior; and prove it through Qt interaction tests. Close the current project-component lifecycle gap through ordered worker-only `Create`, `Enable`, fixed update, variable update, late update, render submission, `Disable`, and `Destroy` dispatch while preserving legacy compatibility, authoring records, and editor-process exclusion. The intermittent Windows atomic-publication recovery defect remains open and must still be resolved before authoritative full matrices. |
| 2026-07-26 | Fluid workspace and direct script attachment implemented | Removed the reserved central widget and made every built-in panel an all-area movable/floatable/closable dock with nested splits, tabs, grouped drag, animation, deterministic reset, and version-3 save/restore fallback. Add Component now exposes New C# Script and New C++ Component even when existing descriptors are already attached; successful contained generation refreshes JSON metadata and attaches the new type to all selected GameObjects in one validated command transaction. The complete `s2.editor_interactions` alias passes in Release at 84.73 seconds and MSVC AddressSanitizer at 111.73 seconds. |
| 2026-07-26 | Full project-component lifespan implemented | Added the optional managed `IProjectComponentLifecycle` and size-tagged native `dpe_component_plugin_v2` while retaining `IProjectComponent` and native-v1 compatibility. Worker dispatch is ordered across enable, bounded 60 Hz fixed, variable, late, render, disable, and destroy; catch-up is capped at four with a drop diagnostic. Managed/native stage failures are isolated and disabled before cleanup. Runtime aliases pass Release at 0.20/0.19 seconds and ASan at 0.51/0.48 seconds; generator aliases pass Release at 25.48/23.98 seconds and ASan at 26.62/24.28 seconds. The managed build completes with zero warnings and errors. |
| 2026-07-26 | Windows managed-host ASan discovery repaired | The focused ASan runtime aliases initially exposed that `dotnet.exe` could not discover the MSVC AddressSanitizer runtime for dynamically loaded native component plugins. Managed ASan tests now prepend the compiler runtime directory through CTest environment modification. Both failed aliases pass after regeneration; no product threshold or acceptance gate changed. |
| 2026-07-26 | Current handoff | DPE-ARCH-0010's Windows editor/lifecycle increment is implemented and focused Release/ASan evidence is green. POC I and ADR-0014 remain Proposed because Ubuntu/macOS, package/relocation, and full Stop/reload/crash evidence are incomplete. The next blocking Windows work is the intermittent atomic multi-document publication rollback defect; repair it before claiming a new complete Release/ASan matrix, then resume POC J and current Ubuntu/macOS matrices. |
| 2026-07-26 | Durable evidence verified | The synchronized Design hash is `497E246E24FCAD41DF1FDA2E113D6D689CF094B744CDEBFB949B3598176A8A4B`, Prompt/Result hash is `516FE3F89821AA9634F66D7599E1393534A55CDD3783EDBB95012021E36AF656`, and ADR-0014 hash is `1C43C4CC2613372B3F0FEEEBAA1989B5B3798A3425D128ED6823FCA9FF146B81`. The recursive validator passes all 47 UTF-8/LF Markdown pairs at `DPE-ARCH-0010`, and `git diff --check` passes. No files were staged, committed, or pushed. |
| 2026-07-26 | Production editor iteration increment selected | Add a narrow incremental Windows Release-editor build instead of forcing the complete repository/test matrix on every UI iteration. Emit a stable production-style developer bundle containing `DragonPixelEditor.exe`, Qt runtime/plugins, managed MonoGame/KNI workers, native runtime dependencies, Python tooling, schemas, and the sample. Resolve bundled runtime assets relative to the executable with source/build fallbacks for existing tests. Provide one-command build-and-launch plus explicit no-build relaunch, validate the bundle manifest/layout and a packaged self-test, and do not characterize this developer bundle as the signed/relocatable POC R release package. |
| 2026-07-26 | Production editor developer bundle completed | `scripts/dev/Build-Production-Editor.ps1` now performs a narrow Release-editor build, staged deployment, Qt runtime/plugin deployment, bundled native runtime plus MonoGame/KNI workers/contracts/Python/schemas/sample, relative-path SHA-256 manifest generation, required-layout validation, packaged MonoGame self-test, safe bundle replacement, and optional launch. `Launch-Editor.ps1 -Production` launches the stable output against the writable development sample. The production executable is `out/product/windows-x64/DragonPixelEditor/DragonPixelEditor.exe`; its manifest records 131 files and matches the executable SHA-256 `a7897834611819b5d0c9cadfd7410e2e26dbfae9bbddd1d8be6d3008dad64bb6`. |
| 2026-07-26 | Production editor verification | The packaged MonoGame self-test exits zero. Registered `s1.editor_monogame` passes in 7.50 seconds and the Windows PowerShell 5.1 path-with-spaces launcher proof passes in 1.28 seconds. Runtime asset resolution prefers executable-relative bundle paths and retains source/build fallbacks for existing development/test flows. After preserving and regenerating a corrupt `.ninja_deps` cache, the unchanged-tree `-Fast` build/deploy path improves from 37.87 seconds to 6.05 seconds without the prior Ninja warning. The interactive production executable was launched successfully and remained active through the launcher probe. |
| 2026-07-26 | Production editor handoff | For a validated build and launch use `Build-Production-Editor.ps1 -Launch`; after that, close the editor and use `Build-Production-Editor.ps1 -Fast -Launch` for iterative changes, or `Launch-Editor.ps1 -Production -SkipBuild` to relaunch without compiling. The script refuses to replace a running bundle so it cannot terminate unsaved editor work. This developer bundle does not close ADR-0021/POC R: signed installers, clean-machine relocation, absolute-path scanning, compliance materials, updates/rollback/uninstall, and three-platform evidence remain open. The overall next milestone blocker remains atomic-publication recovery before a new full Windows matrix. |
| 2026-07-26 | Usable authoring/onboarding increment selected | Continue the accepted Slice 2 workflow with a dockable Getting Started surface, one-click square and circle sprite creation through the existing transactional Sprite preset, and clearer component/script authoring affordances. Generated C# and C++ sources will expose Unity-familiar `Start`, `OnEnable`, `OnDisable`, and `OnUpdate` hooks while retaining the accepted `IProjectComponent`/`IProjectComponentLifecycle` and native ABI v2 contracts. Script metadata fields remain the Inspector-authoritative editable values; source generation/build/execution remains contained and worker-only. Add focused Qt, native command/round-trip, managed rendering/picking, and generator verification without claiming a new full Windows matrix or closing POC I/J. |
| 2026-07-26 | Usable authoring/onboarding increment completed | Added a movable/closable Getting Started dock with accessible one-click Square, Circle, Add Component, New C# Script, New C++ Component, Play, and Scene View handoff actions. Square and Circle reuse the validated Sprite preset with `builtin://square` and `builtin://circle` bindings, unique names, automatic selection, editable Transform/Sprite Inspector fields, Undo/Redo, save/reopen compatibility, and real MonoGame/KNI device rendering. Sprite scale and Z rotation now affect device pixels; circle transparency also shapes the retained ID-buffer pick instead of exposing a square hit area. Project script cards show their contained source and `Start`/`OnEnable`/`OnUpdate`/`OnDisable` mapping while keeping exposed metadata fields editable before build. New C#/C++ sources provide those user-facing hooks and map them onto the unchanged compatible managed lifecycle/native ABI v2 dispatch. |
| 2026-07-26 | Usable authoring focused verification | The targeted Windows Release build completes with zero managed warnings/errors. Sequential Release tests pass **5/5 in 123.75 seconds**: `s1.native_core` 0.38 s, MonoGame POC E 9.98 s, KNI POC E 10.13 s, `s2.project_component_modules` 20.25 s, and `s2.editor_interactions` 83.00 s. The matching MSVC AddressSanitizer tests pass **5/5 in 149.62 seconds**: 0.64 s, 11.02 s, 12.59 s, 22.45 s, and 102.90 s respectively. The rebuilt production-style developer bundle contains 131 manifest entries; its executable SHA-256 is `D9DB14BCD5D4F56E84BC45FAB700F1EA2621B5C05733AB6BDE5FAC8A07C43349`, manifest SHA-256 is `14E45F397E991163C58AB4BF53793FB122D6B0FD33DDD014DC47762CC10E232F`, and its packaged MonoGame offscreen self-test exits zero. `git diff --check` passes. |
| 2026-07-26 | Usable authoring handoff | This is focused Windows implementation evidence, not a new authoritative full matrix. It does not close POC I/J, Slice 1/2, KNI support, or an ADR. The previously recorded intermittent atomic-publication recovery defect still blocks a new full Windows matrix; macOS/Ubuntu, aggregate Qt input-to-paint timing, complete designer/accessibility, and all later-slice gates remain open. |
| 2026-07-26 | Usable authoring durable record verified | `scripts/docs/Test-DocumentationMirrors.ps1` passes all **47 UTF-8/LF Markdown pairs** with matching `DPE-ARCH-0010` revision fields after this handoff was recorded. The active plan copies are byte-identical, `git diff --check` passes, and no files were staged, committed, or pushed for this increment. |
| 2026-07-26 | Rider script-editing increment selected | Add a bounded external-editor workflow for project component sources without turning Dragon Pixel Editor into an IDE or loading project code into its process. Index contained C#/C++ component source files in Project Explorer, generate a disposable Rider solution/project from the current validated component roots, open a selected source through Rider from Project Explorer activation and an Inspector component-card context action, and make newly generated sources appear immediately. Prefer an explicit `DPE_RIDER_EXECUTABLE` override, then the JetBrains Toolbox launcher/PATH, then contained platform-specific installation discovery. Reject missing, linked, or project-escaping source paths; use detached launch and structured diagnostics. Record the public authoring/process refinement as synchronized `DPE-ARCH-0011`, update ADR-0014, and prove indexing, workspace generation, containment, launch argument routing, and Qt interaction on focused Windows Release/AddressSanitizer evidence without claiming the full matrix or closing POC I. |
| 2026-07-26 | Rider script-editing increment completed | Project indexing now publishes stable contained `.cs`, `.cpp`, `.cc`, `.cxx`, `.h`, `.hpp`, and `.dpecomponents` entries under declared component folders. Project Model exposes Scripts / Components filtering and actual filenames. `ScriptEditorService` atomically regenerates `.dragonpixel/Ide/Rider/DragonPixel.ProjectComponents.sln` and its `.csproj`, includes all validated sources/manifests, references `DragonPixel.Contracts` for C#, rejects missing/outside/linked sources, discovers configured/PATH/Toolbox/platform Rider installations, and routes detached argument-vector opens for the solution and selected `--line 1` file. New C# Script refreshes the row immediately; source-row activation and Inspector **Edit Script in Rider**/**Edit Component Source in Rider** use the same service. The editor still never loads or executes project code. |
| 2026-07-26 | Rider source-authoring focused verification | The focused Windows Release build completes with zero managed warnings/errors. One CTest invocation passes **5/5 in 83.89 seconds**: project index 0.63 s, Project Model 0.04 s, script-editor service 0.07 s, duplicate POC I alias 0.04 s, and complete Qt interactions 83.09 s. The matching MSVC AddressSanitizer invocation passes **5/5 in 104.67 seconds**: 2.03 s, 0.13 s, 0.10 s, 0.09 s, and 102.31 s. Tests cover deterministic regeneration/item ordering, contracts/source/manifest inclusion, explicit discovery precedence, missing/outside/link rejection, exact launch arguments, immediate generated-source visibility, Project activation, and Inspector right-click using an injected launcher so Rider is not opened by automation. The build Release editor SHA-256 is `F286B05989E9C81DDF6F3B081A115419058AA59779E3EC3EBBF9BD5B64E751EA`; the installed Rider product version is `261.25134.178.0-RD`. |
| 2026-07-26 | Rider source-authoring bundle handoff | The production-style bundle was not overwritten because its existing editor process was still running at `out/product/windows-x64/DragonPixelEditor/DragonPixelEditor.exe`; the safety guard preserves possible unsaved authoring work. That running bundle retains its prior executable hash `D9DB14BCD5D4F56E84BC45FAB700F1EA2621B5C05733AB6BDE5FAC8A07C43349` and does not contain this increment. After closing it, run `scripts/dev/Build-Production-Editor.ps1 -Fast -Launch` to deploy and smoke-test the verified Release editor. This packaging deferral does not affect the green build-tree executable or focused tests and does not close POC R. |
| 2026-07-26 | Rider source-authoring durable record verified | Published synchronized `DPE-ARCH-0011` Design, `RESULT-DPE-ARCH-0011` Prompt/Result, ADR-0014 refinement, AGENTS guardrail, README workflow, and this handoff. Design SHA-256 is `E50628ABE9DA9A4599191C51030BB6421D4EBB4BBAB2B8B8F393183448AE9E2E`, Prompt/Result SHA-256 is `877112809C322DEC218B6B641B2C1728BEADD594905B0607DE28D295B1EBB875`, and ADR-0014 SHA-256 is `BDA5D127EFBDE5528E712D65E9980122DDE0D01F6A1BE0483D5FCF11007B4AE3`. The recursive validator passes all **47 UTF-8/LF Markdown pairs** at `DPE-ARCH-0011`; the active-plan copies are byte-identical and `git diff --check` passes. No files were staged, committed, or pushed. |
| 2026-07-26 | Managed GameObject controller increment completed | Added `IGameObjectControllerLifecycle`, abstract `GameObjectController`, `GameObjectInput`, and a thread-safe worker-owned `Transform` with `Vector3` position/Euler rotation/scale. The base maps existing `IProjectComponent`/`IProjectComponentLifecycle` dispatch to parameterless `Enabled`, `Disabled`, `Update`, and `FixedUpdate`, binds `Guid Id`, and exposes current input/time without changing legacy binaries. The runtime shares one transform per managed entity, applies dirty values after physics to the disposable render/pick scene, and resets them on Stop. Generated scripts and the user's exact bundled `MyMover.cs` consume WASD/arrow-backed `move.x`/`move.y`, normalize diagonals, parse Inspector Speed, and translate by delta time. Inspector no longer shows Lifecycle; Script Source, Speed, and Rider actions remain. |
| 2026-07-26 | Managed controller focused verification | Managed Release builds finish with zero warnings/errors. Focused Release results pass both runtime aliases at 0.20/0.20 seconds, both generator/build aliases at 23.31/20.21 seconds, and the registered full Qt interaction alias at 83.13 seconds; an initial cold combined invocation hit the existing 90-second UI cap without an assertion, while direct execution passed in 83.1 seconds and the warm registered rerun passed. Matching MSVC AddressSanitizer evidence passes **5/5 in 143.15 seconds**: 0.49 s, 0.54 s, 20.65 s, 20.18 s, and 101.27 s. Tests cover GUID/lifecycle/input/time behavior, runtime transform composition/reset, legacy compatibility, generated mover source and isolated compilation, and Inspector lifecycle omission. |
| 2026-07-26 | Writable project and bundle verified | The exact writable `MyMover.cs` builds against the deployed `DragonPixel.Contracts.dll` with zero warnings/errors. The first bundle refresh exposed that recursively deleting the output can partially damage a bundle while Rider holds its workspace; Rider was not terminated and project source survived. Publication now preserves the existing writable sample by default and updates staged engine/runtime payloads in place; `-ResetSample` is the explicit destructive reset. The repaired bundle verifies all **157** manifest file hashes, includes `MyMover` SHA-256 `58FA9AB06D5358CE1BF1AB877707DDDC8C76B48B5F516F57EA498F2495B49B60`, contains editor SHA-256 `722BDD16E3A744C4656216D3BDC749CAE9006C8D8F666AAFF13BC19F5E405510`, and passes the packaged MonoGame offscreen self-test. |
| 2026-07-26 | Managed controller handoff | This completes the focused Windows DPE-ARCH-0012 implementation increment, not POC I or a slice. A dedicated actual-device managed-controller pixel/pick proof, complete Stop/reload/crash lifecycle evidence, native transform-control parity, package relocation, Ubuntu/macOS runs, the atomic-publication defect, aggregate POC J Qt-paint timing, and all later release gates remain open. |
| 2026-07-26 | Managed controller durable record verified | Published synchronized `DPE-ARCH-0012` Design, `RESULT-DPE-ARCH-0012` Prompt/Result, ADR-0014 refinement, Notes intake, AGENTS guardrail, README phase statement, and this handoff. Design SHA-256 is `D289CB0F3B0149A71CC3B1161D8ACD0B26436F0857169D85F20CB6CE23D54EB4`, Prompt/Result SHA-256 is `8B40D2DACE292A4C15C1B2B35BAF73A76F30B13906EE67CB96813FA3DB44EE9A`, and ADR-0014 SHA-256 is `320D9F647E5390CC16BA1D7C25DC1A00488A5A22D14F696ED7E8C946206EE3D1`. The recursive validator passes all **47 UTF-8/LF Markdown pairs** at `DPE-ARCH-0012`; `git diff --check` passes with only existing autocrlf notices. No files were staged, committed, or pushed. |
| 2026-07-26 | Configurable input-map increment selected | Replace the Game view's hard-coded key table with a project-owned `dpe.inputmap` v1 asset containing named control maps, device-neutral actions, and stable keyboard/mouse/standard-gamepad bindings. Add validated atomic rebinding through an Input Map editor, Qt keyboard/mouse capture, SDL 3 hot-plug-aware gamepad polling, lifecycle neutralization, and a compatibility fallback. Keep `MyMover` and `InputMotion2D` on the same existing `move.x`/`move.y` actions and retain the full-state worker protocol. Record the contract as synchronized `DPE-ARCH-0013` and expand ADR-0015/POC J without claiming slice closure. |
| 2026-07-26 | Configurable input-map implementation complete | Added `dpe.inputmap` v1 validation/canonical serialization/unknown-field preservation, compare-before-write atomic saves, canonical keyboard/mouse/standard-gamepad paths, deterministic evaluation, and compatibility fallback. Replaced the Game view's hard-coded keys with Qt keyboard/mouse capture plus SDL 3.4.12 gamepad polling, lifecycle and transient neutralization, and dynamic action snapshots. Added an Input Map dialog and Project Explorer activation for named map/action/binding editing and rebinding. The sample `DefaultGameplay` map drives the same `move.x`/`move.y` actions consumed by `InputMotion2D` and the user's preserved `MyMover.cs`. |
| 2026-07-26 | Configurable input-map focused verification | Five focused Windows Release aliases pass 5/5 in **94.67 seconds** and five MSVC AddressSanitizer aliases pass 5/5 in **116.79 seconds**: project-index discovery, Game-view custom rebinding/mouse/transient behavior, input-map service validation/recovery/evaluation, the POC J rebinding alias, and complete editor interaction including the dialog. SDL 3.4.12 resolved through the pinned vcpkg baseline. |
| 2026-07-26 | Configurable input-map bundle verified | The production-style editor bundle passes its packaged MonoGame offscreen self-test and all **179** manifest records hash-verify. Editor SHA-256 is `4C63A11EB7E05F9DE4C60544104900C0DF276CA025211567C51E02028AB4F73D`; manifest SHA-256 is `F381D9FA4FB679740377DA4098E429DF0A1893C4C0BCDE3E106AE065AACC1363`; default map SHA-256 is `2D54F4FC49A327BF3FBE4A5F1B7B654B5C398568A7A9770BD9553C5EEC8EEED4`; preserved `MyMover.cs` SHA-256 is `58FA9AB06D5358CE1BF1AB877707DDDC8C76B48B5F516F57EA498F2495B49B60`; bundled `SDL3.dll` SHA-256 is `50553284F985A32A18EA95BA25DE47F44A0B6E3629B214080C7BEEB8C44A235D`. |
| 2026-07-26 | Configurable input-map handoff | This completes the focused Windows DPE-ARCH-0013 implementation increment, not POC J or a slice. Physical standard-gamepad/hot-plug evidence, aggregate real Qt input-to-first-reflecting-paint timing, current Ubuntu/macOS runs, and complete correlated device-pixel/pick evidence remain open. ADR-0015 remains `Proposed`; KNI remains experimental. |
| 2026-07-26 | Configurable input-map durable record verified | Published synchronized `DPE-ARCH-0013` Design, `RESULT-DPE-ARCH-0013` Prompt/Result, ADR-0015 implementation evidence, Notes intake, AGENTS guardrail, README phase statement, and this handoff. Design SHA-256 is `8023B67100B0ECF8E606394DF30D766338C2C24337FC6B4F84CE5723365772BC`, Prompt/Result SHA-256 is `5FE29BA04D456568A4B1B68AAC77C87B053307D5A61D416F4A18CD667CE7B481`, and ADR-0015 SHA-256 is `89BB175FA04D1A890F6B0B1BCC53E78FAAF1D5577414DBBA41C03DB4B47CFAD1`. The recursive validator passes all **47 UTF-8/LF Markdown pairs** at `DPE-ARCH-0013`; all four new JSON inputs parse, and `git diff --check` passes. No files were staged, committed, or pushed. |
| 2026-07-26 | Inspector and input-settings follow-up selected | Improve Inspector entity/component checkbox contrast without changing enabled-state commands, expose generated C# movers with the same editable Horizontal Action, Vertical Action, and Speed setup as `Input Motion 2D`, remove per-frame sample console noise, and add a clear **Edit > Project Settings > Input...** route to the existing validated input-map editor while retaining the Assets and Project Explorer routes. Verify generated metadata/source, isolated managed execution, Inspector rendering/interaction, input settings access, focused Release/ASan aliases, writable sample preservation, and the production bundle. This is implementation refinement within DPE-ARCH-0012/0013, not a new architecture revision. |
| 2026-07-26 | Inspector and input-settings follow-up implemented | Added embedded 18-pixel high-contrast checked and mixed-state graphics for Inspector GameObject/component indicators. Added **Edit > Project Settings > Input...** while retaining Assets and Project Explorer access; the dialog now exposes control-map enabled state, action rename, and binding path/scale/dead-zone editing. Generated C# movers and the canonical sample `MyMover` now expose Horizontal Action, Vertical Action, and Speed like `Input Motion 2D`, consume the configured actions, normalize diagonals, move the shared Transform by delta time, and omit per-frame console output. New C# creation schedules `ComponentModuleService` build automatically so attached scripts become worker-executable. |
| 2026-07-26 | Exact MyMover and focused matrices verified | Added a permanent .NET test that compiles and executes the exact sample `MyMover.cs` with custom `player.strafe`/`player.climb` actions and proves GUID binding, configured fields, enabled/disabled lifecycle, expected Transform displacement, and shutdown reset. The focused Windows Release matrix passes **10/10 in 133.55 seconds** and MSVC AddressSanitizer passes **10/10 in 157.17 seconds**; final UI reruns pass in **87.61** and **108.35 seconds**. Generated source/metadata, isolated component runtime, input evaluation/rebinding, Inspector resources, Input Settings actions/dialog, and component compilation are covered. |
| 2026-07-26 | Inspector/input production bundle verified | The writable sample project compiles with **zero warnings/errors**. The production-style bundle passes its packaged MonoGame offscreen self-test; all **183** non-manifest files are listed, no files are unlisted, and every record hash matches. Editor SHA-256 is `98F8B01EE04395CC6EDAD2C507BE18B8C972E58C231369A67156D16833DFE6C9`; manifest SHA-256 is `F6D746C0558B5FD593B8058D8328AE3DFA336BBD31C9A7E693ABCFDA702CB6B2`; deployed and repository `MyMover.cs` are byte-identical at SHA-256 `BDF5110FFAEF7353E2E1783DB03756604422B7984C0FA63E4F7FE26295945845`. The default input map and all three MyMover Inspector properties are present. |
| 2026-07-26 | Inspector/input-settings follow-up handoff | The requested focused Windows usability/runtime work is complete within DPE-ARCH-0012/0013. Physical standard-gamepad/hot-plug runs, aggregate Qt input-to-first-reflecting-paint timing, current Ubuntu/macOS matrices, the intermittent atomic-publication defect, complete POC J, slice closure, and KNI promotion remain open. |
| 2026-07-26 | Inspector/input-settings durable record verified | Published synchronized Design, Prompt/Result, ADR-0015, Notes intake, ADR index, this plan, and updated repository status at unchanged revision `DPE-ARCH-0013`. Design SHA-256 is `B7EAB913D000FF7333B553F3433960EF7F33A156B2B1F83A8CD58741A435AF56`, Prompt/Result SHA-256 is `E74E99DE954B2F7E839663DE36EB193643E5E19A2B728931DDBC337E48BC1386`, and ADR-0015 SHA-256 is `1B47830561F6E6161947E834576FC0422EF35A1F658F891D8D198DFE5E093176`. The recursive validator passes all **47 UTF-8/LF Markdown pairs** at `DPE-ARCH-0013`; sample input-map/manifest JSON parses; `git diff --check` passes with autocrlf notices only. No files were staged, committed, or pushed. |
| 2026-07-27 | DPE-ARCH-0014 authoring workflow selected | Activated the mirrored `Dragon Pixel Engine New Project Asset Workflow Prefab Hierarchy and Multi-Inspector Plan.md` for a Project Hub, minimal declarative 2D/3D creation, clean scenes, project-v4/template-v1, asset-v3 and a two-pane Project Browser, linked-prefab drag paths, ordered Hierarchy multi-operations, and multiple lockable Inspectors. Published synchronized Design/Prompt/Result/ADR governance at `DPE-ARCH-0014`; the recursive validator passes all 48 Markdown pairs. This advances POCs F/H/M/O without closing them. |
| 2026-07-27 | DPE-ARCH-0014 focused Windows implementation complete | Implemented the Project Hub and staged minimal project/clean-scene lifecycle, project-v4/template-v1/asset-v3 compatibility, recoverable asset operations and two-pane Project Browser, immutable PNG/JPEG worker bindings, versioned domain drag/drop and linked-prefab creation, atomic ordered Hierarchy multi-operations, and multiple independently lockable mixed-value Inspectors. Focused Release aliases pass 18/18; the complete Release editor interaction test passes in 121.29 seconds; focused sanitizer aliases pass 9/9 and the five new interaction functions pass 7/7 in 59.97 seconds. The 188-record production-style bundle hash-verifies, passes its packaged MonoGame self-test, and creates/opens a minimal project outside the source tree. The aggregate sanitizer interaction test remains inconclusive at its 480-second harness timeout; the atomic-publication defect and current Ubuntu/macOS matrices remain open, so no POC, ADR, slice, release, or KNI gate is promoted. |
| 2026-07-27 | Feature-development constitution workflow selected | Activated the mirrored `Dragon Pixel Engine Development Constitution Workflow Setup Plan.md` to install the supplied GitFlow constitution, handbooks, Codex prompts, and GitHub feature templates without beginning the next product feature. The workflow requires current `develop`, one focused `feature/*` branch, a mirrored feature plan before source changes, small tested commits, focused and final verification, aggregate diff review, a pushed draft PR into `develop`, and an explicit stop before merge. Existing DPE-ARCH-0014 architecture, mirror, evidence, and acceptance gates remain unchanged. |
| 2026-07-27 | Atomic-publication Phase 1 ready for review | On `feature/atomic-publication-recovery`, native multi-document save now resolves prior journals before staging, serializes cooperating same-root save/recovery callers, separates Windows topology probes from replacement attempts, revalidates expected hashes and owned rollback prefixes, reconciles exact canonical journal bytes, and retains safe prepared/committed candidates for deterministic startup rollback. Final focused stability passes 100/100 Release and 50/50 ASan repetitions. Complete Windows builds pass, followed by **56/56 strict Release in 423.22 seconds** and **56/56 MSVC ASan in 560.43 seconds**; the two formerly inconclusive aggregate sanitizer interaction aliases pass in 181.13 and 179.14 seconds without timeout changes. This clears the recorded Windows matrix blocker on the feature branch but does not close POC F/J/M/O, an ADR, a slice, Ubuntu/macOS evidence, release acceptance, or KNI support. ADR-0006 handle-pinning/noncooperating-writer and unexecuted current POSIX-path hardening remain explicit. |
| 2026-07-27 | Atomic-publication feature merged | Human review completed and PR #2 merged into `develop` at `17e481d700096e491698f597d8590988042b8203`. A fresh fetch confirms local and remote `develop` match. The 56/56 Windows Release and ASan results are now the integration baseline; the recorded ADR-0006 and current POSIX limitations remain open. |
| 2026-07-27 | Editor library modularization implemented | On `feature/editor-library-modularization`, the shared Qt editor implementation/resources, native/Qt/SDL dependencies, runtime definitions, and adapter/C ABI ordering compile once through `DragonPixelEditorLibrary`/`DragonPixel::Editor`. The production and interaction executables retain only their unique entry points. The complete Release build passes in 58.45 seconds and focused editor tests pass 5/5 in 284.03 seconds; the complete MSVC ASan build passes in 44.48 seconds and focused editor tests pass 5/5 in 403.70 seconds. Both presets retain 56 registered tests. The normal 188-record production bundle hash-verifies and passes its packaged MonoGame self-test. This internal build refactor changes no engine contract and promotes no POC, ADR, slice, platform, release, or KNI claim. |
| 2026-07-27 | Editor-library PR CI platform blockers recorded | PR #3 CI passes Windows Release and MSVC ASan. Both macOS configurations fail before the editor target on 16 unchanged Metadata aggregate-initializer warnings under AppleClang 17 `-Werror`; both Ubuntu configurations fail before project configuration because the vcpkg `libxcrypt` build cannot find the `Ninja Multi-Config` program. These runs do not validate or invalidate the new editor boundary on POSIX and do not close a platform matrix. |
| 2026-07-27 | Editor library modularization merged | Human review completed and PR #3 merged into `develop` at `b3281fa1820a1fa25bad6cb270a3aebbf024d3f7`. The single-compilation editor boundary is now the integration baseline; its recorded platform CI failures selected the next bounded feature. |
| 2026-07-27 | CI/GitFlow integration active | Created `feature/ci-gitflow-integration` directly from current `develop`. The workflow now validates GitFlow relationships, propagates native-command failures, resolves Windows Ninja explicitly, requires at least 56 passing JUnit cases, adds the proven Ubuntu autotools prerequisites, and replaces partial built-in Metadata aggregates with named assignments. Local Windows Release passes 56/56 in 410 seconds and MSVC ASan passes 56/56 in 551 seconds; actionlint, 10 portable policy/evidence tests, PowerShell failure propagation, and focused registry tests pass. Docker execution is inconclusive because the local daemon is unavailable. Hosted feature-PR evidence remains pending and no platform, POC, ADR, slice, release, or KNI claim is promoted. |
| 2026-07-27 | CI/GitFlow integration ready for review | Draft PR #4 now has truthful, platform-separated evidence from run `30303135271`. The stable policy check passes; every platform/configuration configures, builds, registers its complete inventory, runs CTest, and uploads JUnit/failure evidence. Ubuntu Release passes 55/55 in 155.34 seconds. Ubuntu ASan passes 50/55; macOS Release and ASan pass 44/55 and 41/55; hosted Windows Release and ASan pass 44/57 in 804.80 and 1093.28 seconds. Local Windows remains green at 56/56 Release and ASan. Unix and Windows use evidence floors of 55 and 56 because Unix intentionally excludes Windows launcher probes. Remaining reds are recorded product/platform failures; no test, timeout, threshold, platform, support claim, POC, ADR, slice, release, or KNI status changed. Branch protection remains unmodified, and only `CI policy / GitFlow contracts` is a post-merge candidate required check until the separate platform failures are repaired. |
| 2026-07-27 | Tiled JSON tilemap import ready for handoff | On `feature/tiled-tilemap-import`, the first small external tile workflow converts supported orthogonal Tiled JSON maps in an isolated worker, validates and atomically publishes one PNG/TileSet/tilemap dependency chain through `AssetService`, and opens it through **Assets > Import Tiled Tilemap...** in the existing Tile Palette without scene mutation. Finite/infinite numeric arrays, an inline/external JSON atlas TileSet, negative chunks, stable IDs, Y conversion, and all eight orthogonal GID transforms are covered. Focused Windows Release passes 9/9 in 6.48 seconds; MSVC AddressSanitizer passes 9/9 in 21.05 seconds; actual-worker Qt import passes in both configurations. The 189-record developer bundle includes and hash-verifies the isolated importer. Ubuntu/macOS/hosted evidence, XML, multiple TileSets, non-orthogonal/object/encoded inputs, collision conversion, reimport, complete POC K/O, ADR promotion, slice/release closure, and KNI support remain open. |
| 2026-07-27 | Tilemap authoring workspace ready for review | Human review merged Tiled import PR #5 at `8aac0e7`; `feature/tilemap-authoring-workspace` now adds atomic empty-Tilemap creation, a Transform + Tilemap2D preset, Project-to-Scene/Hierarchy creation and Inspector reassignment, tile-specific Project details, real atlas palette/canvas art, stable-ID layer operations, brush flips/quarter turns, and transform-aware painting with overlay/cancel/Play/3D guards in the 2D Scene View. The complete strict Windows Release preset passes **60/60 in 446.31 seconds**; the final focused MSVC AddressSanitizer tile workflow passes **6/6 stages in 42.269 seconds** without a sanitizer finding. The broader ASan preset passes **59/60 in 646.49 seconds**: both complete editor interaction aliases pass, while the existing POC J Qt-painted-FPS gate fails repeatedly below its unchanged 30 FPS threshold. The 189-record developer bundle hash-verifies and passes its packaged MonoGame self-test. Current Ubuntu/macOS, complete POC K/O/J, non-orthogonal/multi-TileSet/rule/animated/terrain/reimport scope, ADR/slice/release promotion, and KNI production support remain open. |

## Completion Checklist

- [x] Create and hash-verify this mirrored active plan.
- [x] Append the new user intake to both Notes copies.
- [x] Publish synchronized `DPE-ARCH-0009` Design, Prompt/Result, ADR index, and `AGENTS.md`.
- [x] Mark narrower active plans superseded for tracking without closing their gates.
- [x] Record the fresh Windows 42/42 strict Release baseline at 192.18 seconds.
- [x] Record the fresh Windows 42/42 MSVC AddressSanitizer baseline at 222.45 seconds.
- [x] Record the expanded Windows 45/45 strict Release matrix.
- [x] Pass and record the refreshed Windows 45-test MSVC AddressSanitizer matrix at 368.80 seconds.
- [x] Repair atomic multi-document publication and record the current Windows 56/56 strict Release and 56/56 MSVC AddressSanitizer matrices.
- [x] Extract and verify the reusable editor library boundary while preserving production/test entry points and registrations.
- [x] Implement and verify the DPE-ARCH-0010 full-grid workspace and direct generated-script attachment on Windows Release/ASan.
- [x] Implement and verify the managed/native-v2 full lifespan, native-v1 fallback, bounded fixed dispatch, and stage-failure containment on Windows Release/ASan.
- [x] Generate, smoke-test, and launch the Windows production-style developer editor bundle with a measured fast incremental path.
- [x] Implement and verify the Getting Started, Square/Circle Sprite, editable script-field, and Unity-familiar generated lifecycle-hook increment on Windows Release/ASan.
- [x] Implement and verify contained Project Explorer source indexing plus Rider solution generation and Inspector/Project source-edit actions on Windows Release/ASan.
- [x] Implement and verify the DPE-ARCH-0012 managed GameObject controller, shared runtime Transform, mover, Inspector omission, and writable-bundle preservation increment on Windows Release/ASan.
- [x] Implement and verify the DPE-ARCH-0013 configurable input-map, keyboard/mouse/gamepad, rebinding, and shared mover-action increment on focused Windows Release/ASan.
- [ ] Close every original Slice 1 gate on Windows, macOS, and Ubuntu.
- [ ] Complete POCs E-L and close every Slice 2 gate.
- [ ] Pass complete 2D and 3D designer/accessibility scenarios.
- [ ] Complete and pass POC M project lifecycle/recovery.
- [ ] Complete and pass POC N reversible migration.
- [ ] Complete and pass POC O asset import/cache integrity.
- [ ] Complete and pass POC P plugin trust/compatibility.
- [ ] Complete and pass POC Q Unity bridge.
- [ ] Complete and pass POC R package/install/update/rollback.
- [ ] Complete and pass POC S release qualification.
- [ ] Satisfy Slice 3 acceptance on all supported platforms.
- [ ] Satisfy Slice 4 and `1.0.0` release acceptance.
- [ ] Promote only ADRs whose full evidence and review gates pass.
- [ ] Preserve KNI's experimental label unless its complete matrix passes.
- [ ] Verify every supported pre-1.0 upgrade fixture.
- [ ] Complete security, accessibility, dependency, license, and legal reviews.
- [ ] Verify clean install, update, rollback, uninstall, and reference-project reproduction.
- [ ] Record final commands, logs, artifact hashes, blockers, and handoff.
- [ ] Verify every mirrored Markdown pair is byte-identical UTF-8/no-BOM with LF endings.
- [ ] Mark this plan complete only after every required acceptance statement has direct evidence.
