# Dragon Pixel Engine CI GitFlow Integration Plan

> **Status:** In progress
> **Branch:** `feature/ci-gitflow-integration`
> **Target:** `develop`
> **Owner:** Dragon Pixel Engine maintainers with Codex implementation support
> **Created:** 2026-07-27
> **Last updated:** 2026-07-27

## Goal and user value

Make the existing cross-platform GitHub Actions workflow a truthful GitFlow integration gate. A contributor and reviewer must be able to distinguish a real configured, built, and tested result from a job that merely reached the end of a shell step after an earlier command failed.

The smallest complete feature unit is repository-side CI enforcement for accepted GitFlow branch relationships, fail-closed build/test execution, non-empty test evidence, and the already-proven Windows, Ubuntu, and macOS portability blockers that prevent the current workflow from reaching meaningful tests. Repository branch-protection settings are deliberately deferred until these checks run successfully on the feature pull request and the reviewed feature is merged.

## Background and evidence

- Pull request #3 merged into `develop` as `b3281fa1820a1fa25bad6cb270a3aebbf024d3f7` on 2026-07-27.
- The final pull-request workflow showed Windows jobs as successful even though CMake configuration and build failed and CTest reported `No tests were found`. Windows PowerShell did not propagate native-command exit codes.
- The Ubuntu jobs stopped while vcpkg built `libxcrypt` because `autoconf`, `autoconf-archive`, `automake`, and `libtoolize` were absent.
- The macOS jobs reached compilation and stopped on `-Wmissing-field-initializers` errors in the metadata registry after descriptor fields were added.
- The workflow runs pushes for `main` and historical `codex/**` branches, but not the permanent `develop` branch or the accepted `feature/**`, `release/**`, and `hotfix/**` GitFlow namespaces.
- `develop` currently has no branch-protection rule or ruleset. GitHub documents that required checks should be uniquely named and must pass before merging once configured; it also documents branch filters for `push` and `pull_request` events. Sources accessed 2026-07-27: [Workflow syntax for GitHub Actions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax), [About protected branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches), and [Status checks](https://docs.github.com/en/pull-requests/reference/status-checks).

## Scope

1. Replace historical workflow branch filters with the repository's accepted GitFlow branches and validate allowed source/target relationships.
2. Give the workflow least-privilege read access, stable unique job names, concurrency cancellation, and a fast policy-validation job.
3. Make every configure, build, and test command fail closed on Windows and require the configured generator/toolchain before configuration.
4. Validate produced CTest JUnit evidence and fail when it is missing, malformed, contains no tests, or reports failures/errors.
5. Install the Ubuntu build prerequisites already proven missing in GitHub Actions and keep the Ubuntu container definition consistent.
6. Make built-in metadata descriptor initialization explicit enough for strict AppleClang warnings without changing descriptor values or public contracts.
7. Add focused portable tests for branch policy and JUnit validation, then run the relevant Windows and available Ubuntu verification.
8. Record exact local and pull-request evidence and prepare the post-merge branch-protection recommendation.

## Non-goals

- Enabling or changing GitHub branch protection before the new checks are trustworthy and merged.
- Changing the GitFlow branch model, merge permissions, release process, or default branch.
- Lowering warnings, test counts, performance thresholds, repetitions, timeouts, or platform coverage.
- Repairing the known macOS POC B frame-throughput acceptance failure if it is reached after compilation; that remains `feature/macos-frame-throughput`.
- Completing POC J, current Ubuntu product acceptance, packaging, publication readiness, or any Slice 2-4 feature.
- Changing public ABI, managed contracts, durable schemas or formats, runtime ownership, adapter support, or architecture revision DPE-ARCH-0014.

## Existing architecture, ownership, and boundaries

- `.github/workflows/slice1.yml` owns GitHub-hosted cross-platform build/test orchestration and evidence upload.
- CMake presets and existing CTest aliases own configurations, thresholds, test selection, and timeouts. CI consumes these contracts and must not redefine or weaken them.
- `scripts/ci/Dockerfile.ubuntu24` and `scripts/ci/Test-UbuntuContainer.ps1` own the reproducible local Ubuntu CI path.
- `src/native/Metadata` owns descriptor defaults. The portability repair must retain identical runtime values and serialization behavior.
- The GitFlow handbook and verification checklist own durable contributor guidance. GitHub repository settings remain an external maintainer-controlled boundary.

## Contract, format, platform, and support impact

| Area | Impact |
| --- | --- |
| C/C++ ABI and C ABI | None. No exported signature, ownership, or layout contract is intentionally changed. |
| Managed contracts/protocols | None. |
| Durable schemas/formats | None. JUnit is consumed as CI evidence only. |
| Process topology | GitHub Actions gains a fast policy job; product/editor/worker topology is unchanged. |
| GitFlow policy | Existing documented branch relationships become machine-checked in pull requests and workflow dispatch tests. |
| Windows | Native-command failures become job failures; Visual Studio's bundled Ninja path is resolved and verified. |
| Ubuntu | Missing autotools prerequisites are added to hosted and container paths. |
| macOS | Existing descriptor values receive warning-clean explicit initialization; no support claim changes. |
| KNI | Remains experimental and is reported separately. |

## Security and data safety

- Set workflow permissions to `contents: read`; no write token, deployment, secret publication, or third-party upload is introduced.
- Pin new actions by full commit SHA if any are required. Prefer repository scripts and the already-pinned actions.
- Parse workflow event data and JUnit as untrusted inputs without shell interpolation or dynamic execution.
- CI must preserve logs and JUnit artifacts even on failure where possible, while never converting a failed command or empty suite into success.
- No project/authoring data mutation path changes.

## Implementation increments

### Increment 1: CI contracts and tests

- Add a portable standard-library Python validator for accepted GitFlow event relationships.
- Add a portable JUnit evidence validator that requires a positive test count and zero failures/errors.
- Add unit tests covering allowed and rejected branches, missing/malformed XML, empty suites, nested suites, and failed/error test evidence.
- Update the workflow triggers and add a fast, uniquely named policy-validation job.
- Verify the Python tests and workflow syntax before committing.

### Increment 2: Fail-closed cross-platform execution

- Make Windows external commands fail immediately and report the command that failed.
- Resolve and assert Ninja, CMake, compilers, .NET, and Qt inputs before configuration.
- Validate every produced JUnit file after CTest.
- Add the proven Ubuntu autotools packages to hosted CI and the Ubuntu container.
- Verify policy tests, a Windows focused/full build as applicable, and the Ubuntu container build/test path when locally available.

### Increment 3: AppleClang metadata portability

- Replace partial positional aggregate construction of built-in component descriptors with an explicit construction seam or complete member initialization.
- Add or extend native tests that confirm the built-in registry retains all expected components and key descriptor defaults.
- Run focused metadata/serialization tests and strict Windows Release plus sanitizer coverage.

### Increment 4: Evidence and handoff

- Run the accepted final feature matrix without weakening thresholds.
- Update this plan, the master plan, the completed editor-library plan, `AGENTS.md`, and affected development handbooks with exact results and limitations.
- Verify every changed mirrored Markdown pair by SHA-256.
- Review the aggregate diff and commit sequence, push the branch, and open a draft pull request into `develop`.
- Leave the pull request unmerged and identify exact trustworthy check names recommended for post-merge protection.

## Test strategy

### Focused tests

- Python standard-library unit tests for GitFlow relationship and JUnit evidence validation.
- Workflow static validation (YAML parse plus `actionlint` when available).
- Existing `poc_c.metadata_serialization` and `s1.native_core` tests after the metadata change.
- A deliberate negative JUnit validation run proving an empty suite cannot pass.

### Build and platform verification

- Windows Release configure/build and all applicable CTest tests with JUnit validation.
- Windows MSVC AddressSanitizer configure/build and all applicable CTest tests with JUnit validation.
- Ubuntu 24.04 container Release/Clang and AddressSanitizer/Clang paths when Docker execution is available.
- GitHub Actions Windows, Ubuntu, and macOS matrix results from the draft pull request, reported separately by platform and configuration.
- Existing POC B and other thresholds remain unchanged. A real product test failure is recorded as a blocker, never recast as CI success.

## Documentation strategy

Affected durable documentation:

- this mirrored feature plan;
- `Dragon Pixel Engine Slices 1-4 Version 1.0 Completion Plan.md`;
- `Dragon Pixel Engine Editor Library Modularization Plan.md`, to record PR #3's confirmed merge;
- mirrored `Development/GitFlow Handbook.md` and `Development/Verification Checklist.md` if their CI enforcement instructions change;
- repository `AGENTS.md` current-work boundary;
- repository `README.md` only if its contributor CI instructions are stale.

The Design Document, LLM Prompt Source, ADR status, schemas, and architecture revision remain unchanged unless implementation discovers an architecture or acceptance change. Any such discovery stops implementation for the required mirrored architecture update.

## Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| A required job is skipped by branch/path filtering and later treated as successful. | Use broad GitFlow event coverage, unique stable job names, and no path filters on candidate required checks. |
| Windows shell behavior hides an external command failure. | Wrap native commands with explicit exit-code checks and test the wrapper behavior. |
| JUnit shape varies between `testsuite` and `testsuites`. | Support both standard roots, aggregate nested suites, and cover both in unit tests. |
| A portability repair changes descriptor semantics. | Use explicit named assignment/default construction and assert registry defaults in existing native tests. |
| Hosted runners drift. | Assert tool versions and prerequisites, keep dependency pins, and preserve a local Ubuntu container path. |
| Branch protection is configured against unstable or absent check names. | Defer settings mutation until post-merge and record exact names observed on the ready-for-review PR. |
| Full matrices expose unrelated existing product failures. | Preserve and report them separately; do not broaden this feature beyond CI truthfulness and the two proven compile/provisioning blockers. |

## Definition of done

This implementation is ready for review when:

- the branch is based on current `develop` and contains only this feature;
- accepted and rejected GitFlow relationships have automated tests;
- Windows cannot pass after a failed configure, build, CTest invocation, missing tool, or empty JUnit result;
- hosted and container Ubuntu definitions contain the proven required build tools;
- strict AppleClang no longer fails on omitted metadata descriptor fields;
- focused and required final verification is recorded exactly per platform/configuration, including genuine failures and skips;
- no tests, thresholds, timeouts, platforms, or support claims are weakened;
- all affected documentation is current and every changed mirror pair is byte-identical;
- aggregate diff and commits are reviewed, the branch is pushed, and a draft PR into `develop` is open and unmerged;
- post-merge branch-protection recommendations name only checks that actually ran truthfully.

The feature is fully done only after human review, merge into `develop`, post-merge validation, and a separately authorized repository-setting update where appropriate.

## Work log

### 2026-07-27

- Verified all four required documentation pairs exist and match by SHA-256.
- Read `AGENTS.md`, Design DPE-ARCH-0014, the complete current Prompt/Result, the active master plan, the completed editor-library plan, relevant ADR-0007, and the governing development handbooks.
- Fetched `origin`, confirmed pull request #3 merged, fast-forwarded local `develop` to `b3281fa`, and verified a clean worktree.
- Audited the final PR #3 workflow logs and repository settings. Confirmed false-green Windows execution, missing Ubuntu autotools, strict AppleClang descriptor warnings, historical branch filters, and absent `develop` protection.
- Created `feature/ci-gitflow-integration` from current `develop`.
- Identified the affected tests and documentation and created this plan before source/workflow changes.
- Completed CI-contract increment in `afa0e02`: added GitFlow and JUnit validators with 10 passing Python unit tests, updated accepted workflow triggers, added least-privilege permissions/concurrency, and made the policy job a prerequisite for each platform matrix.
- Completed fail-closed platform increment in `326a1db`: added a tested PowerShell native-command wrapper, explicit Windows tool/Ninja/vcpkg checks, minimum-56 JUnit validation on every matrix, missing-evidence failures, Ubuntu autotools prerequisites, and matching container validation.
- Verified actionlint 1.7.12 against the workflow using its SHA-256-verified Windows release archive (`6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9`). The PowerShell success/failure propagation test passed, all 10 Python tests passed, and a real focused `poc_c.metadata_serialization` CTest JUnit result passed validation. Docker container execution is currently unavailable because the local Docker Desktop Linux daemon is not running; this is inconclusive, not a pass.
- Completed metadata portability increment in `0573a25`: built-in component descriptors now use explicit named assignments, and `s1.native_core` asserts the 16-record registry plus preserved Transform and default descriptor values.
- An initial focused Release build without a Visual Studio developer environment failed because MSVC standard-library include paths were absent. After importing the Visual Studio 2022 developer shell, focused `s1.native_core` passed 1 of 1 in Release (1.60 seconds) and MSVC AddressSanitizer (2.06 seconds); both generated JUnit files passed the validator. The initial environmental failure is not product evidence.
- Completed the local final Windows matrix after supported configure/builds. Strict Release passed 56 of 56 in 410 seconds and MSVC AddressSanitizer passed 56 of 56 in 551 seconds. Both CTest JUnit files contain 56 cases, zero failures, zero errors, zero skips, and zero disabled tests and pass the minimum-56 validator. No test, timeout, warning, or threshold changed.
- Updated the completed editor-library plan with PR #3's confirmed merge, activated this feature in the master/current-work records, and added the executable CI requirements to the GitFlow handbook and verification checklist. Hosted pull-request evidence remains the next gate.
- Reviewed the ordered seven-commit aggregate diff against `origin/develop`: 16 paths contain only the planned workflow, CI scripts/tests, Ubuntu prerequisites, metadata initialization/test, and current documentation. The first aggregate `git diff --check` identified Markdown hard-break whitespace in this plan; `fbca4cd` replaced it with blockquote metadata. The rerun passes, as do actionlint 1.7.12, 10 Python tests, the PowerShell failure-propagation test, and all 60 mirrored-document checks at DPE-ARCH-0014. The worktree is clean before publication.
- Pushed the reviewed branch and opened draft PR [#4](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/4) into `develop`. The first policy job passed in 4 seconds. The first Windows job then failed closed in the new PowerShell test: although the expected exit-19 exception was caught and the test printed success, PowerShell retained `$LASTEXITCODE=19` for the step. Reproduced that exact exit locally, added a successful recovery command to prove the wrapper resets the native exit state, verified a final exit code of zero, committed `88ffb04`, and pushed the repair. Hosted matrices are rerunning; the earlier failed run is not passing evidence.
- Observed duplicate push and pull-request matrices for the same branch. Commit `c562276` gives same-repository push/PR events one concurrency identity while including the head repository for fork isolation. The next PR event automatically cancelled its redundant push run; two older superseded runs created under the previous key were cancelled manually.
- Hosted run `30297746866` passed the policy gate and PowerShell probe, then failed closed at all six real build boundaries. Windows exposed the hosted runner's shallow `C:\vcpkg` checkout, which could not resolve the pinned `40f3c709...` baseline. Ubuntu advanced past the original autotools failure and explicitly required `libltdl-dev`. AppleClang advanced past production Metadata and found two partial component aggregates in `command_validation_tests.cpp`. No job produced JUnit, so missing-evidence upload also failed as intended; none is counted as passing evidence.
- Commits `999696f` and `9f44ef6` clone/bootstrap the exact pinned vcpkg baseline on Windows, add `libltdl-dev` to hosted/container Ubuntu, and replace the two test aggregates with named assignments. actionlint, all 10 Python tests, and the PowerShell exit-state test pass. Focused `s2.command_validation` passes 1 of 1 in Release (0.03 seconds) and MSVC AddressSanitizer (0.05 seconds), and both JUnit files pass validation. Docker remains unavailable locally; hosted rerun is required.

## Handoff notes

Implementation, local verification, branch publication, and draft-PR creation are complete. Hosted matrix evidence and final handoff review remain. Do not merge this branch. Branch protection remains intentionally unmodified until the draft PR demonstrates stable, truthful check names and a maintainer approves the post-merge repository-setting change.
