# Box2D 3.1.1 and Jolt Physics 5.6.0 Dependency and License Inventory

> **Inventory status:** Development dependency inventory; distribution artifact audit remains required  
> **Architecture revision:** `DPE-ARCH-0006`  
> **Reviewed:** 2026-07-24  
> **Implementation evidence updated:** 2026-07-25  
> **Not legal advice:** Obtain appropriate legal review before distributing binaries.

## Scope

Dragon Pixel Engine uses two private native physics backends behind its framework-neutral Physics module and the negotiated `dpe_physics_api_v1` C ABI extension. Backend types are not public scene, managed, serialization, framework-adapter, or editor contracts.

| Dependency | Pinned version | Purpose | License | Upstream |
| --- | --- | --- | --- | --- |
| Box2D | 3.1.1 | Two-dimensional rigid-body simulation | MIT; copyright Erin Catto | [Release](https://github.com/erincatto/box2d/releases/tag/v3.1.1), [license](https://github.com/erincatto/box2d/blob/v3.1.1/LICENSE) |
| Jolt Physics | 5.6.0 | Three-dimensional rigid-body simulation | MIT; copyright Jorrit Rouwe | [Release](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0), [license](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE) |

These version and license claims were verified against the tagged upstream sources on 2026-07-24.

## Acquisition and build provenance

- `vcpkg.json` requests `box2d`, `joltphysics`, and `nlohmann-json`, overrides Box2D to 3.1.1 and Jolt to 5.6.0, and pins the vcpkg builtin baseline to `40f3c709db80acf154ac4b17a1f83c564ebd022e`.
- Windows developer and CI presets use the vcpkg toolchain exposed by `VCPKG_ROOT`.
- macOS and Ubuntu CI bootstrap the same vcpkg commit and use the same manifest.
- The Ubuntu 24.04 verification container bootstraps the same vcpkg commit under `/opt/vcpkg`.
- CMake resolves `box2d::box2d` and `Jolt::Jolt` as private link dependencies of `DragonPixelPhysics`. `CMAKE_POSITION_INDEPENDENT_CODE` is enabled so the native dependency graph links into the shared C ABI on Unix. The portable Core, Scene, editor, managed contracts, and saved formats do not link or expose backend headers.
- No optional physics backend feature set is declared in the public contract. A feature change or dependency upgrade requires conformance, ABI, sanitizer, and license-inventory review.

## Notices and distribution obligations

Both dependencies use the MIT license. Source and binary distributions must reproduce each dependency's copyright notice and permission text in the third-party notice materials accompanying the distributed product. The vcpkg-installed `share/box2d/copyright` and `share/joltphysics/copyright` files are useful build evidence, but build-tree files are not a release notice mechanism.

Before shipping a package:

1. Generate a package-specific dependency manifest from the exact release build.
2. Include the complete Box2D and Jolt MIT notices in the product's third-party notices.
3. Confirm whether static or dynamic artifacts for each backend are present and list the shipped filenames and architectures.
4. Record compiler, target triplet, vcpkg baseline, enabled features, and source hashes.
5. Scan transitive runtime files and include any additional required notices.
6. Verify notices in every Windows, macOS, and Linux distributable and obtain the planned compliance review.

## Current evidence and open work

The complete Windows MSVC Release matrix passed **36/36 tests in 118.39 seconds**, and the complete MSVC AddressSanitizer matrix passed **36/36 tests in 134.93 seconds**. Ubuntu Release passed **36/36 tests in 102.20 seconds**, and Ubuntu Clang AddressSanitizer passed **36/36 tests in 101.97 seconds**. All four builds resolve the pinned ports. Their registered POC G aliases exercise the private native facade, candidate-world rollback, fixed stepping, transforms, contacts/queries, world cleanup, negotiated C ABI extension, managed `SafeHandle` ownership, worker Simulate/Play/Pause/Stop isolation, unchanged source bytes, collider overlays through both real framework renderers, and rejection of unsupported multiple 3D colliders without discarding the prior valid world. The Ubuntu sanitizer environment propagates into managed framework workers and native-host child workers.

Manual Qt QA used only the disposable writable `out/dev/Slice1Sample` copy and confirmed isolated Simulate Preview plus KNI pause/Stop behavior. This is development evidence, not a package inventory or legal review. No current POC G Release/AddressSanitizer evidence exists for macOS arm64. Release-file enumeration, exact shipped static/dynamic artifact lists, source hashes, transitive-runtime scanning, packaged MIT notices, and final compliance review remain open.

No ADR is promoted on the strength of this inventory alone. ADR-0012 remains `Proposed` until POC G and its complete three-platform acceptance matrix pass.
