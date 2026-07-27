# ADR-0011: Unity Contract Boundary and Bridge Prototype

> **Status:** Proposed
> **Last reviewed:** 2026-07-25
> **Design revision:** `DPE-ARCH-0009`

## Context

Dragon Pixel 1.0 includes a bounded Unity interoperability prototype, not a supported Unity replacement or a second implementation of the standalone editor. Unity has its own object ownership, serialization, lifecycle, rendering, editor APIs, runtime profiles, AOT restrictions, and coordinate conventions. Allowing those types or behaviors into portable engine contracts would couple the core to Unity and undermine MonoGame/KNI portability.

## Decision

- `DragonPixel.Unity.Contracts` targets `.NET Standard 2.1` and contains only AOT-safe DTOs, stable IDs, metadata descriptors, command/result envelopes, and serialization primitives. It does not reference Unity assemblies, Qt, MonoGame, KNI, standalone worker hosting, filesystem services, or runtime code generation.
- `DragonPixel.Unity.Bridge` is the only package that owns Unity APIs. It translates Dragon Pixel scene and command data to Unity objects and owns all Unity lifecycle, coordinate, matrix, unit, object-reference, serialization, and editor integration behavior.
- The bridge uses explicit version and capability negotiation. Unsupported or newer records are preserved or rejected with structured diagnostics; names and Unity instance IDs never replace Dragon Pixel stable IDs.
- Metadata and serializers are generated ahead of time. The bridge supplies the required managed-code stripping/link-preservation declarations and may not depend on reflection emission or dynamic code generation.
- The prototype implements a minimal round-trip scene and command exchange sufficient to validate contracts, identity, coordinate conversion, and error behavior. It does not promise full Unity editor integration, rendering parity, asset-pipeline replacement, arbitrary native plugin loading, or production support.
- Mono and IL2CPP-compatible paths consume the same portable fixtures. Unity-specific differences remain inside the bridge and are reported as capabilities rather than conditionals in portable contracts.

## Consequences and tradeoffs

The narrow boundary preserves portable contracts and provides an honest proof that Dragon Pixel data can survive Unity's AOT and lifecycle constraints. It requires duplicate Unity-side adapters, generated preservation metadata, and explicit coordinate/object mapping. The prototype deliberately exposes less behavior than a direct shared runtime or Unity-object abstraction, but avoids making Unity a dependency of the standalone engine.

## Security and ownership

- Unity owns every Unity object and Unity-side runtime resource. Dragon Pixel owns its serialized records, stable IDs, command semantics, and compatibility envelopes; neither side retains raw object, GC, native, or framework pointers owned by the other.
- Imported scene or command data is validated before Unity object mutation. Bridge operations are bounded and report partial or unsupported mappings rather than silently dropping data.
- Project assemblies or native libraries are not loaded into the standalone editor through the bridge. Any Unity native-plugin behavior remains Unity-specific and outside the portable contracts.
- Diagnostics and test artifacts exclude secrets and arbitrary project content unless explicitly supplied as fixtures.

## Validation and evidence gate

POC Q must prove the Unity 6.3 `.NET Standard 2.1` boundary through Mono and IL2CPP-compatible builds, AOT/link preservation, stable-ID and unknown-data behavior, coordinate/matrix/unit golden fixtures, minimal command and scene round trips, capability/version mismatch handling, and the absence of Unity, framework, or editor types from portable contracts. Portable contract and serialization fixtures must pass on Windows 11 x64, macOS 14+ arm64, and Ubuntu 24.04 x64; Unity execution must cover the documented Unity 6.3 host/target matrix for both required runtime paths.

This ADR remains `Proposed` until POC Q evidence is reviewed. Passing the prototype does not promote Unity to a supported full integration or change the 1.0 non-goals.
