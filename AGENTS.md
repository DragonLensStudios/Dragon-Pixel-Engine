# Dragon Pixel Engine Agent Instructions

These instructions apply to the entire Dragon Pixel Engine repository. They combine the project's specific architecture, evidence, and documentation requirements with the feature-development workflow installed from the Dragon Pixel Engine Development Constitution Kit.

## Mission and Engineering Priorities

Dragon Pixel Engine is an open-source C++20 and Qt visual game-development editor with framework-neutral authoring contracts and separate MonoGame and KNI runtime adapters. It exists to give code-first developers visual scene, component, prefab, tile, asset, input, preview, play, and publishing workflows without taking ownership away from their source code.

Apply engineering priorities in this order:

1. User data safety.
2. Architecture and contract integrity.
3. Failure containment and recoverability.
4. Cross-platform correctness.
5. Developer and contributor experience.
6. Performance.
7. New feature breadth.

A feature that risks corrupting authoring data is not ready. A feature that compiles but lacks evidence is not complete. A green result obtained by weakening a valid test, platform, or threshold is a failure, not success.

## Required Mirrored Documents

All documentation artifacts managed under the paired documentation roots are physical files in both the user's documentation system and the repository. The paired files must have identical UTF-8/LF bytes.

| Logical document | Documentation-system copy | Repository copy |
| --- | --- | --- |
| Design | `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine Design Document.md` | `docs\Dragon Pixel Engine Design Document.md` |
| Prompt/result provenance | `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine LLM Prompt Source.md` | `docs\Dragon Pixel Engine LLM Prompt Source.md` |
| Original first-structure prompt | `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine First Structure Prompt Main Flow.md` | `docs\Dragon Pixel Engine First Structure Prompt Main Flow.md` |
| Project notes/intake | `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Dragon Pixel Engine Notes.md` | `docs\Dragon Pixel Engine Notes.md` |

Before planning, reviewing, or changing architecture or implementation:

1. Verify every paired file exists and has the same SHA-256 hash.
2. Read the Design and Prompt/Result documents completely.
3. Confirm their design revision fields match this file.

The current synchronized design revision is `DPE-ARCH-0016`, last reviewed 2026-07-29. For maintainers and project agents with access to both roots, if either location is unavailable, paired hashes/revisions differ, or a file appears incomplete, stop documentation, planning, and implementation work and repair or clarify the mirror first. Neither path, timestamp, nor Git status automatically wins a conflict. An external contributor who cannot access the private documentation-system root may prepare a repository-only proposal only when the proposal clearly states that the private mirror was unavailable and unchecked; that proposal is not accepted project evidence and may not merge until a maintainer synchronizes and byte-verifies every affected pair.

The first-structure prompt is immutable historical evidence. The Notes file is a living intake/context record and may receive user-supplied additions, but those additions must be mirrored. Neither file overrides the current Design or Prompt/Result documents.

## Precedence

Apply project guidance in this order:

1. Explicit current user instructions.
2. This `AGENTS.md`.
3. The synchronized current revision of the Design Document for architecture, contracts, risks, milestones, and acceptance criteria.
4. The synchronized Development Constitution and handbooks under `docs/Development` for feature branching, planning, commits, verification, review, and release workflow.
5. The synchronized matching current result in the LLM Prompt Source for request/result provenance.
6. The operational Codex master/start prompts under repository `prompts` when the user invokes that workflow.
7. Historical prompt and notes.

If sources conflict, do not silently choose one. Record the conflict and update the appropriate living documents or request direction.

## Living-Document Update Rules

An accepted change to architecture, a public contract, durable data, process topology, ownership, platform/framework support, security boundary, roadmap scope, risk gate, or acceptance criteria requires all of the following in the same work item:

1. Update both copies of the Design Document first.
2. Add or update the relevant ADR and its status when ADRs exist in the repository.
3. Update both copies of the current generated result in the LLM Prompt Source.
4. Give both living documents the same new `DPE-ARCH-####` revision and review date.
5. Update both revision histories and any affected risk, prototype, source, and roadmap sections.
6. Update the current revision recorded in this file.
7. Verify all documentation-system/repository mirror hashes before completing the work.

Do not edit the Original Prompt section in the LLM Prompt Source. Add a dated annotation if provenance needs clarification.

