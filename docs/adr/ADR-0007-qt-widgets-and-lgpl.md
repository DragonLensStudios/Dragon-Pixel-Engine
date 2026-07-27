# ADR-0007: Qt Widgets Editor and LGPL Compliance

> **Status:** Proposed
> **Date:** 2026-07-24
> **Last reviewed:** 2026-07-27
> **Design revision:** `DPE-ARCH-0014`

## Context

The standalone editor needs cross-platform docking, model/view controls, real worker-frame integration, keyboard navigation, accessibility, testable interactions, and mature desktop deployment. Slice 2 adds multi-selection, drag/drop, typed property editing, scene gizmos, project browsing, structured diagnostics, and reusable workspaces. The engine is MIT-licensed, while Qt's open-source use introduces separate LGPL obligations.

## Decision

- Build the editor in C++20 with dynamically linked Qt 6.11 Widgets. `QApplication` owns the event loop; `QMainWindow` owns menus/toolbars and stable `QDockWidget` panels; a custom render-capable Scene View consumes the replaceable frame transport without owning scene state.
- Built-in panels have stable IDs/object names and are views over editor services. Hierarchy and Project Explorer use service-backed `QAbstractItemModel` implementations with persistent domain IDs rather than panel-owned `QTreeWidget`/`QListWidget` data. Inspector, Console, Scene View, and toolbar actions likewise observe services and mutate only through commands.
- Hierarchy supports filtering, ordered multi-selection, inline rename/enable, keyboard navigation, context actions, and drag reparent/reorder. Project Explorer supports manifest-rooted folders, scenes, prefabs, assets, search/type filters, status/diagnostics, asynchronous thumbnails, scene opening, compatible assignment, and drag-to-scene creation; general filesystem management remains deferred.
- Inspector uses scrollable component cards and the typed metadata drawers in ADR-0005. Console uses a structured model with timestamp, severity, subsystem, worker/session, correlation ID, context, text filters, clear/copy/export, and entity/asset navigation.
- Scene View provides one 2D/3D mode switch, orthographic pan/zoom, perspective orbit/pan/fly, focus selection, real picking, selection outlines, grid and camera/light/collider overlays, Move/Rotate/Scale gizmos, local/global orientation, and snapping. Every pointer-only manipulation has a discoverable keyboard/action alternative where practical; inaccessible precision tasks have an equivalent typed Inspector path.
- Scene and Game are independent dockable viewport panels, tabified by default and usable side by side. Game adds aspect/fit/status controls and is the sole embedded Play input owner; both panels preserve focus, accessibility, high-DPI, and workspace-state requirements.
- Built-in `2D`, `3D`, and `Debug` workspaces define dock placement, visibility, toolbars, and Scene View mode. Users can restore a hidden panel from View, reset a workspace, and save their layout. Layout and editor-camera state live under the platform per-user application-data location, not in authoritative project or scene files. Invalid/restored-older layout data falls back to the built-in workspace without blocking project open.
- Prefer standard Qt controls and platform conventions. Define explicit focus order and shortcuts; expose accessible names, descriptions, roles, states, and actions; preserve visible focus; support keyboard-only operation, high DPI, high contrast, screen-reader inspection, and text scaling. Custom Scene View/gizmo accessibility is a release requirement, not a best-effort annotation.
- Action enabled/checked states are derived from services: selection, project validity, dirty state, undo/redo availability, active tool, and runtime state. Invalid actions are disabled with an accessible explanation instead of silently doing nothing. Save/Discard/Cancel and destructive confirmations use injectable prompt services so UI tests exercise the same public action paths as users.
- Use Qt Test for POC H and production interaction tests. Tests send real mouse, key, drag/drop, menu, dialog, focus, and model-view events; they do not claim UI coverage by calling private slots/handlers directly. Accessibility tests inspect the exposed object tree/actions and cover keyboard traversal and high-contrast behavior.
- Dynamically link Qt. Initial required modules are Core, GUI, Widgets, and Test for test artifacts; every added runtime module receives dependency, deployment, and license inventory review.
- Distribution must include Qt/LGPL notices, corresponding-source offer/material, relinking rights/material, module/version inventory, and no restriction that conflicts with LGPL rights. A pre-release legal/compliance review is mandatory; this ADR is not legal advice.
- Use Qt-supported compiler/architecture combinations for Windows, macOS, and Linux. Dependency versions are centrally pinned and primary compatibility/licensing facts are reverified before upgrades or after the 90-day review window.

## Consequences

