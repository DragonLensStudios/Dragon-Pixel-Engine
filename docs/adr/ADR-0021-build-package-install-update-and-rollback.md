# ADR-0021: Build, Package, Install, Update, and Rollback

> **Status:** Proposed
> **Design revision:** `DPE-ARCH-0009`
> **Last reviewed:** 2026-07-25

## Context

Slice 3 must turn declared project targets into reproducible build results and ship a relocatable editor/runtime without exposing the editor process to project code. Slice 4 must prove clean installation, signed or notarized distribution, update interruption recovery, rollback, uninstall, provenance, dependency/license inventory, and Qt compliance on every supported desktop baseline. Developer-machine source/build paths and a running editor overwriting its own binaries are unacceptable release boundaries.

## Decision

- `BuildService`, `PackageService`, and `UpdateService` own the workflows. UI surfaces collect intent and display structured progress; all build, package, sign, stage, switch, rollback, and uninstall mutations occur through cancellable operation transactions with correlation IDs and recovery records.
- Project builds run in supervised disposable workers from declared `dpe.project` version-4 targets. The editor never loads their outputs. A `dpe.build-result` version-1 record binds target/configuration, toolchain identity, input hashes, diagnostics, artifacts and hashes, dependency/license inventory, cancellation state, and reproducibility metadata.
- CMake relative `install()` destinations and a CPack-compatible install tree are the common packaging foundation. Installed resources are resolved from the executable/install root or explicit per-user application-data roots; release artifacts fail validation if they contain source-tree or build-tree absolute paths.
- The direct-distribution baseline is a signed Windows installable package, a correctly nested Developer ID-signed and notarized macOS application delivered in a stapled distribution container, and an Ubuntu installable package with a verifiable signed/hash-bound manifest. Each channel also publishes a complete, immutable update payload. Binary-delta updates and application-store channels are outside the 1.0 baseline unless a later reviewed decision and POC R evidence add them.
- A release/update manifest version 1 records channel, semantic version, platform/architecture, artifact hashes and sizes, minimum compatible version, dependency/SBOM/notices identities, and rollback metadata. Packaging fails if the manifest, dependency inventory, licenses/notices, provenance, required source offers, or Qt LGPL materials and relinking instructions are missing.
- Updates use a minimal helper outside the running editor. It verifies the signed manifest, trust identity, channel, version compatibility, platform/architecture, and every payload hash before requesting editor shutdown. It stages the complete release side by side, validates launch readiness, switches atomically where the platform permits, retains the last known-good release, and records enough state to resume or roll back after interruption.
- Update, rollback, uninstall, and repair never modify project documents, external linked assets, or user-authored content. Per-user caches and settings are separately owned and are retained or removed only according to an explicit user choice.

## Consequences and tradeoffs

- Full-package side-by-side updates consume more bandwidth and temporary disk space than binary deltas, but simplify integrity verification, recovery, and rollback for 1.0.
- One common install tree reduces layout drift, while signing, notarization, native installer construction, and clean-machine tests still require platform-specific release work.
- Reproducible build records, SBOM/provenance, notices, and path scanning add release time and storage. They make an artifact traceable to inputs and prevent accidental developer-machine dependencies.
- Keeping project builds and signing out of the editor reduces crash and credential exposure, but requires supervised workers, a signing boundary, structured logs, and explicit cancellation/recovery UX.

## Security and ownership

- Project code and build tools execute only in disposable workers with declared inputs and staging outputs. Build artifacts are untrusted until `BuildService` verifies their result manifest and hashes; they are never mapped into the editor.
- Signing/notarization credentials belong to the release environment, not to projects, plugins, the editor repository, or ordinary build workers. Private signing keys and authentication material may not appear in manifests, command lines, logs, support bundles, or packaged resources.
- The update helper has only the privileges needed for the selected install root. It rejects unsigned, untrusted, incompatible, cross-channel, wrong-platform, hash-mismatched, and rollback-incomplete payloads before switching the active release.
- The last known-good release and rollback record are updater-owned state. The editor cannot delete them during a self-update. A failed health check or interrupted switch leaves either the old release active or a deterministic recovery action; it never leaves a partially merged install tree.
- Qt is dynamically linked for the LGPL distribution path, with notices, corresponding-source/source-offer materials, and relinking instructions packaged and audited. Distribution remains subject to the documented legal review and is not characterized by this ADR as legal advice.

## Validation and evidence gate

**POC R: Package, Install, Update, and Rollback** must pass on clean Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64 baselines. It must prove relocatable packaging; clean install and launch; reference-sample build/run; independent MonoGame/KNI results; signature or notarization verification as applicable; dependency, SBOM, provenance, notices, and Qt materials; staged update; incompatible/signature/hash failure; injected interruption; last-known-good rollback; uninstall; and release-artifact absolute-path scanning.

Current non-gate Windows developer evidence (2026-07-26): `Build-Production-Editor.ps1` incrementally builds the Release editor target, stages a stable 131-file developer bundle, deploys Qt and the release offscreen self-test plugin, includes native/managed workers, contracts, Python tools, schemas, and a sample, writes relative-path SHA-256 file evidence, and passes a packaged MonoGame editor self-test. Runtime assets prefer executable-relative paths with development fallbacks. The unchanged-tree `-Fast` build/deploy path measures 6.05 seconds after repairing a corrupt Ninja dependency database. This bundle is for local iterative testing and does not satisfy POC R: it is not an `install()`/CPack tree, retains development fallbacks, has not passed absolute-path scanning or clean-machine relocation, and lacks signing, install/update/rollback/uninstall, SBOM/provenance, notices, Qt compliance materials, and three-platform evidence.

POC R remains unimplemented as a release gate, so this ADR remains `Proposed`. Passing POC R is necessary but not sufficient to release `1.0.0`; final package/update compatibility, stability, security, privacy, accessibility, and documentation reproduction remain part of POC S.

## Primary references

- [CMake 4.4 `install()` rules, relative destinations, runtime dependencies, and SBOM support](https://cmake.org/cmake/help/latest/command/install.html)
- [Microsoft Windows package and deployment overview](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/)
- [Microsoft SignTool package-signing guidance](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool)
- [Apple distribution signing for macOS](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/)
- [Apple notarization guidance](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
