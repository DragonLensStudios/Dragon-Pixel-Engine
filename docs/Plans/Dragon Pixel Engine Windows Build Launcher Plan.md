# Dragon Pixel Engine Windows Build Launcher Plan

> **Status:** Implementation verified locally and ready for draft review
> **Branch:** `feature/windows-build-launcher`
> **Target:** `develop`
> **Owner:** Dragon Lens Studios / Codex
> **Started and updated:** 2026-11-24
> **Base:** `fa59b227e3057af603c569f1913b652d22b50c5a`
> **Dependencies:** Existing production editor scripts on `develop`

## Goal and scope

Provide a repository-root batch file that builds and launches the Windows Release editor from File Explorer or a terminal, including a checkout whose path contains spaces. Document the command and its output in the Windows quick start.

The production PowerShell script owns configuration, compilation, dependencies, bundle staging, and smoke testing. The existing editor launcher owns process startup and sample selection. This work adds no new build logic, dependency versions, schemas, ABI, worker protocols, data ownership, installation behavior, or support claims.

## Implementation and verification

1. Add a batch entry point using a quoted path relative to the batch file. Return the build script's exit code to the caller.
2. Run the batch file from an unrelated directory. Verify the Release bundle, packaged MonoGame smoke test, and actual editor process. Verify a blocked rebuild returns failure while preserving the running editor.
3. Verify the plan mirror and diff, push the branch and open a draft PR into `develop`. Human review and merge follow separately.

## Work log

- 2026-11-24. Fetched `origin`. The clean original checkout at `867b315` already matched its published feature branch in draft PR #7. Verified the four governing documentation pairs and revision `DPE-ARCH-0017` there. The independent launcher starts at current `origin/develop`, which carries earlier versions of some living documents. The external copies of those documents follow the active Project View checkout and are preserved.

## Verification and handoff

- From `C:\`, ran `& 'C:\Projects\Github\Engines\Dragon Pixel Engine\Build-And-Run.bat'` through Windows PowerShell 5.1. The initial Codex shell passed an empty Git configuration value through an override that vcpkg could not use. The batch returned exit 1. Cleared `GIT_CONFIG_COUNT` in the verification shell for the successful retry without changing user or machine configuration.
- CMake configured the Windows preset, vcpkg installed all six dependencies, and the initial native Release build completed 57 Ninja steps. Managed MonoGame and KNI worker builds passed with zero warnings and errors. CMake reported optional Vulkan headers missing.
- The first package attempt exposed an unavailable `Get-FileHash` command in the batch-driven session. Packaging now streams files through .NET SHA-256 and emits the same lowercase digest field. The subsequent launch exposed positional binding of the existing string-array arguments. Using named switches correctly selects production and skip-build mode while preserving the optional sample reset.
- The complete retry returned exit 0 after an incremental Release build, Qt deployment, packaged MonoGame self-test, and the 750 ms startup probe. Confirmed a responsive `Dragon Pixel Engine Editor — Main` window and the writable development sample. Independently checked length and SHA-256 for all 144 manifest records.
- Reinvoked the batch with the production editor open. It returned exit 1 with the close-editor diagnostic. The same editor process remained active and was left open.
- The production editor SHA-256 is `2eeb9fad8678b0d8ab2a5de5b5580409c81eb9411d749d4cf4955ad19ac52ab8`. The bundle manifest SHA-256 is `b8ac706069515d7ff07819ef0c0d87f98c14e5677d6eb05abe76debbdf18df37`.
- Compared all 68 mirrored Markdown files in the original Project View Git tree with their external copies. All matched byte for byte. The launcher plan has its own byte-identical external copy. The broader engine test matrix and other platforms were outside this Windows launcher verification.
- Review through the draft PR from `feature/windows-build-launcher` into `develop`. The repository checkout is on the launcher branch. Human review and merge remain pending.
