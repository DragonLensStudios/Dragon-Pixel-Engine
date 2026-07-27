# ADR-0005: Cross-Language Metadata and Inspector Schema

> **Status:** Proposed
> **Date:** 2026-07-24
> **Last reviewed:** 2026-07-27
> **Design revision:** `DPE-ARCH-0014`

## Context

The Inspector must expose C++ and C# properties consistently without loading arbitrary project code into the editor. Runtime reflection and compiler RTTI are not portable contracts and are unsuitable for AOT/Unity sharing. Slice 2 requires typed editing, defaults for GameObject presets, multi-selection, reference filtering, validation, component cards, first-party specialized drawers, and lossless presentation of unavailable components. These behaviors need one language-neutral schema rather than widget-specific rules or raw JSON editing.

## Decision

- Define component metadata format version 3 as one deterministic JSON schema shared by native and managed generators. Version 2 manifests migrate explicitly; unsupported newer manifests remain diagnosable and do not authorize editing.
- A component descriptor includes stable type UUID, schema version, runtime owner, diagnostic and display names, category, tooltip, addability/removability, allowed multiplicity, default enabled state, first-party drawer key, and an ordered property list.
- A property descriptor includes a stable property UUID; diagnostic/display names; Boolean, integer, number, string, enum, Vector2, Vector3, quaternion, color, entity-reference, or asset-reference type; default; minimum/maximum; step; units; enum choices; nullability; entity/asset type filters; category; tooltip; visibility/read-only state; serialization name; and optional drawer key.
- Metadata version 4 also defines stable contract and object-type descriptors plus recursive value shapes for fixed/nullable objects, lists, string-key dictionaries, polymorphic inline values, and interface-filtered entity/component references. Polymorphic payloads carry concrete type and schema identity; inline cycles are invalid.
- C# attributes plus a source generator emit manifests and registration code. C++ explicit registration declarations/macros plus build-time generation emit the same schema. Generator versions and source/build identities are recorded for diagnostics but do not become component identity.
- `IMetadataService` loads, validates, indexes, and diagnoses manifests without loading game assemblies or native project libraries into the editor. The Inspector and preset factory ask the service for descriptors and defaults; they never infer a component contract from a framework or runtime instance.
- Stable IDs, not names, determine type/property identity. A display or qualified-name change requires no migration; a representation or semantic change increments the component schema and uses an explicit migration.
- Standard Inspector drawers map descriptors to Qt controls: checkbox, bounded integer/number controls, string/enum editors, Vector2/3 editors, quaternion-as-Euler presentation, color editor, entity picker, and filtered asset picker. Euler editing converts to the canonical quaternion without changing the stored contract.
- A component appears as a scrollable card with enable, add, remove, and reorder actions governed by descriptor and command validation. Transform and collider cards may use first-party specialized drawers registered by stable drawer key. Third-party native drawer loading remains governed by the later plugin boundary, not this metadata format.
- Known nested values are rendered through typed recursive drawers and path-based commands; editable raw JSON is never the normal Inspector surface. Opaque records retain a read-only diagnostic/raw-data foldout.
- Multi-selection computes the intersection by component type UUID and property UUID. Equal values display normally; unequal values display an explicit mixed state. Committing one value creates one compound command over the selected compatible records and reports per-target validation failures before mutation.
- Unknown, missing, incompatible, or newer component records remain visible as read-only opaque cards showing original identity/version and diagnostics. Their complete raw payload and ordering round-trip unchanged; metadata absence is never permission to replace or normalize it.
- Metadata constraints are enforced by the same validation path for Inspector edits, presets, gizmos, drag/drop, migration, Python, and AI. Runtime reflection may add diagnostics but cannot replace generated manifests, bypass commands, or alter saved identity.

## Consequences

Build-time generation adds tooling, migrations, and golden-fixture maintenance, but provides one AOT-safe Inspector contract, fast startup, deterministic presets and validation, and language symmetry. First-party custom drawers improve usability without turning arbitrary editor code into a requirement for reading a project. The editor must handle a valid record even when its preferred drawer is unavailable by using the standard drawer or a read-only diagnostic representation.

## Alternatives considered

- **Load project assemblies/libraries and reflect at editor runtime:** rejected because it executes crash-prone or incompatible project code inside the editor and is not an AOT-safe portable contract.
- **Edit component payloads as raw JSON:** retained only as an internal diagnostic tool; rejected as the Slice 2 authoring experience because it cannot reliably enforce types, references, mixed values, or accessibility.
- **Use property names as identity:** rejected because rename would become a data migration and cross-language collisions would be ambiguous.
- **Let custom drawers own serialization or mutation:** rejected because drawers are views over metadata and commands, not alternate authorities.

+## DPE-ARCH-0014 refinement

Inspector metadata intersection remains based on stable component and property IDs, but Inspector instances now have independent target providers. An unlocked panel consumes the ordered global SelectionService state; a locked panel consumes a stable scene/entity snapshot and never falls back to the current global selection. Every property, component, source-edit, reset, paste, reorder, remove, and Add-to-missing action must validate against that panel's complete target set before one transaction commits. Deleted locked targets remain explicit unavailable identities so Undo can restore them without a stale command targeting a different object. Multiple panel models do not duplicate or own authoring data.

## Validation and acceptance gate

POC C must validate equivalent native/managed component manifests against one schema and prove property IDs/types/defaults/references/precision plus rename and schema-change behavior in a headless Inspector model. Version-3 conformance must additionally cover every standard property kind, constraints, enum/default/reference filters, deterministic migration from version 2, multi-selection intersections, mixed-value commits, unavailable drawers, and opaque records. POC H must drive the real Qt drawers, validation messages, component-card actions, keyboard navigation, focus order, and accessibility interfaces through public UI events. The ADR remains `Proposed` until that three-platform evidence is reviewed.

Current evidence (2026-07-25): Windows and historical Ubuntu coverage passes the earlier metadata version 3 cases for UUID identities, generated manifests, stable-ID rename/migration, headless inspection, opaque preservation, constraints/reference filters, typed validation, component actions, mixed multi-edit, and POC H public interactions. Current Windows coverage adds metadata version 4, explicit declared-root selection, fixed/nullable nested objects, lists, dictionaries, polymorphic contract values, filtered component references, path commands, generated C#/C++ source metadata, strict runtime-module identity/placement/hash validation, and unbuilt/missing-factory diagnostics. The Windows strict Release and MSVC AddressSanitizer matrices both pass 45/45; Ubuntu's last pre-DPE-ARCH-0008 matrices remain 36/36 and do not cover the expansion.

First-party specialized drawer breadth, complete component reorder/reference-picker workflows, expanded accessibility evidence, and the current macOS POC H matrix remain open. The prior macOS matrices passed their registered metadata/serialization tests but predate this expanded suite. This ADR remains `Proposed`.
