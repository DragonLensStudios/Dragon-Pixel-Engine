# Dragon Pixel Engine MIT and Dependency Compatibility

> **Reviewed:** 2026-09-18
> **Scope:** Documentation baseline 0.1.0; DPE-ARCH-0017 unchanged
> **Disposition:** Project MIT license retained by explicit owner instruction; package compliance remains unqualified

## Project decision

The owner confirmed: **“Keep MIT and document dependency compatibility.”** Repository `LICENSE.md` remains unchanged, including copyright 2026 Monydragon. MIT permits commercial use, modification, and redistribution with its notice preserved and disclaims warranty. [OSI MIT text](https://opensource.org/license/mit), accessed 2026-09-18.

This is a clarification of existing licensing, not a relicensing of project history, dependencies, or third-party code. Documentation version 0.1.0 does not qualify an engine release.

## MonoGame and KNI

Verified upstream license files identify Microsoft Public License (Ms-PL) and additional MIT notices for inherited portions:

- [MonoGame LICENSE.txt](https://github.com/MonoGame/MonoGame/blob/develop/LICENSE.txt), accessed 2026-09-18.
- [KNI LICENSE.txt](https://github.com/kniEngine/kni/blob/main/LICENSE.txt), accessed 2026-09-18.
- [OSI Ms-PL text](https://opensource.org/license/ms-pl), accessed 2026-09-18.

MIT and Ms-PL are distinct. The working compatibility approach is to license Dragon Pixel's own work under MIT while retaining applicable licenses/notices on separately supplied dependencies. This does not authorize relabeling Ms-PL-derived source as MIT. Ms-PL section 3 requires preserving existing notices; its source distribution condition requires Ms-PL with a complete license copy, while compiled distribution must comply with that license. It also has patent and trademark conditions that MIT does not duplicate.

The inspected worker project files reference `MonoGame.Framework.DesktopGL` 3.8.5 and `nkast.Kni.Platform.SDL2.GL` 4.2.9001.1. Upstream branch license files establish the reviewed project licensing facts; they are not an audit of every file in those exact NuGet packages. Before binary distribution, inventory the exact restored packages and transitive/native payloads, preserve their actual notices, and review combined distribution terms. A framework adapter/process boundary is not itself a license exemption.

## Qt and other dependencies

The existing Qt decision uses dynamic LGPLv3 linking, with notices, corresponding-source materials, replacement/relinking rights, and required legal/compliance review before distribution. Some Qt modules have different terms, so review the actual module/plugin inventory. [Qt obligations](https://www.qt.io/development/open-source-lgpl-obligations), accessed 2026-09-18.

Retain the existing [Qt deployment inventory](Qt%206.11.1%20LGPL%20Deployment%20Inventory.md) and [Box2D/Jolt inventory](Box2D%203.1.1%20and%20Jolt%20Physics%205.6.0%20Dependency%20and%20License%20Inventory.md). Their recorded versions and evidence retain their original dates. SDL, JSON libraries, managed runtimes, framework content tools, graphics/native libraries, and transitive dependencies keep their own terms; project MIT does not replace them.

## Remaining distribution gate

The source-documentation baseline does not provide a complete SBOM, packaged license bundle, Qt source/relinking kit, signed installer, or legal approval. POCs R/S and ADR-0007/0021 retain those obligations. No license gate, support claim, architecture revision, or package policy was relaxed.
