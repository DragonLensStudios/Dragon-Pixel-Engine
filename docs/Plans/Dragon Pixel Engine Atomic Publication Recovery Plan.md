# Feature Plan: Dragon Pixel Engine Atomic Publication Recovery

> **Status:** Complete — reviewed and merged through PR #2
> **Branch:** `feature/atomic-publication-recovery`
> **Target:** `develop`
> **Owner:** Dragon Pixel Engine maintainers
> **Started:** 2026-07-27
> **Updated:** 2026-07-27
> **Design baseline:** `DPE-ARCH-0014` (unchanged unless discovery proves an architecture change is required)
> **Repository mirror:** `docs/Plans/Dragon Pixel Engine Atomic Publication Recovery Plan.md`
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine Atomic Publication Recovery Plan.md`

## Goal

Reproduce, diagnose, and repair the intermittent atomic-publication recovery defect that prevents a new authoritative complete Windows matrix claim. The repair must preserve transactional publication, protect the last known valid destination, recover deterministically across interruption boundaries, and provide durable regression evidence.

## User Value

Project documents, generated outputs, developer bundles, manifests, and other publication destinations must never be left partially replaced or lose their last valid version when a write, replacement, cleanup, process, or machine operation is interrupted. Contributors also need a deterministic test instead of an intermittent matrix blocker.

## Background

The active DPE-ARCH-0014 evidence records an intermittent atomic-publication recovery defect as the highest-priority implementation blocker before a new complete Windows Release and MSVC AddressSanitizer matrix can be claimed. Earlier work added SHA-256-bound journals, staging, backups, rollback, and startup recovery for several publication paths, while a production-bundle refresh exposed unsafe partial replacement when another process held files. Discovery must identify the exact failing owner and state transition rather than assuming that every atomic-write implementation shares one defect.

This feature is the one-time First Feature selected by the installed Dragon Pixel Engine Codex master and atomic-recovery prompts. It begins only after governance PR #1 merged into current `develop`.

## Scope

- Inventory atomic write, replacement, backup, staging, journal, cleanup, rollback, and startup-recovery implementations relevant to the reported failure.
- Identify existing plans, tests, fault-injection seams, logs, and evidence that describe or reproduce the intermittent defect.
- Produce a deterministic reproduction or the strongest bounded reproduction possible before changing behavior.
- Add or refine fault injection for:
  - failure before temporary/staged write;
  - failure during write or flush;
  - failure before destination replacement;
  - failure after destination replacement but before cleanup;
  - recovery with a valid prior destination;
  - recovery with a valid staged candidate;
  - interrupted or failed cleanup.
- Repair the smallest authoritative owner/state machine responsible for the defect.
- Preserve the last known valid destination and make repeated recovery idempotent.
- Add regression coverage for success, rollback, restart recovery, malformed/stale journal state, and cleanup boundaries appropriate to the discovered implementation.
- Run focused strict Release verification and focused MSVC AddressSanitizer verification when native code is involved.
- Run the complete Windows strict Release and MSVC AddressSanitizer matrices after focused verification passes, unless an unrelated pre-existing failure is directly evidenced and recorded without weakening the matrix.
- Update this mirrored plan with exact commands, results, durations, remaining blockers, and review handoff.
- Review the complete branch diff, push, and open a draft PR into `develop`; do not merge it.

## Non-Goals

- Do not implement editor modularization, CI/GitFlow enforcement, public-repository readiness, packaging breadth, macOS throughput, Ubuntu matrix repair, or POC J latency in this feature.
- Do not redesign every atomic write path if the defect belongs to one bounded owner.
- Do not weaken atomicity, containment, validation, recovery, sanitizer, timeout, or platform acceptance requirements.
- Do not claim a complete Windows matrix, POC, ADR, slice, release, or KNI status from focused or partial evidence.
- Do not terminate user-owned tools or discard writable sample/project content merely to make publication succeed.
- Do not change architecture, a public contract, or a durable format without first following the synchronized living-document and ADR revision process.

## Existing Architecture

- Authoritative authoring state remains editor-owned and all durable mutations use validated commands or operation transactions.
- Candidate content is written to contained staging, validated, flushed where required, and only then committed to its destination.
- Recovery journals and backups bind expected content with hashes and must distinguish last-known-valid, staged-candidate, committed, rollback, and cleanup state without relying on timestamps alone.
- Release/developer-bundle publication must preserve writable user content by default and refuse unsafe replacement while files are in use.
- Runtime workers, generators, importers, packagers, and project code do not become alternate authorities for durable state.

Discovery will map the concrete implementation to these accepted boundaries before any source modification.

## Ownership and State Boundaries

- The existing publication service, transaction helper, or script identified by discovery owns staging and commit orchestration.
- The destination's last validated version remains authoritative until a candidate has passed validation and the commit boundary completes.
- Temporary files, staging directories, backups, and journals are operation-owned recovery state; cleanup cannot retroactively invalidate a committed destination.
- Recovery must be idempotent and must never choose a candidate solely because it is newer by timestamp.
- Project files, source files, linked external content, and writable sample content remain user-owned and may not be deleted or replaced outside their accepted service boundaries.
- Tests may inject failures only through explicit seams and disposable contained fixtures.
- Transaction saves and startup recovery under the same recovery root are serialized for cooperating callers by a bounded lease on the existing `Transactions` directory. No new durable lock artifact or process topology is introduced.
- Hash/state revalidation rejects conflicts visible before each pathname mutation. This increment does not close ADR-0006's handle/inode-pinning work: noncooperating writers in the final validation-to-system-call window and replacement of the leased Unix recovery-directory pathname remain explicit hardening limits.

## Discovery and Root Cause

The matrix blocker belongs to `dragonpixel::serialization::save_utf8_transaction` in the portable native Serialization module and its registered `s1.native_core` test. It is not the production-bundle refresh or the Qt asset-service staging implementation.

Preserved local evidence records the original intermittent Windows sequence: a nominal transaction failed while changing its journal from `prepared` to `committed` with Win32 error 1175; later stress runs exposed `ERROR_ACCESS_DENIED` during journal publication and a persistent `ERROR_SHARING_VIOLATION` followed by an immediate rollback read that could not open `preimage-000000.bin`. The current implementation retries replacement APIs but collapses a temporarily unreadable topology probe into an unsafe topology and does not retry recovery reads. A current bounded Release stress run completed 320 consecutive repetitions, then its next `s1.native_core` process stopped making progress until the harness was terminated; this is supporting intermittent evidence, not a passing stress result.

The deterministic regression exposes the durable-state defect behind those failures. `save_utf8_transaction` creates a new transaction without first resolving an older prepared journal. A successor save can therefore commit new bytes while the earlier recovery record remains. On the next recovery pass, those valid newer bytes match neither the earlier pre-image nor post-image, so recovery refuses to proceed and the stale journal blocks project opening. If a later save happens to reproduce the older post-image, that stale journal can instead roll it back. The required invariant is: **no new transaction may stage or publish until all prior transaction records under the same recovery root have been recovered or conservatively rejected without changing the destination.**

## Contract, ABI, Protocol, or Format Impact

No architecture, public C ABI, managed contract, worker protocol, project format, schema, support claim, or acceptance threshold change is expected. The journal remains format version 1; the repair changes only internal publication ordering, lease/retry handling, conservative conflict checks, and recovery interpretation of existing temporary journal artifacts. If discovery requires a public or architectural change, implementation pauses for the mandated Design/Prompt/Result/ADR revision first.

## Security and Data-Safety Considerations

- Resolve and validate every destructive/replacement target before mutation; never operate recursively on a broad or unresolved path.
- Preserve containment across links/reparse points, path aliases, case folding, and concurrent path changes.
- Protect last-known-valid bytes when staging, replacement, validation, or cleanup fails.
- Bind recovery choices to validated hashes and safe contained paths rather than filename/timestamp assumptions, and revalidate immediately before each supported mutation. Handle-bound identity across the final pathname check/mutation window remains tracked by ADR-0006 rather than being claimed complete here.
- Make rollback and repeated startup recovery safe after partial cleanup.
- Keep secrets, credentials, private prompts, and arbitrary user content out of logs and recovery manifests.
- Do not kill external user processes to release files.

## Implementation Increments

### Increment 1: Reproduction and root cause

- Locate the exact publication owner, current state transitions, tests, and historical evidence.
- Build a disposable deterministic reproducer with explicit failure points.
- Commit the failing regression test or bounded diagnostic evidence before the repair when practical.
- Record the root cause and the last-known-valid invariant in this plan.

### Increment 2: Transactional repair

- Make the smallest change that corrects state ordering, validation, replacement, rollback, or cleanup ownership.
- Ensure retries and startup recovery are idempotent.
- Keep prior journal/fixture compatibility or provide an explicit deterministic migration/recovery path.
- Run focused verification after each coherent change and commit separately from unrelated refactoring.

### Increment 3: Regression matrix and handoff

- Complete interruption-boundary, malformed-state, concurrency/locked-file, and cleanup regression cases appropriate to the owner.
- Run focused Release and sanitizer verification.
- Run the required complete Windows Release and sanitizer matrices.
- Update exact durable evidence, review the aggregate diff, push, and open the draft PR.

## Test Strategy

- Prefer the existing native, script, service, or end-to-end test harness that owns the failing publication path.
- Add deterministic failure injection at the operation boundary rather than using timing-only sleeps.
- Assert destination bytes/hashes, staged and backup disposition, journal state, diagnostics, cleanup, idempotent retry, and absence of writes outside the fixture root.
- Include a valid-prior-destination fixture and ensure every failed candidate leaves it usable.
- Include a valid-staged-candidate recovery fixture only where the accepted contract permits completing the candidate after restart.
- Include corrupt, stale, mismatched, and partially cleaned journal fixtures.
- Run `git diff --check` and the recursive documentation mirror validator with final evidence.

Exact focused aliases and matrix commands will be recorded after discovery maps the owner to its test registrations.

## Platform Strategy

The defect currently blocks a new authoritative Windows matrix, so reproduction and the first repair evidence run on Windows 11 x64. Portable native logic receives platform-neutral tests where practical. Focused Windows success does not close Ubuntu/macOS or cross-platform product gates; existing failed Ubuntu/macOS CI evidence remains visible and outside this feature unless the same defect directly explains it.

## Documentation Requirements

- Maintain this plan in both documentation roots with byte-identical UTF-8/LF bytes.
- Record root cause, invariants, changed ownership/state ordering, every command/result, injected failure coverage, durations, hashes where relevant, and remaining limitations.
- Update code-adjacent documentation if a recovery state or developer operation changes.
- Update Design/Prompt/Result and affected ADRs only if discovery changes architecture, a public/durable contract, a risk gate, or acceptance evidence.
- Before final response, record the draft PR and exact handoff here and verify all documentation mirrors.

## Risks and Mitigations

- **Risk:** “Atomic publication” names several implementations and the wrong one is repaired. **Mitigation:** Trace the recorded intermittent failure to its concrete owner and reproduce it before behavior changes.
- **Risk:** A happy-path fix loses the prior destination after a later cleanup failure. **Mitigation:** Assert last-known-valid hashes at every injected boundary and separate commit from cleanup state.
- **Risk:** Recovery completes a corrupt or stale staged candidate. **Mitigation:** Require identity, version, containment, manifest, and content-hash validation before promotion.
- **Risk:** Tests remain timing-dependent and intermittent. **Mitigation:** Add explicit deterministic fault-injection/state seams and avoid sleep-based orchestration.
- **Risk:** A broad replacement deletes writable user content or in-use files. **Mitigation:** Use contained disposable fixtures, preserve writable content by default, and refuse unsafe replacement without killing external processes.
- **Risk:** Full matrices expose unrelated failures. **Mitigation:** Preserve the complete result, diagnose provenance, and record unrelated blockers without skipping, relabeling, or weakening tests.

## Verification Evidence

The final source commit is `3e63ef1`. Focused builds used `cmake --build --preset windows-release|windows-asan --target dpe_s1_native_tests`; focused tests used `ctest --preset windows-release|windows-asan -R '^s1\.native_core$' --output-on-failure`. The latest focused results pass **1/1 in 1.53 seconds Release** and **1/1 in 1.97 seconds ASan** (1.54 and 1.98 seconds total CTest time).

Bounded final stability used the same focused aliases with `--repeat until-fail:100` for Release and `--repeat until-fail:50` for ASan. Release passes **100/100 in 157.04 seconds**; ASan passes **50/50 in 106.54 seconds**. These replace, but do not erase, the preserved pre-repair 320-pass-then-hang observation.

The complete final Windows commands configured and built every target with `Build-Windows.ps1 -SkipTests` and `Build-Windows.ps1 -AddressSanitizer -SkipTests`, then ran unfiltered `ctest --preset windows-release|windows-asan --output-on-failure --output-junit ...`. Results:

| Gate | Build | Complete CTest | Notable aggregate aliases |
| --- | --- | --- | --- |
| Windows strict Release | Passed in **105.81 s**; managed builds report zero warnings/errors | **56/56 passed in 423.22 s** | `s2.editor_interactions` 126.50 s; `poc_h.qt_interactions` 125.14 s |
| Windows MSVC AddressSanitizer | Passed in **64.31 s**; managed builds report zero warnings/errors | **56/56 passed in 560.43 s** | `s2.editor_interactions` 181.13 s; `poc_h.qt_interactions` 179.14 s |

The ignored local evidence bundle is hash-bound below. Both JUnit files report 56 tests, zero failures, and zero skipped tests; both copied `LastTest.log` files contain 56 `Test Passed` records.

| Evidence file | SHA-256 |
| --- | --- |
| `atomic-publication-feature-final-release-repeat-100.log` | `BB8DD149F22875DF8F8FE76ABC5F21D5C0D9ACC33918FAE711949A3C7065C29F` |
| `atomic-publication-feature-final-asan-repeat-50.log` | `178963075E42A909D84DE188FB5A192A88BE11AA67824BBCA8FFF61A7A1DBD6A` |
| `atomic-publication-windows-release-build.log` | `FE75D0DAAF29FB06A970CB5ED7A8D9D7C8EAAC610963A5C0894063F691932437` |
| `atomic-publication-windows-release.xml` | `8D28BB7D0B0D5FE05247D1C352B95AF86F2391EC5389FA7922DEFADD90D0683A` |
| `atomic-publication-windows-release-LastTest.log` | `9A6969DF9C0B79DF628597ED938F2D25FD8B285308E86714DDBBD1CFA01FF278` |
| `atomic-publication-windows-release-console.log` | `DBC8C52D3DDFE648D1980FDA35A87EE7D9734D20D52A9A0E9BA4E7306C90988B` |
| `atomic-publication-windows-asan-build.log` | `E5729366DE17057E5223800F898824E4724F6208A9AC91DC7B70447B60A03CBF` |
| `atomic-publication-windows-asan.xml` | `6C5D1EDF13C522B7982038B5F81A19EFA6E204633532B2A6882E73BAEF7F3A8A` |
| `atomic-publication-windows-asan-LastTest.log` | `AAB2BAEA0A5A23612FF9AD4A7F76032F30FEE0359D9A3CF72986CDD4F59C3BF4` |
| `atomic-publication-windows-asan-console.log` | `02E7EB2E0CD3BBCD51C5F426867484F1948C8CBCF5B2960420FA5E0693551F35` |

## Definition of Done

- [x] Governance PR merged before feature start.
- [x] Current `develop` fetched and used as branch base.
- [x] Required mirrors and `DPE-ARCH-0014` revision checks passed.
- [x] Mirrored feature plan created before source modification.
- [x] Exact failing publication owner identified.
- [x] Deterministic reproduction or strongest bounded reproduction recorded.
- [x] Root cause and last-known-valid invariant documented.
- [x] Repair implemented in focused commits.
- [x] Regression tests cover relevant interruption and recovery boundaries.
- [x] Focused strict Release verification passes.
- [x] Focused MSVC AddressSanitizer verification passes when applicable.
- [x] Complete Windows strict Release matrix recorded.
- [x] Complete Windows MSVC AddressSanitizer matrix recorded.
- [x] Documentation and exact evidence updated.
- [x] Aggregate diff and commit sequence reviewed.
- [x] Branch pushed.
- [x] Draft PR opened into `develop`.
- [x] PR left unmerged for human review.
- [x] Human review completed and PR merged into `develop`.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Governance prerequisite satisfied | Governance PR #1 is merged into `develop` at `df092a0715ce841c8c466a11091ebbedf5d5676d`. The merged PR's Windows Release/ASan jobs reported success; its Ubuntu/macOS jobs failed and remain honest unrelated evidence rather than a basis for narrowing this feature. |
| 2026-07-27 | Feature branch prepared | Fetched `origin`, fast-forwarded local `develop` to `df092a0715ce841c8c466a11091ebbedf5d5676d`, confirmed a clean worktree, and created `feature/atomic-publication-recovery` directly from that commit. |
| 2026-07-27 | Required documents verified | The recursive validator passes **57 UTF-8/LF mirrored Markdown pairs** at `DPE-ARCH-0014`. The four required Design, Prompt/Result, First Structure, and Notes pairs match SHA-256; Design and Prompt/Result hashes remain the same complete-read baseline used for the immediately preceding governance work. |
| 2026-07-27 | Plan created | Created this mirrored feature plan before modifying source. Discovery will now identify the exact publication owner, tests, and deterministic failure seam. |
| 2026-07-27 | Historical failure identified | Preserved ignored evidence identifies `s1.native_core` and native multi-document transaction recovery as the blocker. Recorded failures include prepared-to-committed journal publication error 1175, later `ERROR_ACCESS_DENIED`, and persistent sharing-violation exhaustion followed by a locked pre-image rollback read. Other atomic writers were traced and excluded from this specific matrix failure. |
| 2026-07-27 | Bounded baseline stress reproduced | A current strict Release `ctest --preset windows-release -R '^s1\.native_core$' --output-on-failure --repeat until-fail:500` run completed **320** repetitions and then stopped making progress at the start of the next repetition. The run was terminated and is recorded as failed/inconclusive rather than passing. |
| 2026-07-27 | Deterministic regression reproduced | Added a disposable native test that leaves a prepared transaction after its first replacement, attempts a successor transaction under the same recovery root, and then invokes startup recovery. Before the repair, the focused Release test fails **0/1 in 0.21 seconds** with `successor=; recovery=Transaction recovery found unexpected target contents and made no changes.` This proves a newer save bypasses unresolved durable recovery state. |
| 2026-07-27 | Successor ordering repaired | Commit `42cfda2` acquires the recovery context before staging and resolves every prior journal under the same root. A stale or conflicting record now blocks the successor before new target bytes can be published. |
| 2026-07-27 | Durable-state ambiguity hardened | Commit `ef29578` separates Windows API-attempt and topology-probe accounting, retries transient topology/read failures within the bounded budget, and reloads the durable journal after an ambiguously reported committed-marker publication. |
| 2026-07-27 | Cooperating callers serialized | Commit `c406317` holds an exclusive bounded lease on the existing recovery `Transactions` directory across preflight recovery, staging, publication, rollback, and cleanup. Same-root save/recovery contention rejects without mutation; a different root remains independent. The Windows lease uses a non-share-delete directory handle; Unix uses advisory `flock` on the opened directory inode. |
| 2026-07-27 | External-conflict preservation hardened | Commit `99e6534` revalidates expected target/staged hashes, tracks whether a publication API was invoked, rolls back only the safely owned prefix after a conflict, preserves an unexpected current target, revalidates restore/removal/commit boundaries, and uses non-clobbering hard-link publication for a missing POSIX target. Deterministic tests cover existing/missing prepublication conflicts, conflicts detected inside publication, completed APIs reported as failed, unexpected post-attempt ownership, and unavailable post-attempt inspection. |
| 2026-07-27 | Latest focused verification | After `99e6534`, `s1.native_core` passes **1/1** with strict Windows Release in **1.39 seconds** and **1/1** with MSVC AddressSanitizer in **1.68 seconds**. These are focused results only; the required repeated stress and complete Windows matrices remain pending after the journal-candidate increment. |
| 2026-07-27 | Journal candidates retained | Commit `3e63ef1` retains fully flushed prepared/committed journal candidates when canonical publication is ambiguous, gives a canonical journal unconditional precedence, requires exactly one safe name/regular file/size-bounded/hash-valid candidate when the canonical journal is absent, and treats a noncanonical committed candidate only as prepared rollback metadata. Exact canonical bytes, rather than transaction ID or timestamp, decide immediate committed-marker reconciliation. Malformed and duplicate candidates reject without mutation. |
| 2026-07-27 | Ambiguous ownership coverage completed | Added deterministic fixtures for target changes before and inside publication, API-reported failure after publication, a current target becoming unexpected/missing/unavailable after an API attempt, exact backup preservation, same-root lease contention, prepared/committed candidate fallback, canonical precedence, candidate ambiguity, Windows unsafe topology after exactly two probes/one API attempt, cleanup, idempotence, and successor saves. Independent aggregate review found no remaining Phase 1 correctness blocker under the recorded cooperative-caller boundary. |
| 2026-07-27 | Final focused stability passed | Latest focused Release/ASan passes 1/1 in 1.53/1.97 seconds. The final exact commit then passes **100/100 Release repetitions in 157.04 seconds** and **50/50 ASan repetitions in 106.54 seconds** without an early failure. |
| 2026-07-27 | Current complete Windows matrices passed | Full Release build passed in 105.81 seconds and unfiltered CTest passed **56/56 in 423.22 seconds**. Full MSVC ASan build passed in 64.31 seconds and unfiltered CTest passed **56/56 in 560.43 seconds**. The formerly inconclusive aggregate sanitizer interaction aliases pass in 181.13 and 179.14 seconds under their unchanged 480-second caps. No test, platform threshold, timeout, or registration was removed or weakened. |
| 2026-07-27 | Durable evidence and mirrors verified | JUnit reports 56 tests/zero failures/zero skips for both matrices; copied raw logs contain 56 pass records each; every recorded evidence artifact matches its SHA-256. `git diff --check` passes and `Test-DocumentationMirrors.ps1` passes **58 UTF-8/LF Markdown pairs at DPE-ARCH-0014**. |
| 2026-07-27 | Aggregate branch review passed | Reviewed the complete `develop...HEAD` diff and ordered commit sequence locally and through an independent read-only review. No P0/P1/P2 issue, enum-ordinal break, C ABI exposure, test/timeout weakening, architecture promotion, support-claim expansion, or unrelated change was found. The branch remains limited to atomic publication/recovery implementation, its native tests, governing plan/evidence, ADR status evidence, and current repository handoff text. |
| 2026-07-27 | Branch published for human review | Pushed `feature/atomic-publication-recovery` through reviewed commit `0efd02e` and opened draft PR [#2](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/2) into `develop`. The PR remains unmerged; this final mirrored handoff record is the only post-review content change. |
| 2026-07-27 | Human review and merge completed | PR [#2](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/2) was reviewed and merged into `develop` at merge commit `17e481d700096e491698f597d8590988042b8203`. A fresh fetch confirms local and remote `develop` match that commit. The feature is complete; later hardening limits remain tracked rather than silently claimed resolved. |

## Handoff Notes

Phase 1 implementation, required Windows verification, human review, and merge are complete. The merged repair resolves the stale-successor defect, serializes cooperating same-root save/recovery callers, separates API/probe retry evidence, preserves externally changed targets, rolls back only safely owned prefixes, and retains deterministic prepared/committed journal candidates. The current `develop` Windows baseline is 56/56 strict Release and 56/56 MSVC ASan; this does not close a POC, ADR, slice, release, Ubuntu/macOS gate, or KNI support claim.

Remaining hardening is explicit: ADR-0006 handle/inode pinning for noncooperating writers and `save_utf8_atomic` outside the lease; replacement of the locked Unix `Transactions` pathname while its old inode remains advisory-locked; startup journal v1's inability to infer attempted count if a live originally-existing/missing-target restoration itself fails; the ambiguous single-file POSIX case where no-clobber `link` publishes but staged `unlink` fails; and current Linux/macOS execution evidence for the new POSIX paths. Aggregate review and mirror verification are complete. PR [#2](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/2) is merged; subsequent work must branch from current `develop`.
