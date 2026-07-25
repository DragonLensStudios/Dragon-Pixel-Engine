# POC C Unified Metadata and Serialization Evidence

> **Evidence status:** Windows and Ubuntu passed; macOS pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **Related ADRs:** ADR-0005 and ADR-0006

## Purpose

Validate that C++ registration and C# source generation can expose components through one editor-safe metadata contract, and that deterministic JSON can preserve unavailable or incompatible component records without loading component implementation code into the editor.

## Implemented proof

- A shared JSON Schema 2020-12 component-metadata contract.
- A native C++ registration-manifest emitter with a Transform descriptor.
- A real C# incremental source generator consuming `DpeComponent` and `DpeProperty` attributes and emitting a Rotator descriptor at build time.
- Schema validation of both manifests using the same schema.
- A headless Inspector catalog that renders property rows from metadata only.
- A versioned scene schema and canonical UTF-8/LF JSON writer with ordinal object-key ordering.
- Known native and managed values, an entity reference, and high-precision decimal coverage.
- Structural round-trip preservation of a missing component, a future component, additional vendor fields, nested arrays/objects, nulls, and numeric data.
- Opaque Inspector states for missing types and known types with newer schema versions.
- A display-name rename test showing that stable type/property IDs require no migration.
- An explicit version 1 to version 2 Transform migration that changes a serialized property ID, emits a migration report, and leaves opaque data untouched.

## Windows evidence

Environment: Windows 11 x64, MSVC v143 from Visual Studio Build Tools 2022 17.14.37, .NET SDK 10.0.203.

| Configuration | `poc_c.metadata_serialization` | Result |
| --- | --- | --- |
| Release | Shared schemas, generator/native manifests, Inspector, round trip, rename, migration passed | Passed |
| MSVC AddressSanitizer | Same managed/native integration path passed | Passed |

POC C is integrated into the same checked-in CMake/CTest Release and AddressSanitizer presets as POC A. The final runs executed all three registered tests with no failures.

## Ubuntu evidence

The same POC C fixtures pass in a clean Ubuntu 24.04 x64 container in both Release and Clang AddressSanitizer configurations. The production S1.0 suite in those runs also passes deterministic known/missing/newer component round trips, explicit migration, atomic replacement, injected-save failure, backup recovery, and schema validation for the shared sample project.

## Evidence boundaries

- The POC generator and schema validator remain dedicated evidence tooling. Production `DragonPixel.Contracts`, native registration, metadata registry, schemas, and Inspector integration now implement the proven contract.
- S1.0 atomic-save failure/recovery and durable migration reporting pass on Windows and Ubuntu.
- Three-platform determinism remains unproven until the identical fixtures run on macOS.

## Remaining gate work

- Run the same test on macOS 14+ arm64.
- Expand production metadata coverage for specialized drawers and later-slice property types without changing the shared schema boundary implicitly.
- Review complete evidence before accepting ADR-0005 and ADR-0006.

## Conclusion

The cross-language metadata and opaque-record strategy is feasible on Windows and Ubuntu, including actual managed source generation, schema validation, and production persistence/recovery evidence. ADR-0005 and ADR-0006 remain proposed pending macOS evidence and review.
