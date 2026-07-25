# ADR-0015: Embedded Play Input and Consumption Correlation

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0007`

## Context

The visible Play surface is a Qt widget while MonoGame/KNI run in child processes with private framework windows. Polling input in those hidden worker windows targets the wrong focus owner. Existing `viewportInput` only carries camera/selection revisions, and its test advances a revision without changing pixels. The previously reported latency therefore proves transport correlation, not user/game-input consumption.

## Decision

- The focused Qt Play viewport owns embedded input capture. It normalizes physical events into canonical control tokens and framework-neutral action values. Qt key codes, native scan codes, and MonoGame/KNI input enums do not cross the portable boundary.
- `DragonPixel.Contracts` defines immutable full-state input snapshots with monotonic revision, capture/focus state, action kind/value, and press/release counters. Full states make crash/reconnect recovery and release-all behavior explicit.
- JSON-RPC adds capability-negotiated `runtimeInput`. Only the play worker receives gameplay input in the first slice. Stale/malformed revisions fail without replacing the current state.
- Focus loss, capture release, Pause, Stop, worker exit, and restart send or initialize a neutral snapshot. Auto-repeat does not create repeated press edges.
- Edit-mode camera/gizmo shortcuts are disabled while Play capture owns them. Escape releases capture; it is not silently delivered as both an editor and gameplay command.
- The worker applies the newest state during update/render and tags a frame only after the matching state was consumed by runtime behavior. Shared-frame header version 2 already contains the input revision and remains sufficient.
- MonoGame and KNI adapters receive the same portable state. A built-in managed `Input Mover` consumes the `Move` action and alters only a disposable Play transform. Saved authoring data never changes.
- The first implementation supports keyboard buttons and a Move action using WASD/arrows. Mouse actions, gamepads, text/IME, touch, raw-relative input, durable rebinding, and `dpe.inputmap` are deferred.

## Consequences

Embedded Play behaves like the visible Qt application rather than an unfocused child window, and latency evidence can prove a user-visible effect. The editor must manage capture/focus carefully, the protocol carries another monotonic state, and tests must compare real pixels/picks instead of acknowledging receipt.

## Alternatives considered

- **Poll the worker framework window:** rejected for embedded Play because it is not the visible focused surface.
- **Forward raw OS or Qt key codes:** rejected because they are platform/toolkit-specific and unsuitable for replay or adapters.
- **Treat request acknowledgement or echoed revision as consumption:** rejected because pixels may be unchanged.
- **Persist bindings immediately:** deferred to a versioned input-map contract and dedicated authoring UX.
- **Send individual key events only:** rejected because a lost release or restart can leave stuck state; full snapshots plus edge counters recover deterministically.

## Validation and acceptance gate

POC J must prove real Qt events, shortcut suppression, focus/pause/stop/crash neutralization, monotonic validation, runtime-only movement, unchanged authoring JSON, device-produced MonoGame/KNI pixel/pick changes, and median action-to-first-reflecting-frame below 100 ms at 1280x720 and at least 30 FPS on all three baselines. This ADR remains `Proposed` until that evidence is reviewed; old revision-echo measurements are not acceptance evidence.

## Primary sources

Verified 2026-07-25:

- [Qt `QWidget` event handling](https://doc.qt.io/qt-6/qwidget.html)
- [Qt `QKeyEvent`](https://doc.qt.io/qt-6/qkeyevent.html)
- [MonoGame keyboard input](https://docs.monogame.net/api/Microsoft.Xna.Framework.Input.Keyboard.html)
- [MonoGame input management](https://docs.monogame.net/articles/tutorials/building_2d_games/11_input_management/)