Implementation progress that does not change architecture should update normal repository documentation/tests, not invent a new architecture revision. If prototype evidence validates or invalidates an assumption or risk gate, update both living documents even when the intended architecture remains unchanged.

## Documentation, Plan, and Response Capture

- Store every durable project document, implementation plan, architecture/research result, and substantive generated response as Markdown in both documentation locations.
- Store every plan under `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans` and the repository `docs\Plans` directory. Use the same relative path, filename, and UTF-8/LF bytes in both locations.
- Create the plan before executing its work, maintain its status, decisions, verification evidence, blockers, and handoff notes while the work proceeds, and mark its final disposition before ending the work item.
- Before a substantive final response, append the delivered changes, exact verification, remaining blockers, and next handoff to the active mirrored plan or the appropriate mirrored evidence document. The final response must agree with that durable record.
- Add material to the appropriate existing living document when it belongs there. Otherwise create a descriptive same-named Markdown file in both `C:\Projects\Documentation\Engines\Dragon Pixel Engine` and repository `docs`.
- A chat response alone is not the durable project record. Record decisions, completed work, verification results, blockers, and meaningful handoff information in the appropriate mirrored Markdown before ending the work item.
- Keep mirrored filenames and relative organization identical. Use physical files, not links or symlinks.
- Create/edit both copies in the same work item. Never defer the second copy to a later task.
- Use UTF-8 without a byte-order mark and LF line endings. After editing, compare SHA-256 hashes for exact equality.
- If a user requests a rename or removal, apply it to both copies and update links/manifests in the same work item. Do not delete durable documentation merely because it is outdated; preserve history or supersede it explicitly.
- The repository mirror provides Git history. The external mirror provides integration with the user's documentation system. They represent one logical document, not competing sources.
- Repository control files such as root `README.md`, `LICENSE.md`, `AGENTS.md`, files under `prompts`, GitHub templates, and code-adjacent technical files are not part of the paired documentation roots unless a user explicitly adds them to the mirror set. The installed operational prompts are repository workflow controls; their durable policies also appear in the mirrored Development Constitution, handbooks, and plans.
- The repository `docs` tree is the public contribution entry point, but it does not automatically outrank the external mirror. The external-contributor proposal exception above does not authorize a project agent or maintainer to bypass mirror checks or continue an accepted work item on mismatched documentation. A maintainer or project agent with access must create or update the external copy and verify byte equality before the work item is accepted or merged. Never claim mirror verification that was not performed.

## Fact and Citation Policy

- Mark compatibility-sensitive claims as verified facts or assumptions.
- Cite primary vendor, standards-body, official project documentation, or official source files.
- Reverify technology/framework/platform claims before an upgrade and whenever the recorded review is older than 90 days.
- Record the access date and do not present KNI on .NET 10 as supported until its conformance matrix passes.
- Treat Qt licensing guidance as a compliance requirement and obtain appropriate legal review before distribution; do not characterize project notes as legal advice.

## Governing Development Workflow

The installed workflow documents are:

- `docs/Development/Development Constitution.md`
- `docs/Development/GitFlow Handbook.md`
- `docs/Development/Commit Guide.md`
- `docs/Development/Verification Checklist.md`
- `docs/Development/Release Workflow.md`
- `docs/Development/Feature Plan Template.md`
- `docs/Development/Open Source and Commercial Sustainability.md`
- `prompts/Dragon Pixel Engine Codex Master Prompt.md`
- `prompts/Start Atomic Publication Recovery.md`

The mirrored constitution and handbooks govern normal development. Operational prompts apply only when invoked and cannot override this file, the synchronized architecture, the active plans, or explicit current user instructions. The master prompt's hard-coded **First Feature** section is a one-time kickoff for the first post-governance run; after that feature is merged, superseded, or reprioritized, the current accepted plans and explicit user direction control and Codex must not repeat a completed feature merely because it remains historical text in the prompt.

### Required feature preparation

Before planning or changing source:

