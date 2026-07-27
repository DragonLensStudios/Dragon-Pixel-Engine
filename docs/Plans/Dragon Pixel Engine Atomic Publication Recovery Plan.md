# Feature Plan: Dragon Pixel Engine Atomic Publication Recovery

> **Status:** In progress — discovery and deterministic reproduction
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

## Contract, ABI, Protocol, or Format Impact

No architecture, public C ABI, managed contract, worker protocol, project format, schema, support claim, or acceptance threshold change is expected. Internal journal/state representation may change only if the existing durable recovery contract already permits it and compatibility/recovery fixtures cover prior states. If discovery requires a public or architectural change, implementation pauses for the mandated Design/Prompt/Result/ADR revision first.

## Security and Data-Safety Considerations

- Resolve and validate every destructive/replacement target before mutation; never operate recursively on a broad or unresolved path.
- Preserve containment across links/reparse points, path aliases, case folding, and concurrent path changes.
- Protect last-known-valid bytes when staging, replacement, validation, or cleanup fails.
- Bind recovery choices to validated identities and hashes rather than filename/timestamp assumptions.
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

## Definition of Done

- [x] Governance PR merged before feature start.
- [x] Current `develop` fetched and used as branch base.
- [x] Required mirrors and `DPE-ARCH-0014` revision checks passed.
- [x] Mirrored feature plan created before source modification.
- [ ] Exact failing publication owner identified.
- [ ] Deterministic reproduction or strongest bounded reproduction recorded.
- [ ] Root cause and last-known-valid invariant documented.
- [ ] Repair implemented in focused commits.
- [ ] Regression tests cover relevant interruption and recovery boundaries.
- [ ] Focused strict Release verification passes.
- [ ] Focused MSVC AddressSanitizer verification passes when applicable.
- [ ] Complete Windows strict Release matrix recorded.
- [ ] Complete Windows MSVC AddressSanitizer matrix recorded.
- [ ] Documentation and exact evidence updated.
- [ ] Aggregate diff and commit sequence reviewed.
- [ ] Branch pushed.
- [ ] Draft PR opened into `develop`.
- [ ] PR left unmerged for human review.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Governance prerequisite satisfied | Governance PR #1 is merged into `develop` at `df092a0715ce841c8c466a11091ebbedf5d5676d`. The merged PR's Windows Release/ASan jobs reported success; its Ubuntu/macOS jobs failed and remain honest unrelated evidence rather than a basis for narrowing this feature. |
| 2026-07-27 | Feature branch prepared | Fetched `origin`, fast-forwarded local `develop` to `df092a0715ce841c8c466a11091ebbedf5d5676d`, confirmed a clean worktree, and created `feature/atomic-publication-recovery` directly from that commit. |
| 2026-07-27 | Required documents verified | The recursive validator passes **57 UTF-8/LF mirrored Markdown pairs** at `DPE-ARCH-0014`. The four required Design, Prompt/Result, First Structure, and Notes pairs match SHA-256; Design and Prompt/Result hashes remain the same complete-read baseline used for the immediately preceding governance work. |
| 2026-07-27 | Plan created | Created this mirrored feature plan before modifying source. Discovery will now identify the exact publication owner, tests, and deterministic failure seam. |

## Handoff Notes

Discovery is beginning. No source, schema, test, architecture, or public-contract file has changed yet. The next step is a read-only inventory of publication/recovery implementations and evidence, followed by a deterministic failing regression test before the smallest repair.
