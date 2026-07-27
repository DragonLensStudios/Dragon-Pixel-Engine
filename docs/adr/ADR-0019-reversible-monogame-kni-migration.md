# ADR-0019: Reversible MonoGame and KNI Migration

> **Status:** Proposed
> **Last reviewed:** 2026-07-25
> **Design revision:** `DPE-ARCH-0009`

## Context

POC D proves a bounded read-only scanner, but Dragon Pixel 1.0 also needs an assisted migration workflow that can generate, build, verify, and remove a sibling Dragon Pixel project without modifying the original MonoGame or KNI source. Static analysis cannot reliably infer gameplay intent, dynamic behavior, custom processors, native dependencies, or behavioral equivalence, so migration evidence and user decisions must remain explicit.

## Decision

- `IMigrationService` owns discovery, evaluation, analysis, reporting, planning, staged generation, verification, and rollback. Migration work runs in supervised disposable processes; the editor consumes validated manifests, plans, progress, diagnostics, and results.
- `dpe.migration-plan` version 1 records source identity and pre-scan manifests/hashes, evidence-backed classifications, confidence and source locations, explicit user decisions, destination and staging identity, generator/toolchain version, generated outputs, and a removal/rollback manifest.
- Default discovery uses static project, solution, source, and content inspection. It records source-tree paths, sizes, timestamps, attributes, and hashes before and after the operation; the manifests must be identical.
- MSBuild/Roslyn evaluation, package restore, custom targets, and builds are separately disclosed capabilities. Any operation that may execute project logic or fetch dependencies requires explicit approval and an isolated worker; it is never implied by opening or scanning a source project.
- Findings are classified `reuse`, `adapt`, `manual`, `unsupported`, or `unknown`, with confidence, evidence, limitations, and a recommended next action. Low-confidence evidence is never presented as automatic conversion.
- Before generation, the user selects the source/startup project, MonoGame or KNI adapter, destination, build target, asset ownership policy, and optional transformations. The approved choices become immutable inputs to that migration run.
- Generation writes only to a contained staging area and commits to a new sibling Dragon Pixel project after complete validation. The original source tree is never rewritten, deleted, or used as rollback storage. The 1.0 accepted path does not perform an in-place overlay migration.
- Verification builds and runs the original and migrated projects where safe and feasible, records artifacts and observed gaps, and never claims semantic equivalence solely from static analysis. Rollback removes only hash-matching generated outputs named by the removal manifest.

## Consequences and tradeoffs

The sibling-project model makes migration reviewable and reversible and gives users an intact source of truth when generated output is incomplete. It requires additional storage, explicit decisions, removal manifests, isolated toolchains, and manual work for behavior that cannot be inferred. Rejecting in-place conversion limits convenience but prevents ambiguous ownership and accidental source destruction.

## Security and ownership

- The original MonoGame/KNI tree remains owned by the user and is read-only to scanner and generator workers. `IMigrationService` owns the plan and operation state; generated destination files become Dragon Pixel-owned only after validated commit.
- Source, staging, destination, and tool paths are normalized and containment-checked after resolving links or reparse points. The destination must be a new sibling and may not alias, nest inside, or overwrite the source.
- Restore, target execution, package acquisition, and build capabilities are opt-in and separately audited. Workers receive only the minimum required read/write roots and are terminated on cancellation or policy violation.
- Rollback validates the removal manifest and current hashes before removing generated outputs. User-modified or unknown files are preserved and reported for manual resolution.

## Validation and evidence gate

POC N must prove read-only scans of representative real and fixture MonoGame/KNI projects with byte-identical pre/post source trees; evidence-backed reports and explicit decisions; contained sibling staging and commit; generated project validation; original and migrated build/run comparison where feasible; complete removal manifests; cancellation and injected-failure recovery; and rollback that never changes the source on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. Tests must cover conditional builds, custom processors/targets, native dependencies, unsupported/dynamic patterns, package-restore consent, destination conflicts, and user-modified generated outputs.

This ADR remains `Proposed` until POC N passes its three-platform evidence and migration safety, limitations, and recovery behavior are reviewed.
