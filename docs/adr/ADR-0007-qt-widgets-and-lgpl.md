# ADR-0007: Qt Widgets Editor and LGPL Compliance

> **Status:** Proposed
> **Date:** 2026-07-24
> **Design revision:** `DPE-ARCH-0005`

## Context

The standalone editor needs cross-platform docking, model/view controls, render integration, keyboard navigation, accessibility, and mature desktop deployment. The engine is MIT-licensed, while Qt's open-source use introduces separate LGPL obligations.

## Decision

- Build the editor in C++20 with Qt 6.11 Widgets, using `QApplication`, `QMainWindow`, and `QDockWidget`; use a render-capable Qt widget behind the replaceable frame transport.
- Dynamically link Qt. Initial required modules are Core, GUI, and Widgets; each additional module requires an inventory/license/deployment review before adoption.
- Prefer standard Qt controls. Custom viewport/gizmo controls must implement accessible names, roles, focus order, keyboard actions, high contrast, and assistive behavior.
- Distribution must include Qt/LGPL notices, corresponding-source offer/material, relinking rights/material, module/version inventory, and no restriction that conflicts with LGPL rights. A pre-release legal/compliance review is mandatory; this ADR is not legal advice.
- Use the official Qt-supported MSVC 2022 toolchain on Windows. Dependency versions are pinned and reverified before upgrades or after the fact-review window expires.

## Consequences

Qt accelerates a polished accessible desktop shell but adds binary deployment, plugin discovery, version pinning, and LGPL compliance work. Dear ImGui may serve debug tooling but not the primary editor shell.

## Validation and acceptance gate

POC B must prove docking/viewer behavior and frame integration on all platforms. Slice 1 must pass keyboard/accessibility smoke checks and an automated deployment inventory; release remains blocked pending the required compliance review.

Current evidence (2026-07-24): the Qt shell, docks, accessible names, offscreen editor flows, frame integration, and dynamically linked development configuration pass on Windows and Ubuntu. macOS artifact evidence, final multi-platform deployment materials, accessibility review, and pre-distribution legal review remain pending.

Primary sources: [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html), [`QDockWidget`](https://doc.qt.io/qt-6/qdockwidget.html), [Qt Widgets accessibility](https://doc.qt.io/qt-6/accessible-qwidget.html), and [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).
