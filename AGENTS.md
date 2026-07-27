# Dragon Pixel Engine Agent Instructions

These instructions apply to the entire Dragon Pixel Engine repository.

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

The current synchronized design revision is `DPE-ARCH-0014`, last reviewed 2026-07-27. If either location is unavailable, paired hashes/revisions differ, or a file appears incomplete, stop documentation, planning, and implementation work and repair or clarify the mirror first. Neither path, timestamp, nor Git status automatically wins a conflict.

The first-structure prompt is immutable historical evidence. The Notes file is a living intake/context record and may receive user-supplied additions, but those additions must be mirrored. Neither file overrides the current Design or Prompt/Result documents.

## Precedence

Apply project guidance in this order:

1. Explicit current user instructions.
2. This `AGENTS.md`.
3. The synchronized current revision of the Design Document for architecture, contracts, risks, milestones, and acceptance criteria.
4. The synchronized matching current result in the LLM Prompt Source for request/result provenance.
5. Historical prompt and notes.

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
- Repository control files such as root `README.md`, `LICENSE.md`, `AGENTS.md`, GitHub templates, and code-adjacent technical files are not part of the paired documentation roots unless a user explicitly adds them to the mirror set.

## Fact and Citation Policy

- Mark compatibility-sensitive claims as verified facts or assumptions.
- Cite primary vendor, standards-body, official project documentation, or official source files.
- Reverify technology/framework/platform claims before an upgrade and whenever the recorded review is older than 90 days.
- Record the access date and do not present KNI on .NET 10 as supported until its conformance matrix passes.
- Treat Qt licensing guidance as a compliance requirement and obtain appropriate legal review before distribution; do not characterize project notes as legal advice.

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

The active master tracker is the mirrored plan `Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan.md`. The current implementation increment is the mirrored `Dragon Pixel Engine New Project Asset Workflow Prefab Hierarchy and Multi-Inspector Plan.md`. Together they authorize gate-preserving completion of the DPE-ARCH-0014 Project Hub/minimal-template, asset-v3/Project Browser, linked-prefab drag, ordered Hierarchy multi-operation, and multiple lockable Inspector work without claiming the still-open POC F/H/M/O or slice gates. Other narrower plans remain historical evidence and are superseded for active tracking only.

Proceed in evidence-backed increments:

1. Preserve the completed `DPE-ARCH-0010` fluid workspace/direct attachment/lifecycle, `DPE-ARCH-0011` Rider source-authoring, `DPE-ARCH-0012` managed-controller/runtime-transform, `DPE-ARCH-0013` configurable input-map/rebinding, and focused Windows `DPE-ARCH-0014` project/asset/Hierarchy/multi-Inspector increments without weakening editor/worker ownership, portable action contracts, or Stop/discard neutralization. Repair the intermittent atomic-publication recovery defect before claiming a new authoritative full Windows matrix.
2. Close Slice 1 without changing its original thresholds: complete aggregate POC J Qt-event-to-paint evidence, repair the macOS POC B failure, and pass the current strict Release/native-sanitizer and foundational matrices on Windows, macOS, and Ubuntu.
3. Close Slice 2 by completing POCs E-L, the accepted editor-service split, real Scene/Game/input/tile/physics/prefab paths, complete designer workflows, accessibility, dependency/distribution evidence, and three-platform matrices without JSON editing.
4. Complete Slice 3 through POCs M-R: start with safe declarative 2D/3D project creation and recovery, then project-v4 lifecycle/settings/upgrades/archive, asset-v3 import/cache integrity, reversible MonoGame/KNI migration, declared worker builds, relocatable packaging, plugin management, verified updates/rollback, and the bounded Unity bridge.
5. Complete Slice 4 through POC S: one product-version source, frozen performance/memory budgets, long-running and corruption recovery, complete compatibility fixtures, clean install/update/rollback/uninstall, security/license/privacy/accessibility reviews, documentation/reference-project reproduction, and final three-platform release-candidate evidence.
6. Close each slice only when every acceptance statement has direct durable evidence. Mark the active plan complete and publish `1.0.0` only after all applicable gates pass.

The last complete Windows baseline passes 45 of 45 strict Release tests and 45 of 45 MSVC AddressSanitizer tests; focused DPE-ARCH-0010 editor/component, DPE-ARCH-0011 Rider source-authoring, DPE-ARCH-0012 managed-controller/runtime-transform, and DPE-ARCH-0013 input-map/rebinding Release/sanitizer aliases pass. Focused DPE-ARCH-0014 Windows evidence passes 18 of 18 Release aliases, the complete Release editor-interaction test, 9 of 9 sanitizer service/model/prefab aliases, and 5 focused sanitizer interaction functions; its refreshed 188-record production-style bundle hash-verifies, passes the packaged MonoGame self-test, and creates/opens a minimal project outside the source tree. The aggregate DPE-ARCH-0014 sanitizer interaction run is inconclusive at its 480-second harness timeout. The intermittent atomic-publication recovery defect blocks a new authoritative complete matrix. Aggregate POC J Qt-event-to-paint timing remains open, Ubuntu's latest full evidence predates DPE-ARCH-0008, macOS retains the 14-of-15 POC B failure, POCs M-S remain open with only focused Windows portions of M/O implemented, every ADR remains Proposed, and KNI remains experimental.

Do not claim KNI production support, reduce a platform threshold, substitute synthetic rendering for real-device evidence, promote an ADR without its named gate, claim a slice from partial/platform-local evidence, or broaden beyond the documented 1.0 scope into Unreal, AAA rendering, networking, visual scripting, a marketplace, or general IDE behavior.

## Repository Practices

- Keep changes small enough to map to one accepted milestone or ADR.
- Add tests for ownership, schema/round-trip behavior, failure recovery, and cross-platform contracts with the code that introduces them.
- Prefer deterministic, reviewable files and explicit migrations over implicit behavior.
- Preserve user changes and unrelated worktree modifications.
- Keep Markdown UTF-8, maintain valid internal/external links, and update diagrams when their described topology changes.
- End every documentation, planning, research, or implementation work item by verifying all mirrored Markdown pairs are byte-identical.
