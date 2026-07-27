# ADR-0015: Embedded Play Input and Consumption Correlation

> **Status:** Proposed
> **Date:** 2026-07-25
> **Design revision:** `DPE-ARCH-0013`

## Context

The visible Play surface is a Qt widget while MonoGame/KNI run in child processes with private framework windows. Polling input in those hidden worker windows targets the wrong focus owner. Earlier `viewportInput` evidence carried only a revision and did not change pixels, so it proved transport correlation rather than user/game-input consumption.

## Decision

- The focused Qt Play viewport owns embedded input capture. It normalizes physical events into canonical control tokens and framework-neutral action values. Qt key codes, native scan codes, and MonoGame/KNI input enums do not cross the portable boundary.
- `DragonPixel.Contracts` defines immutable full-state input snapshots with monotonic revision, capture/focus state, action kind/value, and press/release counters. Full states make crash/reconnect recovery and release-all behavior explicit.
- JSON-RPC adds capability-negotiated `runtimeInput`. Only the play worker receives gameplay input in the first slice. Stale/malformed revisions fail without replacing the current state.
- Focus loss, capture release, Pause, Stop, worker exit, and restart send or initialize a neutral snapshot. Auto-repeat does not create repeated press edges.
- Edit-mode camera/gizmo shortcuts are disabled while Play capture owns them. Escape releases capture; it is not silently delivered as both an editor and gameplay command.
- The worker applies the newest state during update/render and tags a frame only after the matching state was consumed by runtime behavior. Shared-frame header version 2 already contains the input revision and remains sufficient.
- MonoGame and KNI adapters receive the same portable state. The built-in managed `InputMotion2D` proof consumes `move.x` and `move.y` and alters only a disposable Play transform. Saved authoring data never changes.
- `dpe.inputmap` version 1 is the project-owned, framework-neutral binding document referenced by an `input-map` asset sidecar. It contains stable named control maps, actions, and bindings using canonical keyboard, mouse, and standard-gamepad paths. The existing full-state protocol carries evaluated actions without exposing device enums.
- The focused Qt Game view captures keyboard and mouse state, while an editor-owned SDL 3 adapter polls standardized hot-plug-aware gamepad state. `InputMapService` alone validates and atomically persists rebinding; invalid documents do not replace the last valid map or authoritative bytes.
- The default Gameplay map binds `move.x`/`move.y` to WASD, arrows, left stick, and d-pad; representative `jump`, `look.x`/`look.y`, and `fire` actions cover keyboard, mouse, and gamepad. `InputMotion2D` and `MyMover` consume the same device-neutral movement actions.
- Projects without a valid map retain an in-memory compatibility binding for the previous keyboard behavior. Text/IME, touch, locked raw-relative pointer mode, rumble, sensors, controller-specific extensions, and cloud binding profiles remain deferred.

## Consequences

Embedded Play behaves like the visible Qt application rather than an unfocused child window, and latency evidence can prove a user-visible effect. The editor must manage capture/focus carefully, the protocol carries another monotonic state, and tests must compare real pixels/picks instead of acknowledging receipt.

## Alternatives considered

- **Poll the worker framework window:** rejected for embedded Play because it is not the visible focused surface.
- **Forward raw OS or Qt key codes:** rejected because they are platform/toolkit-specific and unsuitable for replay or adapters.
- **Treat request acknowledgement or echoed revision as consumption:** rejected because pixels may be unchanged.
- **Persist toolkit/framework enums:** rejected because numeric values and device APIs are not portable; canonical paths remain durable while adapters translate them.
- **Send individual key events only:** rejected because a lost release or restart can leave stuck state; full snapshots plus edge counters recover deterministically.

## Validation and acceptance gate

POC J must prove schema/round-trip determinism, invalid-document preservation, atomic save recovery, map/action/binding authoring, fallback compatibility, real Qt keyboard/mouse events, injected and actual standard-gamepad samples, hot plug/removal, persistent rebinding, shortcut suppression, focus/pause/stop/crash/map replacement neutralization, monotonic validation, shared `InputMotion2D`/`MyMover` consumption, unchanged authoring JSON, device-produced MonoGame/KNI pixel/pick changes, and median action-to-first-reflecting-frame below 100 ms at 1280x720 and at least 30 FPS on all three baselines. This ADR remains `Proposed` until that evidence is reviewed; old revision-echo measurements are not acceptance evidence.

