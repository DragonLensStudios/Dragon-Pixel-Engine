# Dragon Pixel Engine Slice 1 Windows Development Environment Plan

> **Plan status:** Completed
> **Created:** 2026-07-24
> **Architecture baseline:** `DPE-ARCH-0002`
> **Repository:** `C:\Projects\Github\Engines\Dragon Pixel Engine`
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine Slice 1 Windows Development Environment Plan.md`

## Outcome

Prepare the Windows 11 x64 workstation for Dragon Pixel Engine architecture validation and Slice 1 development. The setup must provide a repeatable C++20, CMake, Qt 6.11.1, .NET 10, and test workflow without beginning the production editor or bypassing the required ADR and POC gates.

## Baseline Inventory

| Capability | Initial state | Required state |
| --- | --- | --- |
| Visual Studio | Enterprise 2026 18.5.2 installed without native C++ components | Visual Studio 2026 IDE integration plus Visual Studio 2022 Build Tools with MSVC v143 x64/x86, AddressSanitizer, vcpkg, CMake integration, and Windows 11 SDK |
| .NET | SDK 10.0.203 installed | Preserve and verify `net10.0` builds |
| CMake | Not on `PATH` | Kitware CMake 4.4.0 or newer compatible release |
| Ninja | Not on `PATH` | Ninja 1.13.2 or newer compatible release |
| Qt | Not installed | Qt 6.11.1 MSVC 2022 x64 dynamic binaries with Core, GUI, and Widgets |
| Python tooling | Python, `uv`, and `pipx` available | Use isolated tooling only; do not add Python to the real-time runtime |

## Decisions

- Use the installed Visual Studio 2026 IDE as the interactive host and Visual Studio 2022 Build Tools as the command-line compiler installation. Qt 6.11 officially supports MSVC 2022 on Windows 11. A v143 toolset inside Visual Studio 2026 compiles Qt successfully but its installed ASan component supplies only the newer toolset's x64 runtime, so the side-by-side 2022 Build Tools installation is required for supported-matrix x64 sanitizer tests.
- Install CMake and Ninja as command-line tools so builds and tests work consistently from PowerShell, Visual Studio, and automation.
- Install Qt 6.11.1 `win64_msvc2022_64` binaries under `C:\Qt`. Qt remains dynamically linked; deployment and LGPL compliance artifacts are later release work.
- Use `aqtinstall` pinned to merged commit `8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b` to acquire the public Qt binary archives without storing Qt account credentials. Stable `aqtinstall` 3.3.0 predates Qt 6.11's Windows repository layout; the pinned upstream fix is required until a later stable release contains it. The installed Qt artifacts still come from Qt's public package repository.
- Keep vcpkg available for later manifest-mode dependencies, but do not add project dependencies or a `vcpkg.json` until an ADR/POC requires them.
- Add repository bootstrap and verification scripts. The verifier must create its smoke project only in a disposable temporary directory and must not introduce production engine/editor modules.

## Execution

1. Repair and verify all mandatory Markdown mirrors, then read the Design and Prompt/Result documents completely.
2. Record this plan in `docs\Plans` and the external `Plans` mirror; update `AGENTS.md` so all future plans use these locations.
3. Add a reusable `.vsconfig` and Windows install script for Visual Studio IDE integration plus the Visual Studio 2022 Build Tools native workload, v143 toolset, CMake integration, ASan, vcpkg, and Windows 11 SDK.
4. Install global CMake and Ninja command-line packages and Qt 6.11.1 MSVC 2022 x64.
5. Add an environment verification script that checks tool versions and compiles/tests disposable C++20, Qt Widgets, and .NET 10 smoke programs.
6. Run verification from a normal PowerShell process and from the Visual Studio developer environment.
7. Record exact versions, commands, failures, and final status here; verify every mirrored Markdown pair by SHA-256.

## Verification and Acceptance

- `cl` resolves from Visual Studio 2022 Build Tools to an MSVC v143 compiler after developer-environment initialization and compiles C++20 plus an AddressSanitizer smoke target.
- `cmake`, `ctest`, and `ninja` are callable from PowerShell.
- CMake finds Qt 6.11.1 Core, GUI, and Widgets from the pinned MSVC 2022 x64 installation.
- A disposable `QApplication`/`QMainWindow`/`QDockWidget` smoke executable builds and passes headlessly with the offscreen platform plugin.
- A disposable `net10.0` console project restores and builds.
- The verification command returns nonzero on a missing or incompatible required dependency and prints actionable remediation.
- No production editor, engine module, framework adapter, or project format is implemented by this environment work.
- The plan files and every pre-existing mirrored document pair are byte-identical at completion.

## Sources Reviewed 2026-07-24

- [Visual Studio command-line installation](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio)
- [Visual Studio workload and component IDs](https://learn.microsoft.com/en-us/visualstudio/install/workload-and-component-ids)
- [Qt 6.11 supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt 6.11 command-line installation](https://doc.qt.io/qt-6/get-and-install-qt-cli.html)
- [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [vcpkg manifest mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)
- [aqtinstall documentation](https://aqtinstall.readthedocs.io/en/stable/)
- [aqtinstall Qt 6.11 Windows layout fix](https://github.com/miurahr/aqtinstall/pull/1000)

## Execution Record

- Visual Studio native components, CMake 4.4.0, and Ninja 1.13.2 installed successfully.
- The first Qt attempt with stable `aqtinstall` 3.3.0 failed before download because Qt 6.11 uses a new per-architecture Windows repository layout. Upstream PR 1000 is merged but unreleased; its exact merge commit is now pinned rather than using a moving branch.
- Qt 6.11.1 installed and the C++20/Qt Widgets/CTest and `.NET 10` smoke checks passed.
- The first x64 ASan link through Visual Studio 2026's v143 toolset exposed a missing v143 x64 ASan runtime even though the generic ASan component reports installed. The plan now installs and verifies Visual Studio 2022 Build Tools side-by-side before repeating the complete gate.
- Visual Studio 2022 Build Tools 17.14.37 installed side-by-side, and the final verification passed with MSVC 14.44.35207, CMake 4.4.0, Ninja 1.13.2, .NET SDK 10.0.203, vcpkg `2025-11-19-da1f056`, and Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`.
- CTest ran both the offscreen `QApplication`/`QMainWindow`/`QDockWidget` target and the x64 AddressSanitizer target: 2 of 2 tests passed. `dumpbin` confirmed dynamic linkage to `Qt6Widgets.dll`; the disposable `.NET 10` project built with zero warnings and zero errors.
- CMake reported optional Vulkan headers as absent. This does not block the Qt Widgets environment or Windows Direct3D-backed POC work; install a Vulkan SDK later only if an accepted rendering prototype selects that backend.
- No production editor or engine modules were created. The next project step remains the foundational ADR set, followed by POC A and POC C as required by `DPE-ARCH-0002`.
- The installer completed an idempotent rerun without reinstalling existing packages. The final recursive audit found six Markdown pairs across the documentation roots; every pair has the same SHA-256 hash and valid UTF-8/LF bytes. The historical `Dragon Pixel Engine Initial Plan.md` intentionally retains its original lack of a final newline while remaining LF-only and byte-identical.

## Post-Plan Handoff

At `DPE-ARCH-0005`, the ADR drafts, POCs A-D, S1.0 bootstrap, and locally executable Slice 1 editor/runtime scope described as later work above have been implemented. Windows and Ubuntu now pass the 15-test Release and native AddressSanitizer matrices; macOS arm64 remains the final Slice 1 acceptance gate. This completed environment plan remains the historical setup record and is superseded for active delivery tracking by `Dragon Pixel Engine Slice 1 Complete Execution Plan.md`.