1. Complete the mirrored-document checks above and read this file, the Design Document, and Prompt/Result completely. An external contributor using the proposal-only exception must instead record that the private mirror was unavailable and must not claim the check passed.
2. Read the active master plan, the relevant current feature plan, and affected ADRs.
3. Fetch and inspect the current repository state; confirm the intended base branch is current.
4. Inspect `git status`, current branch, and relevant recent commits. Preserve unrelated user work.
5. Search for existing owners, services, contracts, schemas, tests, diagnostics, and documentation before adding parallel mechanisms.
6. Identify the authoritative owner of every state being changed.
7. Determine whether the work affects durable formats, C ABI, managed contracts, protocols, process topology, support claims, security boundaries, or acceptance gates.
8. Create the focused feature branch and its mirrored feature plan before modifying source.

Do not silently infer that a missing plan, test, branch, platform result, or mirror is valid.

### GitFlow branches

Permanent branches:

- `main`: released and tagged public versions only; no direct feature work.
- `develop`: reviewed integration work and the base/target for normal features.

Temporary branches:

- `feature/<descriptive-name>` for one cohesive feature or defect.
- `release/<version>` for stabilization, versioning, documentation, packaging, and release blockers.
- `hotfix/<version-or-defect>` for a defect in a released version.

Use clear branch names such as `feature/atomic-publication-recovery`, `feature/editor-library-modularization`, `feature/ci-gitflow-integration`, or `feature/macos-frame-throughput`. Avoid vague names such as `feature/work`, `feature/fixes`, `feature/phase1`, `temp`, or new `codex/*` feature branches.

Multiple independent `feature/*` branches and PRs may be active concurrently. Each independent branch starts from the then-current `develop`, records its owner and cohesive scope in its mirrored plan, and normally targets `develop`.

A dependent feature may branch from and temporarily target one unmerged `feature/*` parent only when both plans and both PR bodies record the dependency, parent base commit, overlap, ownership, retarget procedure, and post-parent verification. The child may not merge before the parent. After the parent merges, update the child from current `develop`, retarget it to `develop`, review the aggregate diff, and rerun affected checks. Never force-push a shared contributor branch without explicit coordination. Never work directly on `main`, and do not use the historical `codex/slice1-complete` branch as a new feature base.

### Feature lifecycle

For every feature:

1. Fetch and start an independent feature from the latest `develop`, or record the explicit parent-feature dependency before creating a stacked branch.
2. Create one focused `feature/*` branch.
3. Create or update the mirrored feature plan before source changes.
4. Implement the smallest complete increments.
5. Add or update tests with behavior changes.
6. Run focused verification after every meaningful increment.
7. Commit each coherent increment with a precise conventional message.
8. Run the feature's required final verification matrix.
9. Update the plan and documentation with exact commands, results, failures, limitations, and handoff.
10. Review the aggregate branch diff, commit sequence, generated files, release paths, and evidence.
11. Push the branch.
12. Open a draft pull request into `develop` using the feature template, or temporarily into the recorded parent feature branch for an explicit dependent stack.
13. Stop and present the draft PR for human review. Do not merge without explicit authorization.

The ordered Codex handoff above reviews the aggregate diff before push and draft-PR creation. An earlier draft may be opened for visibility only when explicitly requested or when a human contributor follows the handbook; opening it does not trigger the final stop. Implementation, verification, plan/evidence updates, and aggregate review continue, and Codex stops only after the PR has been updated to the ready-for-review handoff state. A few focused commits may form one PR, but unrelated features may not share one PR. A stacked child remains draft and unmergeable until its parent merges, it is updated and retargeted to `develop`, and its aggregate diff and affected verification are current.

### Release and hotfix lifecycle

For a release:

1. Create `release/<version>` from `develop`.
2. Permit only stabilization, versioning, documentation, packaging, and release-blocking fixes.
3. Run the complete release matrix and clean install/update/rollback evidence required by the active design.
4. Merge the reviewed release into `main` and tag that merge.
5. Merge release changes back into `develop`.

For a hotfix:

1. Create `hotfix/*` from `main`.
2. Implement only the released defect repair and verify affected release paths.
3. Merge the reviewed hotfix into both `main` and `develop` and tag the corrected release.

Never force-push shared branches or rewrite shared history without explicit authorization.

## Planning Standard

Every feature plan must include:

- status, branch, target, owner, dates, dependencies, and stack disposition;
- goal, user value, background, scope, and non-goals;
- existing architecture, ownership, and state boundaries;
- contract, ABI, protocol, schema, format, platform, and support impact;
- security and data-safety considerations;
- small implementation increments;
- test, sanitizer, platform, packaging, and documentation strategy as applicable;
- risks and mitigations;
- an evidence-backed definition of done;
- a dated work log and handoff notes.