Current Windows evidence (2026-07-25): the focused Game widget emits complete `focused`/`captured` snapshots for `move.x`, `move.y`, and `jump`, including action kind, value, and monotonic press/release counters. Auto-repeat is deduplicated, edge counters advance on neutral/active or sign transitions, and focus loss, capture release, Pause, Stop, crash, and restart force neutral state. Protocol-v2 validation is transactional: malformed, duplicate, regressing, or future correlations cannot partially replace accepted state; a rejected runtime snapshot fails closed by restarting the Play worker and requires a neutral acknowledgement before input is re-enabled. Command and gameplay-input revisions are tracked separately, and an unreflected correlation expires after five real seconds.

`InputMotion2D` consumes the immutable snapshot into a runtime-only transform. MonoGame and KNI independently prove that `move.x` changes actual device-produced pixels and the retained ID-buffer pick location, followed by a stable neutral frame, without changing authoring JSON. The current Windows strict Release and MSVC AddressSanitizer matrices both pass 45 of 45 tests while retaining the unchanged 1280×720/30 FPS/100 ms gates; ASan completes in 368.80 seconds.

The remaining POC J gate is the aggregate real Qt key-event-to-first-reflecting-Qt-paint median/tail measurement, together with complete shortcut-suppression and current Ubuntu/macOS evidence. The protocol currently exercises separate one-dimensional move axes; the available `Axis2D` DTO is not yet exercised end to end. This ADR remains `Proposed`.

DPE-ARCH-0013 implementation evidence (2026-07-26): Windows now indexes and loads a project-owned `dpe.inputmap` v1 asset, uses a validated atomic `InputMapService`, replaces the hard-coded Game-view binding table, captures Qt keyboard/mouse state, polls SDL 3.4.12 standardized gamepads, and exposes named map/action/binding authoring and rebinding through the editor. The same default `move.x`/`move.y` actions feed `InputMotion2D` and `MyMover`. Five focused Release aliases pass 5/5 in 94.67 seconds and five MSVC AddressSanitizer aliases pass 5/5 in 116.79 seconds; coverage includes deterministic unknown-field preservation, validation and containment, stale-write rejection, custom keyboard/mouse/gamepad evaluation, analog dead zones, transient/lifecycle neutralization, indexing/loading, and dialog interaction. The 179-entry production bundle hash-verifies and its packaged MonoGame self-test exits zero. No physical standard-gamepad/hot-plug run, aggregate real Qt input-to-paint timing, or current Ubuntu/macOS evidence was performed, so this ADR remains `Proposed` and POC J remains open.

Follow-up evidence (2026-07-26): Windows now exposes the input editor as Project Input Settings, including map enabled state, action rename, and binding path/scale/dead-zone editing. Generated C# movers and the exact sample `MyMover` expose the same configurable Horizontal Action, Vertical Action, and Speed fields as `Input Motion 2D`; a dedicated exact-source test executes custom action names through the shared controller input and proves normalized delta-scaled Transform movement plus lifecycle/reset behavior. Inspector enabled indicators are high contrast, and newly created C# scripts schedule the isolated component build automatically. Ten focused Release aliases pass 10/10 in 133.55 seconds and ten MSVC AddressSanitizer aliases pass 10/10 in 157.17 seconds. The 183-entry production bundle hash-verifies and passes its packaged MonoGame self-test. This strengthens Windows authoring/script evidence but does not satisfy physical-gamepad/hot-plug, aggregate Qt input-to-paint, current Ubuntu/macOS, or complete POC J acceptance; this ADR remains `Proposed`.

## Primary sources

Verified 2026-07-25:

- [Qt `QWidget` event handling](https://doc.qt.io/qt-6/qwidget.html)
- [Qt `QKeyEvent`](https://doc.qt.io/qt-6/qkeyevent.html)
- [MonoGame keyboard input](https://docs.monogame.net/api/Microsoft.Xna.Framework.Input.Keyboard.html)
- [MonoGame input management](https://docs.monogame.net/articles/tutorials/building_2d_games/11_input_management/)
- [Qt `QMouseEvent`](https://doc.qt.io/qt-6/qmouseevent.html)
- [Qt `QWheelEvent`](https://doc.qt.io/qt-6/qwheelevent.html)
- [SDL 3 gamepad API](https://wiki.libsdl.org/SDL3/CategoryGamepad)
- [SDL 3 gamepad enumeration](https://wiki.libsdl.org/SDL3/SDL_GetGamepads)
- [SDL 3 gamepad polling](https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads)
- [SDL zlib license](https://www.libsdl.org/license.php)
