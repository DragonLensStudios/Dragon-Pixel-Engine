# ADR-0003: Stable Native/Managed C ABI

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

Managed workers need native runtime/components without depending on compiler-specific C++ ABI, STL layout, exception behavior, allocator identity, or garbage-collected object addresses.

## Decision

- `dpe_api_v1` is the only public managed/native runtime boundary. It is a C function table acquired through an exported version-negotiation entry point.
- The ABI uses fixed-width values, 16-byte UUIDs, explicit pointer-plus-length UTF-8 views, opaque handles, stable status codes, structured error records, and capability/minor-version negotiation.
- Ownership is explicit per function. Outputs use caller-provided two-call buffers or `dpe_alloc`/`dpe_free` from the same module. Create/destroy or retain/release pairs are mandatory where ownership exists.
- No C++ class/STL value, exception, RTTI type, owning raw pointer, GC object, or MonoGame/KNI/Qt object crosses the boundary.
- Every export catches native exceptions and converts them to status/error data. Managed wrappers use source-generated `LibraryImport`, exact layouts, `SafeHandle`, and callback trampolines that catch managed exceptions.
- Calls are batched at snapshot/command/component-group granularity rather than one property/component per frame.

## Consequences

The contract is more verbose than direct C++/CLI or embedding managed objects, but it is testable, cross-platform, compiler-independent, and compatible with worker isolation. Generated bindings and strict ownership tests become required infrastructure.

## Validation and acceptance gate

POC A must pass 10,000 create/destroy cycles, allocator pairing, UTF-8/error exchange, invalid/stale handle behavior, native/managed exception conversion, and ABI version/capability failure on all three platforms with sanitizer/leak evidence.

Current evidence (2026-07-24): Windows and Ubuntu pass the complete Release and native AddressSanitizer ownership/error suites, including 10,000 managed `SafeHandle` cycles. macOS arm64 remains pending.

Primary guidance: [.NET native interoperability best practices](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices).