Plans are living records. Update them during implementation, not only at the end. A plan's final disposition must distinguish implementation-ready-for-review from post-review merge completion.

## Commit Standards

Use focused commits with an imperative conventional prefix:

- `feat(scope):`
- `fix(scope):`
- `refactor(scope):`
- `test(scope):`
- `build(scope):`
- `ci(scope):`
- `docs(scope):`
- `perf(scope):`
- `chore(scope):`

Examples include `test(publication): reproduce interrupted manifest replacement`, `fix(publication): retain last valid destination during rollback`, and `docs(preview): record developer preview limitations`.

Each commit must represent one understandable change, preserve buildability when practical, include tests for behavior changes, avoid unrelated formatting, and explain non-obvious reasons. Do not use messages such as `update`, `fix stuff`, `changes`, `work`, or `final`.

## Pull Request Standards

Every pull request into `develop` must state:

- goal and user value;
- scope and non-goals;
- architecture, ownership, contract, ABI, protocol, format, and support impact;
- files and systems changed;
- exact verification commands and results;
- failure and recovery coverage;
- platform and adapter evidence reported separately;
- packaging/release impact;
- documentation updated;
- known limitations, remaining blockers, and follow-up work;
- confirmation that unrelated work was preserved and valid tests/thresholds were not weakened.

Use `.github/PULL_REQUEST_TEMPLATE/feature.md`. Review the aggregate diff before pushing and opening the draft PR. CI being green is necessary where required but is not sufficient evidence by itself. Codex must leave the PR unmerged for review.

## Testing Policy

A changed subsystem receives tests proportional to its risk, including native or managed unit tests, schema/round-trip/migration fixtures, ABI ownership/failure tests, Qt interaction/accessibility tests, worker lifecycle/crash recovery, real rendering/picking/input correlation, physics isolation, packaging/relocation, clean-install, performance, or long-running stability as applicable.

- Reproduce a defect with a failing test before fixing it when practical.
- Do not delete or weaken valid tests to make a change pass.
- Do not increase a timeout to hide a deadlock or severe regression without root-cause evidence. A justified harness-only allowance must remain distinct from a product acceptance threshold.
- Record exact commands, pass/fail counts, durations when available, skips, failures, and artifact identities.
- Report each platform, adapter, configuration, and sanitizer result separately.
- Treat missing tests, `No tests were found`, ignored command failures, missing artifacts, and unverified CI as failures or inconclusive evidence, never success.
- A partial matrix cannot justify a complete platform, adapter, ADR, POC, slice, or release claim.

Use `docs/Development/Verification Checklist.md` to select the required feature matrix. Run focused checks after each increment and the accepted final matrix before review handoff.

## Coding Standards

### C++

- Use C++20, RAII, value types, directional dependencies, minimal headers, and explicit result/error types at recoverable boundaries.
- Use smart pointers only when ownership requires indirection; do not use owning raw pointers or global mutable state.
- Catch exceptions before C ABI and process boundaries.
- Keep portable targets free of Qt/framework dependencies and add sanitizer coverage for ownership/lifetime changes.

### Qt

- Keep `EditorWindow` focused on composition and put behavior in services, models, controllers, and reusable widgets.
- Use model/view classes for durable collections and preserve stable IDs across refreshes.
- Route all mutation through command services and keep per-user UI state non-authoritative.
- Provide keyboard operation, focus order, accessible names/roles/actions, high-DPI/high-contrast behavior, and injectable prompt seams.

### C# and .NET

- Keep portable contracts on the accepted portable profile and workers/adapters on the accepted .NET runtime.
- Prefer generated registration and serializers over reflection as a portable contract.
- Catch managed exceptions at command and lifecycle boundaries; preserve cancellation and deterministic disposal.
- Keep adapter-specific types inside their adapter assemblies.

### CMake

- Prefer reusable library targets and target-scoped includes, definitions, dependencies, and flags.
- Keep dependency direction visible and avoid baking development paths into installed artifacts.
- Add tests through named targets and stable aliases; generated files are committed only when repository policy requires them.

### Diagnostics

