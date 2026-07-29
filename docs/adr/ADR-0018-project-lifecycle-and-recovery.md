# ADR-0018: Project Lifecycle and Recovery

> **Status:** Proposed
> **Last reviewed:** 2026-07-29
> **Design revision:** `DPE-ARCH-0016`

## Context

Dragon Pixel 1.0 must create, discover, configure, open, upgrade, recover, archive, and restore projects on all three desktop baselines. The existing project-open path does not define template provenance, SDK requirements, build targets, upgrade ownership, archive recovery, or safe creation semantics. These operations affect multiple files and directories and cannot be owned by dialogs or implemented as ad hoc filesystem writes.

## Decision

- `IProjectLifecycleService` owns templates, project creation, compatibility checks, settings, SDK/toolchain validation, ordered upgrades, archive/restore, and lifecycle recovery. UI surfaces collect intent and display results but do not perform authoritative mutations.
- `dpe.project` version 4 adds an engine semantic-version range, required SDK/toolchain descriptors, declared build/package targets, plugin requirements, and optional template provenance while preserving version-3 component roots.
- `dpe.project-template` version 1 is declarative and non-executable. It records stable template identity/version, supported engine range, typed parameters, contained source entries, expected output documents, and hashes.
- Installed 2D and 3D templates are immutable. Creation begins with a dry-run summary, expands into a sibling staging directory, uses an injected stable-ID provider, validates the complete candidate through the normal project/scene/schema services, and commits only to a destination that is new and empty.
- Discovery and recent-project state are per-user, non-authoritative indexes. A discovered path is not trusted, scanning does not mutate it, and opening still builds and validates a candidate session before replacing the active project.
- Project versions 1 through 3 migrate deterministically to version 4 through explicit ordered migrations with backups, hashes, diagnostics, and recoverable commit boundaries. A newer or incompatible project opens read-only when safe or fails without rewriting it.
- Settings and SDK/toolchain checks return structured compatibility diagnostics. Validation does not execute project code, restore packages, or run custom build targets.
- Archive moves a validated project to a user-selected recoverable archive target and writes an archive manifest. Restore checks identity, containment, hashes, destination conflicts, and compatibility before mutation. Permanent deletion is outside the first implementation and requires a separate destructive-approval and recovery decision if later added.
- Creation, upgrade, archive, and restore are validated operation transactions with dry-run output, contained staging, cancellation, audit correlation, injected-failure recovery, and explicit final disposition.

## Consequences and tradeoffs

Central lifecycle ownership makes multi-file changes deterministic, testable, and recoverable, and prevents project dialogs from bypassing validation. It adds staging storage, manifests, migration fixtures, and careful cross-volume archive behavior. Requiring a new empty creation destination and recoverable archive semantics is less permissive than arbitrary filesystem operations, but materially reduces overwrite and data-loss risk.

## Security and ownership

- `IProjectLifecycleService` owns lifecycle transactions; the project/scene services remain authoritative for validated document state. Templates, panels, plugins, migration tools, and automation clients cannot write project files directly.
- Every source, destination, archive, and generated path is normalized and containment-checked after resolving links or reparse points. Root, home, workspace-wide, reserved metadata, case-colliding, non-empty creation, and ambiguous archive targets are rejected before mutation.
- Template expansion is data-only. Project code, template scripts, build tools, and package restore do not execute during creation, discovery, or compatibility inspection.
- Cancellation or failure preserves the previous valid project and archive state, removes only verified operation-owned staging, and leaves a recovery manifest when automatic cleanup is unsafe.

+## DPE-ARCH-0014 implementation boundary

The first POC M implementation increment adds the production Project Hub, relocatable immutable 2D/3D template resources, dry-run and recoverable creation, recent-project state, clean scene creation, project-v4/template-v1 validation, and project-v1-to-v3 in-memory compatibility. The 2D template contains only a primary orthographic camera; the 3D template contains only a primary perspective camera and directional light. Both contain a default input map and worker-only C# component project. This increment deliberately does not claim settings/upgrades/archive/restore completion; those remain required before POC M or this ADR can pass.

## Validation and evidence gate

POC M must prove built-in 2D and 3D template validation, dry-run creation, contained new destinations, deterministic injected IDs, complete candidate validation, project-v4 round trips and version-1-to-4 upgrade fixtures, discovery/recent/settings isolation, SDK/toolchain diagnostics, archive/restore, cancellation, and failure injection at every staging and commit boundary on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. It must prove no outside write, partial project, silent rewrite, or unrecoverable archive state remains after failure.

Current partial Windows evidence (2026-07-25): the project index validates the originally selected manifest and project-root leaf before any recovery write, returns a canonical manifest/root pair, preserves the active session if candidate validation fails, and revalidates source snapshots. Portable relative paths use `/` grammar and reject absolute, drive, UNC, backslash, dot/dot-dot, reserved-name, invalid-character, and trailing-dot/space aliases. A global NFC/case-fold registry covers declared roots, indexed documents, and asset sources; `startupScene` must be contained by a declared scene root. Safe ancestor canonicalization accommodates platform aliases while linked manifest/root leaves remain rejected. The current strict Release matrix passes 45/45; focused project-index coverage reports 22 passed cases and one filesystem-dependent link-fixture skip.

This is foundation evidence, not POC M acceptance. The current implementation does not pin directory/file handles throughout enumeration and open, detect every hard-link alias, preserve all metadata during replacement, or close link-swap/enumeration races. Windows passes the current 45/45 strict Release and 45/45 MSVC AddressSanitizer matrices, but template creation, project-v4 upgrades, discovery/recent/settings, SDK checks, archive/restore, complete failure injection, and both other platforms remain open. This ADR remains `Proposed` until POC M passes its three-platform Release and native-sanitizer evidence and the lifecycle/recovery behavior is reviewed.

## DPE-ARCH-0016 Project View lifecycle refinement

Project View navigation derives only from the validated current project candidate. Logical folder history, expansion, splitter, and list/tile mode are per-user state; a missing or invalid remembered folder falls back to its nearest valid ancestor or declared Assets root without creating directories or rewriting the project. Project-only organization may operate while no Scene is open, but it still requires a valid project session and `AssetService` transaction. This refinement changes no project format, creation/upgrade/archive ownership, or POC M gate, and ADR-0018 remains `Proposed`.

Focused DPE-ARCH-0016 evidence (2026-07-29): Project View captures logical folder paths and stable item IDs before index refresh, restores valid expansion/current-folder/selection state after candidate publication, and falls back to a valid ancestor or Assets root. A public Qt regression navigates nested folders through Back/Forward/Up and breadcrumbs, refreshes, and verifies restored context without a Scene or project-file write. Complete Windows Release passes 63/63, MSVC AddressSanitizer passes 63/63, and Ubuntu Release passes 62/62. Project creation/upgrades/archive/restore and full three-platform POC M remain open; ADR-0018 remains `Proposed`.
