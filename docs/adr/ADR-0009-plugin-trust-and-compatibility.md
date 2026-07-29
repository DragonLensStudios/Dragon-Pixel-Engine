# ADR-0009: Plugin Trust and Compatibility

> **Status:** Proposed
> **Last reviewed:** 2026-07-28
> **Design revision:** `DPE-ARCH-0015`

## Context

Dragon Pixel 1.0 must support versioned runtime, editor, importer, and automation extensions without allowing a plugin to become a second authority for project documents or an unreviewed code-loading path into the Qt editor. Plugins can contain incompatible or malicious code, request excessive permissions, introduce dependency conflicts, fail during an engine upgrade, or leave projects unreadable when removed. Their identity, compatibility, integrity, permissions, ownership, and recovery behavior therefore require one explicit boundary.

## Decision

- `dpe.plugin` version 1 is the authoritative plugin manifest. It records stable plugin identity and version, publisher, engine/API version ranges, integrity and signature metadata, platforms and architectures, dependencies, runtime kind, contributions, requested capabilities, and license inventory identity.
- Supported runtime kinds are `native-worker`, `managed-worker`, `editor-declarative`, `editor-native-trusted`, and `python-tool`. Each kind has a separate loading and capability policy; a manifest category does not grant code execution by itself.
- The editor discovers and validates manifests as data without loading plugin code. Native and managed runtime plugins load only inside disposable workers through the existing versioned C ABI or managed contracts. Python tools remain external processes.
- DPE-ARCH-0015 adds the project-local `dpe_tile_extension_plugin_v1` worker ABI. Tile evaluators receive bounded neutral batches and return render/collision results; brush evaluators return bounded command proposals. The editor validates every proposal through the authoritative command owner and never loads the module or grants it project-write authority.
- Declarative editor contributions use metadata, commands, and editor services. Trusted native Qt editor plugins are explicit-install, exact-editor-version-bound, restart-required, disabled after a crash, and receive no stable cross-version C++ ABI promise.
- Install, update, disable, and remove are `IPluginService` package transactions. They stage changes, validate integrity/signatures, engine/API ranges, platform/architecture, dependency closure, conflicts, requested capabilities, and licenses, then commit recoverably with rollback information.
- A plugin may use only capabilities declared by its manifest and approved by policy or the user. It cannot write authoritative project documents directly; project mutations go through validated commands or explicitly authorized staged-import operations.
- Disabling, removing, quarantining, or failing to load a plugin preserves its component and document records opaquely. A missing plugin produces repairable diagnostics rather than destructive cleanup.
- Plugin compatibility is tested against declared public contract versions and fixtures. An engine upgrade does not silently enable an incompatible plugin, rewrite its data, or discard the last known-good installation.

## Consequences and tradeoffs

Data-only discovery and worker isolation protect editor stability and project ownership, while transactional installation makes dependency and upgrade failures recoverable. The cost is additional packaging metadata, per-platform artifacts, compatibility fixtures, permission UI, and slower plugin iteration. Exact-version native Qt plugins are less convenient than a broad binary ABI, but avoid promising stability across compiler, Qt, or editor-private C++ changes. Full in-process extensibility and implicit capability grants are rejected for 1.0.

## Security and ownership

- `IPluginService` owns the installed-plugin registry, compatibility state, permission grants, quarantine state, and package transactions. Panels and plugins do not mutate that state directly.
- Plugin packages and paths are normalized, containment-checked after resolving links or reparse points, and hash/signature verified before activation. Staging directories are writable only for the operation being performed.
- Worker plugins receive the minimum required project and broker capabilities. Direct project writes, undeclared network or process access, and privilege expansion are denied or surfaced as blocking diagnostics.
- A crash or failed health check quarantines the responsible plugin for the session without disabling unrelated plugins or changing saved authoring state. Rollback restores the prior package and registry state.
- License and dependency inventories are release inputs. Integrity validation is not a substitute for trust review, and a valid signature does not automatically grant capabilities.

## Validation and evidence gate

POC P must prove manifest/schema and integrity validation, declared-capability enforcement, dependency and version-conflict handling, transactional install/update/disable/remove with rollback, worker isolation, direct-write rejection, crash quarantine, engine-upgrade compatibility, opaque-data preservation, and dependency/license inventory on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. Tile-extension coverage additionally requires ABI size/version/ownership tests, batch/resource limits, malformed or over-broad command rejection, timeout/crash quarantine, editor-process exclusion, and lossless missing-type behavior. Tests must include malformed and tampered packages, unsupported platforms/architectures, missing dependencies, cycles, incompatible upgrades, denied permissions, worker crashes, and interrupted transactions.

This ADR remains `Proposed` until POC P passes its three-platform matrix and the security, compatibility, packaging, and licensing evidence is reviewed.

Focused tile-extension evidence (2026-07-28): draft PR #6 implements the size-tagged `dpe_tile_extension_plugin_v1` evaluation/proposal tables, hash-bound manifest declarations, worker-only loading, input/output/resource validation, allocator pairing, and lossless missing/incompatible behavior. The editor-side Custom Extension Brush negotiates the preview-worker capability, sends an immutable bounded context with a portable stable seed, rejects stale or malformed correlations and unknown tile identities, caps accepted proposals at 4,096 commands, restarts the disposable worker after a two-second timeout, and applies valid cell commands through `TileDocumentService` as one Undo transaction. The editor never loads the native module or grants it direct file authority. Focused Release passes eight ABI/runtime aliases in 8.54 seconds plus the two Qt correlation/Scene workflows in 30.5 seconds; MSVC AddressSanitizer passes the same aliases in 16.21 seconds plus the Qt workflows in 36.7 seconds without a finding. The complete local matrices pass 63/63 in 570.78 seconds Release and 63/63 in 895.30 seconds under MSVC AddressSanitizer. General plugin install/update/remove, permission UI, signed distribution, full crash quarantine, and the required hosted three-platform POC P matrix remain open, so this ADR remains `Proposed`.