- Errors identify subsystem, stable code, operation/correlation ID, and actionable context.
- Do not swallow failures, log secrets, or present warnings as success.
- Recovery failures preserve the last known valid state whenever possible.

## Architecture Guardrails

- Keep portable native core modules independent of Qt, .NET, MonoGame, KNI, Unity, Python, and OS UI APIs.
- Keep `DragonPixel.Contracts` dependency-light and compatible with `.NET Standard 2.1`; standalone managed runtime/adapters target .NET 10.
- Keep MonoGame, KNI, Unity, and future Unreal behavior in separate adapters. Framework types must not leak into portable contracts.
- Maintain one language-neutral entity/component authoring model. C# and C++ component implementations resolve the same stable type and entity IDs.
- Cross the managed/native boundary only through the versioned C ABI. Do not expose C++ class/STL ABI, exceptions, GC objects, framework objects, or ambiguous ownership.
- Keep saved authoring state in the editor. Preview/play workers consume mirrors or immutable snapshots; Stop must not implicitly write runtime changes into a scene.
- Route all authoritative mutations through validated commands and transactions. Panels, plugins, migration, Python, and AI may not write scene/project files directly.
- Preserve unknown or incompatible component records without data loss.
- Keep Python/AI external to the real-time loop and behind capabilities, staging, validation, cancellation, and audit logs.
- Validate Windows, macOS, and Linux from Slice 1. Do not remove a failing platform from the matrix merely to pass a milestone.
- Production viewport acceptance must use scene-driven MonoGame/KNI graphics-device frames, revision-correlated picking, and input-to-present evidence. Synthetic frames may remain diagnostic fixtures but cannot satisfy a rendering gate.
- Keep Box2D and Jolt private behind engine-owned physics interfaces and neutral DTO/C ABI batches. Backend handles and runtime state are transient and never serialized.
- Linked prefab sources, mappings, normalized overrides, and fallbacks are distinct from locally owned entity records. Runtime snapshots flatten provenance, and every prefab write uses validated command and recovery paths.
- Route project creation, upgrade, archive/restore, general asset import, migration, build, packaging, plugin, update, and support-bundle workflows through their accepted editor services and validated operation transactions. Use resolved containment, isolated staging, cancellation, structured diagnostics, audit correlation, and recoverable commit/rollback boundaries.
- Keep template, importer, migration, build, packaging, project-plugin, and analyzer code out of the editor process. The editor consumes validated manifests and structured results; installed project/runtime modules load only in disposable workers.
- Keep project-source editing outside the editor process. Project Explorer may index only stable contained source files beneath declared component roots; Rider integration regenerates disposable IDE state, launches with an argument vector, and never implies build, reload, lifecycle execution, or direct scene mutation.
- Keep `GameObjectController` a backward-compatible authoring adapter over the worker-only component contracts. Its stable GUID, input/time state, and shared `Vector3` Transform are runtime-only; dirty controller transforms may affect disposable rendering and picking but never write through to authoring files, and Stop/reload/crash must discard them.
- Keep release artifacts relocatable. Resolve resources from the install root or approved per-user application-data roots; do not ship source/build-machine absolute paths or developer credentials.
- Apply updates through the external verified helper with signed/hash-bound manifests, side-by-side staging, and last-known-good rollback. The running editor may not overwrite its own installation.
- Keep crash/support evidence local and user-initiated by default. Preview and redact bundle content, exclude secrets/project content/prompts by default, and do not enable network telemetry without a separate accepted architecture decision and granular opt-in.
- Use one generated product-version source across native, managed, Python, schemas, protocols, packages, and update manifests before release. Contract-specific versions and capability negotiation remain explicit.

## Current Work Boundary

The active product master tracker is the mirrored plan `Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan.md`. PR #6 and the CI/GitFlow integration PR #4 were reviewed and merged into `develop`. By explicit user reprioritization on 2026-07-29, the current product work item is the mirrored `Dragon Pixel Engine Project View and Team GitFlow Workflow Plan.md` on independent branch `feature/project-view-workflow`, based directly on `develop` at `fa59b227e3057af603c569f1913b652d22b50c5a`.

Proceed in evidence-backed increments:

