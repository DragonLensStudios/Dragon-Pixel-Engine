# Qt 6.11.1 LGPL Deployment Inventory

> **Status:** Windows development inventory recorded; distribution package and legal review pending
> **Recorded:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0005`
> **License mode:** Dynamically linked Qt under LGPLv3 requirements

## Purpose

Record the Qt modules and deployment obligations visible in the current Slice 1 Windows editor build. This is an engineering compliance checklist, not legal advice, and it does not approve a distributable release.

## Build linkage

The editor CMake target directly requests Qt 6.11.1 Widgets and Network and links them dynamically. A dry run of Qt 6.11.1 `windeployqt` against the current Windows Release editor identified these Qt runtime libraries:

- `Qt6Core.dll`
- `Qt6Gui.dll`
- `Qt6Network.dll`
- `Qt6Svg.dll` (deployment-time transitive module)
- `Qt6Widgets.dll`

The deployment scan also identified:

- `opengl32sw.dll` and `D3Dcompiler_47.dll`
- `generic/qtuiotouchplugin.dll`
- `iconengines/qsvgicon.dll`
- GIF, ICO, JPEG, and SVG image-format plugins
- the Windows network-information plugin
- `platforms/qwindows.dll`
- `styles/qmodernwindowsstyle.dll`
- certificate-only and SChannel TLS plugins
- Qt/QtBase translation catalogs

The dry run warned that optional `dxcompiler.dll` and `dxil.dll` were not found. The current Slice 1 editor does not select Direct3D 12 features that require them. This must be reevaluated if the renderer or Qt RHI configuration changes.

## Required distribution work

Before any Dragon Pixel Engine binary distribution that includes LGPL Qt libraries:

1. Ship the complete applicable LGPLv3 license text and prominent Qt copyright/license notices.
2. Identify the exact Qt version, modules, build provenance, and any local modifications.
3. Provide the corresponding Qt source or a valid written/source-delivery mechanism for the exact distributed binaries, including the scripts/configuration needed to rebuild modified Qt portions when applicable.
4. Preserve the user's ability to replace or relink the LGPL Qt libraries. Do not statically link Qt for the planned distribution unless a separately reviewed relinking mechanism satisfies the license.
5. Ensure the product EULA, installer, signatures, platform protections, and update mechanism do not prohibit LGPL-permitted reverse engineering or replacement for debugging modifications to the library.
6. Keep third-party license notices for Qt's bundled/transitive libraries and plugins with the deployment artifact.
7. Run an automated artifact inventory against the final packaged directory; the development-time `windeployqt` list is not the final bill of materials.
8. Obtain legal review before the first external distribution.

## Platform status

- **Windows 11 x64:** development linkage and `windeployqt` inventory recorded. No release package has been approved.
- **Ubuntu 24.04 x64:** build/test linkage to the official Qt 6.11.1 dynamic libraries passes in the clean container. A deployable Linux bundle and its library/plugin/license inventory remain pending.
- **macOS 14+ arm64:** build, test, bundle, framework-signing, replacement/relinking, and notices evidence remain pending.

## Change triggers

Repeat this inventory whenever the Qt version, linked modules, plugins, renderer/RHI backend, TLS backend, packaging tool, installer, updater, signing/notarization flow, or license mode changes. Such a change may require an ADR and synchronized living-document update under `AGENTS.md`.

## Primary guidance

- [Qt open-source LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations), accessed 2026-07-24.
- [Qt 6.11 release information](https://doc.qt.io/qt-6/qt-releases.html), accessed 2026-07-24.

## Current conclusion

Dynamic Qt integration is consistent with the selected architecture on Windows and Ubuntu, but Slice 1 has only an engineering inventory. Distribution remains blocked on a final multi-platform artifact inventory, complete notices/source/relinking materials, and legal review.