Qt accelerates a polished, model-driven, accessible desktop shell and makes real interaction testing practical. It adds model/view implementation, custom viewport accessibility, binary deployment, platform plugin discovery, version pinning, workspace migration, and LGPL compliance work. Stable panel IDs and service ownership prevent a saved layout or recreated dock from becoming authoring state.

DPE-ARCH-0009 extends the accessibility boundary to installers, the external update helper, update/rollback prompts, and uninstall workflows. They must support keyboard-only operation, exposed names/roles/states/actions, visible focus, high DPI, high contrast, text scaling, and correct localization/encoding on every supported platform. Every distribution must also carry the complete Qt module/platform-plugin inventory, applicable licenses/notices, corresponding-source offer or material, relinking instructions and material, and terms that do not restrict LGPL relinking rights. Automated artifact checks supplement but do not replace the mandatory pre-release legal/compliance review.

## Alternatives considered

- **Panel-owned item widgets:** suitable for prototypes but rejected for Slice 2 because they duplicate domain state, make stable selection/drag behavior fragile, and are harder to test and virtualize.
- **Dear ImGui as the application shell:** retained for optional debug tooling but rejected as the primary editor because docking alone does not provide the required native desktop semantics and accessibility model.
- **Persist layouts inside project files:** rejected because personal monitor/dock/camera choices are non-authoritative and would create source-control churn.
- **Private-handler UI tests:** rejected because they bypass Qt event routing, focus, enable states, dialogs, drag/drop, and accessibility behavior.

+## DPE-ARCH-0014 refinement

The production no-project surface is a Project Hub. Project Browser becomes a service-backed two-pane folder/content surface with breadcrumb, grid/list, search/type/status filters, preview/details, accessible context and keyboard actions, and versioned drag/drop. Hierarchy adds public multi-root drag/reorder and management actions. Inspector is a reusable dock; View > New Inspector creates another stable-ID dock, its count/layout is per-user state, and every restored panel reopens unlocked. Qt views remain interaction surfaces over lifecycle, asset, selection, command, prefab, and metadata services and may not mutate filesystem or scene state directly.

## Validation and acceptance gate

POC B must prove docking/viewer behavior and frame integration on all platforms. POC H must prove real menus, inline editors, dialogs, model selection, multi-selection, drag/drop, gizmo commit/cancel, shortcuts, focus traversal, action states, workspace reset/restore, high-DPI behavior, and accessibility exposure on all three baselines. End-to-end tests use disposable project copies and must show that authoring, save/reopen, layout restoration, console navigation, and worker recovery require no JSON edits and never write the source sample. Release remains blocked pending the deployment inventory and required compliance review.

DPE-ARCH-0009 adds POCs R and S for clean-installed editor/installer/updater/uninstaller accessibility, complete package-level Qt inventory and LGPL materials, and recorded legal review on Windows, macOS, and Ubuntu. No POC R/S or legal-review evidence is accepted yet. The existing POC H, platform, accessibility, distribution, and legal gates remain unchanged.

Current evidence (2026-07-25): the Qt shell, stable docks, model-backed Hierarchy/Project Explorer/Inspector/Console foundations, typed editing and validation, public-action command flows, workspace persistence, shared real-frame integration, and earlier POC H coverage pass on Windows and historical Ubuntu. Windows additionally exercises nested drawers, rooted component creation/build actions, the TileSet wizard/palette, separate Scene/Game docks, full-state focused Game input, fail-closed worker recovery, and candidate project-open safety through public Qt tests. The Windows strict Release and MSVC AddressSanitizer matrices both pass 45/45; the two full Qt registrations complete under ASan in 95.78 and 91.47 seconds. Ubuntu's last pre-DPE-ARCH-0008 matrices remain 36/36. Manual Windows QA also passed launch, authoring, viewport, save/reopen, and runtime controls.

The current macOS POC H matrix, complete drag/drop/gizmo and designer workflow coverage, full keyboard/accessibility/high-DPI/high-contrast review, distribution materials, and pre-release legal review remain open. The prior macOS registered editor tests predate this expanded POC H suite. The ADR therefore remains `Proposed`.

Primary sources verified 2026-07-24: [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html), [`QDockWidget`](https://doc.qt.io/qt-6/qdockwidget.html), [Qt model/view programming](https://doc.qt.io/qt-6/model-view-programming.html), [Qt Widgets accessibility](https://doc.qt.io/qt-6/accessible-qwidget.html), [Qt Test overview](https://doc.qt.io/qt-6/qtest-overview.html), and [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).
