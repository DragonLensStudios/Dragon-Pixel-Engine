# ADR-0004: MonoGame and KNI Framework Adapter Boundary

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

MonoGame and KNI expose similar XNA-style APIs but have different packages, capabilities, platform behavior, content pipelines, and compatibility constraints. Treating them as one compile-time framework would leak types and conditional behavior into portable code.

## Decision

- Implement MonoGame and KNI in separate `net10.0` adapter packages and worker compositions behind one internal lifecycle/capability contract.
- The lifecycle covers capability query, initialization, snapshot load, fixed/variable update, framework-neutral render submission/frame production, pause/resume/stop, diagnostics/counters, and deterministic shutdown.
- Framework objects and content/device behavior remain inside each adapter. Portable contracts expose cameras, transforms, sprites, meshes, materials, lights, render layers, asset handles, and explicit optional capabilities.
- MonoGame is implemented and supported first against the full platform/conformance suite.
- KNI remains explicitly experimental until `.NET 10`, scene/lifecycle/frame/input/content/shutdown, packaging, and all desktop platforms pass the same suite. Failure does not downgrade the engine runtime/platform baseline.

## Consequences

Separate adapters duplicate some glue but keep portable contracts clean and make divergences measurable capabilities instead of hidden conditionals. KNI support can lag without blocking MonoGame or misrepresenting compatibility.

## Validation and acceptance gate

POC B must render the same sprite/static mesh and exercise the same lifecycle through each adapter. Slice 1 conformance evidence determines KNI's support label.

Current evidence (2026-07-24): MonoGame and KNI pass the same Windows and Ubuntu worker lifecycle, framework graphics-device, snapshot, shared-frame, Qt consumption, and recovery suites. macOS arm64 remains pending, so KNI remains experimental.
