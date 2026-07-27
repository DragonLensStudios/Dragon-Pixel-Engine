# Feature Plan: Dragon Pixel Engine Editor Library Modularization

> **Status:** In progress
> **Branch:** `feature/editor-library-modularization`
> **Target:** `develop`
> **Owner:** Dragon Pixel Engine maintainers
> **Started:** 2026-07-27
> **Updated:** 2026-07-27
> **Design baseline:** `DPE-ARCH-0014` (unchanged)
> **Repository mirror:** `docs/Plans/Dragon Pixel Engine Editor Library Modularization Plan.md`
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine Editor Library Modularization Plan.md`

## Goal

Extract the editor implementation that is currently compiled independently into `DragonPixelEditor` and `DragonPixelEditorInteractionTests` into one reusable CMake library target. Keep the production executable and the public Qt interaction executable as thin consumers while preserving runtime behavior, dependency direction, resources, warnings, sanitizer instrumentation, and test coverage.

## User Value

Contributors should be able to change editor services and widgets once, compile them once per configuration, and exercise the exact same implementation in the production editor and the full interaction suite. A clear editor-library boundary reduces duplicate build work and makes the remaining service split easier to review without changing authoring behavior.

## Background

Atomic-publication recovery PR #2 is merged into `develop` at `17e481d700096e491698f597d8590988042b8203`. A fresh fetch confirms local `develop` and `origin/develop` are identical at that commit. The atomic feature plan and the current-work paragraph in `AGENTS.md` still describe the already merged review state; this feature will correct that status without rewriting the preserved implementation evidence.

The recommended post-atomic feature sequence names `feature/editor-library-modularization` first. The current `src/editor/CMakeLists.txt` compiles the same 27 editor implementation/resource files into both the production executable and the full Qt interaction-test executable, repeats their native/Qt/SDL link dependencies, repeats runtime compile definitions, and repeats managed/native adapter dependencies. The smallest complete feature unit is a build-graph refactor that centralizes those shared inputs without changing C++ behavior.

## Scope

- Introduce one reusable CMake library target for the shared editor implementation and Qt resources currently duplicated by the production and interaction-test executables.
- Give the reusable target a stable Dragon Pixel alias consistent with existing target naming.
- Centralize the editor implementation source list, native module dependencies, Qt/SDL dependencies, runtime compile definitions, adapter/C ABI build ordering, automatic Qt processing, and native warning/sanitizer configuration at the library boundary.
- Keep `main.cpp` owned only by `DragonPixelEditor` and `EditorInteractionTests.cpp` owned only by `DragonPixelEditorInteractionTests`.
- Keep test-only Qt dependencies and test registration on the interaction-test executable.
- Preserve executable names, test names, runtime environment, timeouts, packaging script targets, installed resource lookup, and current product thresholds.
- Correct the merged atomic-publication disposition and activate this plan in the minimum governing/master documentation.
- Record exact build, test, sanitizer, packaging, mirror, and aggregate-diff evidence before handoff.

## Non-Goals

- Do not split `EditorWindow` behavior into new services in this feature.
- Do not change editor UI, commands, authoring state, worker lifecycle, rendering, input, project lifecycle, assets, prefabs, or component behavior.
- Do not change a public C ABI, managed contract, worker protocol, durable format, schema, architecture revision, ADR status, POC status, slice status, platform threshold, or KNI support claim.
- Do not begin CI/GitFlow enforcement, public-repository readiness, developer-preview packaging breadth, Ubuntu matrix refresh, macOS frame-throughput repair, or POC J latency work.
- Do not rename the production executable, interaction executable, registered tests, bundle directory, or package-facing files.
- Do not merge into `develop`.

## Existing Architecture

- `DPE.Editor` is the Qt application shell; `DPE.Editor.Services` owns editor behavior and may depend only on public core contracts and Qt adapters.
- Portable native modules remain independent of Qt, SDL, managed runtimes, and framework adapters.
- `EditorWindow` is still a large composition/orchestration surface, and ADR-0008 explicitly records the complete Project/Scene/Selection/Command/Metadata/Asset/RuntimeSession/Diagnostics/Workspace service split as unfinished.
- The editor and full Qt interaction tests currently compile an identical editor source set separately. Smaller service tests intentionally compile bounded service sources independently and are outside this first modularization unit.
- Production editor scripts build the existing `DragonPixelEditor` target and expect its executable and runtime resources at unchanged locations.

## Ownership and State Boundaries

- The new editor library owns compiled editor implementation objects and embedded editor resources, not application startup or test entry points.
- `DragonPixelEditor` owns `main.cpp`, `QApplication` startup, command-line processing, self-test entry, and the production executable identity.
- `DragonPixelEditorInteractionTests` owns the Qt Test entry point and test-only Qt dependency.
- Existing editor services retain their current state and mutation ownership; moving compilation into a library does not authorize new dependencies or direct writes.
- Native runtime, framework workers, project code, external tools, and authoring documents remain outside the library's ownership.

## Contract, ABI, Protocol, or Format Impact

No public engine contract, installed SDK library, C ABI, managed contract, worker protocol, document format, schema, or runtime capability changes. The reusable editor target is an internal build boundary and is not a new supported plugin or binary compatibility surface. `DPE-ARCH-0014` and affected ADR statuses remain unchanged.

## Security and Data-Safety Considerations

- This is a compile/link refactor; it must not change authoritative mutation paths or project bytes.
- Tests continue to use disposable projects and must preserve the tracked sample.
- Runtime path definitions and adapter/native library paths must remain exactly available to the shared implementation.
- The packaging smoke test must verify that resource and worker discovery still resolve from the production-style bundle.
- Existing sanitizer and warning settings must apply to the shared implementation and both entry points.

## Implementation Increments

### Increment 1: Plan and target-boundary baseline

- Record the merged atomic prerequisite and the exact duplicated source/dependency boundary.
- Create this mirrored plan before source modification.
- Capture a clean focused Release configure/build/test baseline for the affected targets when practical.

### Increment 2: Reusable editor library

- Add the shared editor library and stable alias.
- Move only the duplicated editor implementation/resources, common dependencies, definitions, and build ordering to the library.
- Reduce each executable to its unique entry point plus the shared library.
- Configure and build both consumers in strict Release, then run focused behavior tests.
- Commit the coherent build-graph refactor after focused Release verification.

### Increment 3: Sanitizer, package, and handoff evidence

- Configure/build the affected targets under MSVC AddressSanitizer and run the focused sanitizer aliases.
- Rebuild and validate the production-style editor bundle without broadening POC R claims.
- Update mirrored status/evidence and the active master tracker.
- Run mirror, whitespace, target/diff, and aggregate branch review checks.
- Commit documentation/evidence, push the branch, open a draft PR into `develop`, and stop for human review.

## Test Strategy

### Build-graph checks

- Configure the existing Windows Release and MSVC AddressSanitizer presets without changing preset policy.
- Build `DragonPixelEditorLibrary`, `DragonPixelEditor`, and `DragonPixelEditorInteractionTests` explicitly.
- Inspect verbose or generated build metadata as needed to confirm shared editor sources compile once per configuration and are not also direct executable sources.
- Ensure all targets retain strict warning and sanitizer configuration.

### Focused behavior checks

- `s1.editor_monogame`
- `s1.editor_kni`
- `s1.editor_crash_recovery`
- `s2.editor_interactions`
- `poc_h.qt_interactions`

The two full Qt aliases intentionally exercise the same interaction executable; both remain registered and must pass. POC J is not rerun as a feature gate because the modularization changes no input behavior and the accepted aggregate latency work remains a later focused feature.

### Packaging checks

- Run the existing production-editor build/deploy path against the unchanged `DragonPixelEditor` target.
- Require the packaged MonoGame self-test and bundle manifest/layout/hash verification to pass.
- Do not characterize the developer bundle as a signed, clean-machine, relocatable POC R package.

### Repository checks

- `git diff --check`
- `scripts/docs/Test-DocumentationMirrors.ps1`
- Verify every changed mirrored Markdown pair has the same SHA-256, UTF-8 without BOM, and LF bytes.
- Review `origin/develop...HEAD`, the ordered commit sequence, generated files, and changed path scope.

## Platform Strategy

The implementation and first verification run on Windows 11 x64 because the current complete baseline and build outputs are present there. The target structure is portable CMake and must not add a Windows-only source or link assumption. Focused Windows Release/ASan evidence does not close the current Ubuntu or macOS matrices, POC H, an ADR, or a slice. Current POSIX execution remains a later platform feature unless this refactor exposes a direct build portability defect.

## Documentation Requirements

- Maintain this plan byte-identically in both documentation roots.
- Update the active master plan and `AGENTS.md` current-work state to record that atomic PR #2 merged and this feature is active.
- Update the atomic plan only to record its post-review merge disposition; preserve its original evidence and handoff history.
- Update code-adjacent build documentation only if the developer-facing target or command changes; executable/package commands are expected to remain unchanged.
- Do not revise Design, Prompt/Result, or ADR content unless implementation discovery changes architecture or a public/durable contract.
- Before the final response, record exact commands, results, durations, limitations, and the draft PR handoff here.

## Risks and Mitigations

- **Risk:** Qt resources disappear when moved behind a static archive. **Mitigation:** Use a library form whose object/resource initialization is linked into every consumer, then prove icons/resources through the full interaction suite and production bundle.
- **Risk:** Runtime compile definitions no longer reach the source that consumes them. **Mitigation:** Classify definitions by shared implementation versus entry point and compile both consumers from a clean configure.
- **Risk:** Link dependencies become accidentally public or disappear. **Mitigation:** Preserve the current dependency set at the narrowest correct target boundary and inspect generated link/build metadata.
- **Risk:** Sanitizer instrumentation covers entry points but not the shared implementation. **Mitigation:** Apply the repository native-target configuration directly to the library and run the affected ASan tests.
- **Risk:** A broad CMake cleanup absorbs unrelated service tests. **Mitigation:** Limit this unit to sources duplicated between the production editor and full interaction executable; leave bounded service-test targets unchanged.
- **Risk:** Build success hides behavior/resource regressions. **Mitigation:** Run production self-tests, crash recovery, both complete Qt interaction aliases, and bundle validation.

## Definition of Done

- [x] Atomic-publication PR #2 merge verified on current fetched `develop`.
- [x] Four required document mirrors and `DPE-ARCH-0014` revision checks passed.
- [x] Design and Prompt/Result read completely.
- [x] Active master plan, atomic plan, workflow handbooks, and affected ADRs read.
- [x] Focused feature branch created from current `origin/develop`.
- [x] Mirrored feature plan created before source modification.
- [ ] Shared editor implementation/resources compile through one reusable library target.
- [ ] Production and interaction executables contain only their unique entry points plus the shared target.
- [ ] Existing executable names, test registrations, runtime behavior, and packaging path remain unchanged.
- [ ] Focused strict Release build/tests pass.
- [ ] Focused MSVC AddressSanitizer build/tests pass.
- [ ] Production-style bundle validation and packaged self-test pass.
- [ ] Exact evidence and remaining platform/POC/ADR limitations are recorded.
- [ ] Mirrored documentation and Git whitespace checks pass.
- [ ] Aggregate diff and commit sequence reviewed.
- [ ] Branch pushed.
- [ ] Draft PR opened into `develop`.
- [ ] PR left unmerged for human review.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Prerequisite verified | Fetched `origin`; local `develop`, `origin/develop`, and their merge-base all resolve to `17e481d700096e491698f597d8590988042b8203`, the merge commit for atomic-publication PR #2. The starting worktree was clean. |
| 2026-07-27 | Required documents verified | The four required Design, Prompt/Result, First Structure, and Notes pairs exist and match by SHA-256. Design and Prompt/Result both declare `DPE-ARCH-0014`, matching root governance, and were read completely with the active plans/workflow and ADR-0007/0008. |
| 2026-07-27 | Feature selected | The recommended post-atomic sequence names editor-library modularization first. Discovery found 27 shared implementation/resource files, common native/Qt/SDL link dependencies, runtime definitions, and adapter/C ABI dependencies duplicated between the production editor and full Qt interaction executable. The bounded service-test targets remain outside this increment. |
| 2026-07-27 | Branch prepared | Created `feature/editor-library-modularization` directly from fetched `origin/develop` before source modification. |
| 2026-07-27 | Plan created | Created this byte-identical mirrored plan before changing CMake or C++ source. The intended change is an internal build boundary at unchanged `DPE-ARCH-0014`; no architecture or ADR revision is currently required. |

## Handoff Notes

Implementation is in progress. The next action is a focused baseline build of the current two editor consumers, followed by the smallest CMake-only extraction into a reusable editor library. Do not begin CI, public-repository, packaging-breadth, platform-matrix, or POC J work on this branch.
