# ADR-0022: Version 1 Compatibility Policy

> **Status:** Proposed
> **Design revision:** `DPE-ARCH-0009`
> **Last reviewed:** 2026-07-25

## Context

Dragon Pixel Engine has several public boundaries with different compatibility mechanics: product and SDK versions, the native C ABI, managed contracts, local protocols and shared-frame headers, schemas and durable documents, project/runtime plugins, and release/update manifests. A single `1.0.0` label cannot substitute for explicit negotiation and migration at each boundary. The first stable release also has to upgrade every supported public pre-1.0 fixture without discarding unknown or incompatible data.

## Decision

- One generated version source supplies CMake, native binaries, managed assemblies/packages, Python tooling, schemas, protocol handshakes, packages, update manifests, and displayed product identity. Release validation rejects mixed or placeholder version identities.
- The product and public SDK/package surface use semantic `MAJOR.MINOR.PATCH` versions. Within major version 1, patch releases are corrective and minor releases may add backward-compatible capabilities. A removal, incompatible behavior change, or required consumer rewrite is breaking and requires the next major version unless a security emergency follows the exception process below.
- Every public boundary retains its own explicit version and capability negotiation. Product-version equality alone never authorizes ABI calls, protocol messages, document writes, plugin loading, or package updates.

| Public boundary | Version 1 rule |
| --- | --- |
| C ABI | Major-versioned function tables remain C-only, size checked, ownership explicit, and append-only for compatible optional capabilities. An incompatible layout, ownership, or semantic change requires a new ABI major and a compatibility path. |
| Managed contracts and Python tooling | Public types/commands keep stable IDs and documented semantics. Compatible additions are optional or have safe defaults; breaking surface changes require a major version and migration guidance. |
| Local IPC and shared-frame transport | Peers negotiate protocol/header versions and capabilities before use. Unknown optional capabilities are not assumed; incompatible required versions fail with structured diagnostics. |
| Schemas and durable documents | Each format has an explicit version and ordered deterministic migrations. Older supported documents upgrade through fixtures; newer/incompatible documents open read-only or fail without rewrite. Unknown component/property records remain opaque and lossless where the owning schema permits. |
| Runtime and declarative plugins | `dpe.plugin` declares engine/API ranges, dependencies, platform/architecture, integrity, and capabilities. Compatibility does not imply trust. Trusted native Qt editor plugins remain exact-editor-version-bound, explicit-install, restart-required, and outside cross-minor binary compatibility. |
| Packages and updates | The signed release/update manifest declares product version, minimum compatible version, channel, platform/architecture, hashes, and rollback identity. An updater rejects an incompatible or unverifiable transition before installation. |

- Before the first release candidate, all public pre-1.0 format, metadata, ABI/protocol, plugin, and package fixtures are cataloged. A `1.0.0` release candidate must either upgrade each declared supported fixture or preserve it without mutation and emit a documented incompatibility diagnostic.
- A deprecated public capability remains functional for the rest of major version 1 unless a security-critical exception is approved. Deprecation documentation names the replacement, migration, diagnostic, and earliest removal version. Removal or emergency incompatibility requires a reviewed ADR, version change, compatibility or migration path, release note/security advisory, and regression fixtures.
- KNI remains an experimental adapter and is excluded from a production-support claim until its complete .NET 10 and three-platform conformance matrix passes. Experimental status must be visible in manifests, UI, documentation, and independently reported evidence.

## Consequences and tradeoffs

- Separate boundary versions prevent accidental compatibility claims, but require more handshake, fixture, migration, and release-matrix maintenance.
- Major-version stability constrains cleanup and removal after `1.0.0`; compatible parallel capabilities and deprecation periods can temporarily increase code and documentation complexity.
- Lossless unknown-data handling protects projects created by newer tools, but readers must carry opaque records and block unsafe writes rather than normalizing data they do not understand.
- A single generated product version improves traceability while leaving contract-specific versions explicit, so release tooling must validate both kinds of identity.

## Security and ownership

- Release engineering owns the generated product version and signed compatibility manifest. Individual adapters, plugins, importers, projects, or UI surfaces cannot override or synthesize a trusted compatibility result.
- Each receiving boundary validates version, size/schema, capabilities, integrity where applicable, and resource limits before accepting data or code. A version match is not a substitute for signature, permission, containment, or input validation.
- Compatibility fallback never enables a missing capability silently, loads incompatible project/plugin code into the editor, or rewrites a newer document. Failures remain structured, visible, and non-destructive.
- Security-critical exceptions use the narrowest change possible, retain rollback or migration where feasible, and require an ADR, advisory, version change, and fixtures. Release pressure alone is not an exception.

## Validation and evidence gate

**POC S: Version 1.0 Release Qualification** must exercise the frozen public compatibility corpus on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64. It must prove every declared public pre-1.0 upgrade fixture; compatible same-major API/ABI/protocol/schema/plugin behavior; incompatible/newer rejection or read-only preservation; unknown-data round trips; mixed-version detection; deprecation diagnostics; and documentation-driven reproduction without data loss.

**POC R** supplies the clean-install, minimum-version, update, interruption, and rollback compatibility evidence for signed release manifests. MonoGame and KNI results remain independent, and POC S cannot promote KNI from experimental without the complete KNI conformance matrix.

POCs R and S are unimplemented as of 2026-07-25. This ADR remains `Proposed`, and no `1.0.0` compatibility claim may be made until its evidence is reviewed with every earlier slice gate.
