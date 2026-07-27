# ADR-0017: Named Scene and Game Viewports

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0009`

## Context

The existing Qt viewport switches one widget between preview and Play. Designers need to keep authoring context visible while observing the primary camera, and embedded Play input must belong to the visible Game surface rather than a hidden framework window.

## Decision

- Scene and Game are independent dockable panels, tabified by default and allowed side by side. Layout and camera state are per-user, non-authoritative state.
- Preview publishes named `scene` and `game` outputs. Scene uses the editor camera and authoring selection; Game uses the unique enabled primary scene camera. Missing or ambiguous primaries produce a visible structured diagnostic.
- Each output has independent dimensions, purpose, camera source, shared-memory mapping, revisions, and retained pick target. View IDs are negotiated protocol values rather than widget pointers or framework objects.
- Play publishes only the `game` output from an immutable snapshot. Scene remains bound to Preview. Stop/crash discards Play and returns Game to the live primary-camera Preview output.
- Only the focused Game panel captures framework-neutral Play input. Focus loss, Pause, Stop, crash, and capture release publish a neutral state.
- Game provides free/common aspect presets, fit/scale, diagnostics, adapter identity, and frame statistics. Scene retains all authoring input and tile/gizmo tools.

## Consequences

One preview graphics device renders two cameras and owns two frame mappings. This costs an additional render/readback but avoids duplicate preview processes and keeps resource state consistent. Worker supervision, resize correlation, and Qt tests become view-aware.

## Validation and acceptance gate

POC L must prove simultaneous device-produced Scene/Game pixels, unique-primary diagnostics, independent resize/revision/picking, Scene interaction during Play, Game-only input, Stop isolation, mapping replacement, and crash recovery through MonoGame and KNI on all baseline platforms. This ADR remains `Proposed` until that evidence is reviewed.

Current Windows evidence (2026-07-25): the Qt editor exposes independently dockable Scene and Game panels. Stable view IDs, purposes, sizes, camera sources, input revisions, and independent shared-frame mappings are negotiated; Game shows a live primary-camera preview outside Play, switches to an isolated Play session, and returns on Stop. Qt interaction tests prove three separately supervised Scene-preview, Game-preview, and Play worker processes, Scene continuity, full-state focused Game input, neutralization/fail-closed restart, and crash recovery. The current Windows strict Release and MSVC AddressSanitizer matrices both pass 45 of 45 tests.

The implemented Windows topology currently uses one worker per named output rather than one preview graphics device producing concurrent Scene and Game mappings. POC L must still measure and decide that topology, add simultaneous device-pixel/resize/picking assertions, and pass the current Ubuntu/macOS matrices. This ADR remains `Proposed`.