1. Implement DPE-ARCH-0016 through the Project View/team workflow plan while preserving the merged DPE-ARCH-0015 Tilemap Editor, `AssetService` and command ownership, versioned drag validation, linked-prefab provenance, editor/worker isolation, data recovery, and current thresholds.
2. Close Slice 1 without changing its original thresholds: complete aggregate POC J Qt-event-to-paint evidence, repair the macOS POC B failure, and pass the current strict Release/native-sanitizer and foundational matrices on Windows, macOS, and Ubuntu.
3. Close Slice 2 by completing POCs E-L, the accepted editor-service split, real Scene/Game/input/tile/physics/prefab paths, complete designer workflows, accessibility, dependency/distribution evidence, and three-platform matrices without JSON editing.
4. Complete Slice 3 through POCs M-R: start with safe declarative 2D/3D project creation and recovery, then project-v4 lifecycle/settings/upgrades/archive, asset-v3 import/cache integrity, reversible MonoGame/KNI migration, declared worker builds, relocatable packaging, plugin management, verified updates/rollback, and the bounded Unity bridge.
5. Complete Slice 4 through POC S: one product-version source, frozen performance/memory budgets, long-running and corruption recovery, complete compatibility fixtures, clean install/update/rollback/uninstall, security/license/privacy/accessibility reviews, documentation/reference-project reproduction, and final three-platform release-candidate evidence.
6. Close each slice only when every acceptance statement has direct durable evidence. Mark the active plan complete and publish `1.0.0` only after all applicable gates pass.

Merged PR #6 provides the recorded DPE-ARCH-0015 Tilemap implementation and platform-separated evidence; it does not close POC K/O/P/J, affected ADRs, a slice, release, or KNI production support. DPE-ARCH-0016 now authorizes the service-owned Project View navigation/drop refinements and the explicit concurrent/dependent PR review topology. None of those additions is implementation evidence until the active plan records passing results.

Do not claim KNI production support, reduce a platform threshold, substitute synthetic rendering for real-device evidence, promote an ADR without its named gate, claim a slice from partial/platform-local evidence, or broaden beyond the documented 1.0 scope into Unreal, AAA rendering, networking, visual scripting, a marketplace, or general IDE behavior.

### Recommended focused feature sequence

Re-evaluate after the current Project View/team workflow PR is reviewed and merged, beginning with:

1. `feature/public-repository-readiness`
2. `feature/developer-preview-packaging`
3. `feature/ubuntu-current-matrix`
4. `feature/macos-frame-throughput`
5. `feature/poc-j-input-latency`

Do not mechanically start the next listed feature if current evidence, an accepted plan, or explicit user direction changes the priority.

## Repository Practices

- Keep changes small enough to map to one accepted milestone or ADR.
- Add tests for ownership, schema/round-trip behavior, failure recovery, and cross-platform contracts with the code that introduces them.
- Prefer deterministic, reviewable files and explicit migrations over implicit behavior.
- Preserve user changes and unrelated worktree modifications.
- Keep Markdown UTF-8, maintain valid internal/external links, and update diagrams when their described topology changes.
- End every documentation, planning, research, or implementation work item by verifying all mirrored Markdown pairs are byte-identical.

## Review Disposition and Definition of Done

Codex's feature implementation handoff is **ready for review** only when:

- the mirrored plan is current;
- accepted scope is implemented and non-goals remain out of scope;
- architecture, ownership, data safety, and compatibility remain valid;
- tests cover success and relevant failure/recovery paths;
- focused and required final verification is recorded exactly;
- documentation and release/install considerations are current;
- the aggregate diff and commit sequence have been reviewed;
- known blockers and unsupported/unrun platforms are explicit;
- the branch is pushed and a draft PR into `develop` is open.

The feature is fully **done** only after human review, any required corrections, approval, merge into `develop`, and post-merge validation where required. Implementation without review is not complete. Codex must not merge unless the user explicitly authorizes it.

## Codex Behavior

Codex acts as a senior software architect and implementation partner. It must understand before modifying, extend established owners/contracts instead of creating parallel replacements, plan before coding, work one focused branch at a time, use small meaningful commits, verify after meaningful changes, keep evidence current, and stop for review after opening the draft PR.

Codex must never invent passing results, hide a failing platform, promote an ADR without its gate, claim KNI production support or a slice from partial evidence, modify unrelated user work, force-push or rewrite shared history without explicit instruction, merge without explicit authorization, lower valid thresholds for green results, or broaden scope beyond accepted architecture.
