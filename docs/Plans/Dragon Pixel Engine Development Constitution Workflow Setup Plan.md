# Dragon Pixel Engine Development Constitution Workflow Setup Plan

> **Status:** Ready for review; draft PR #1 open
> **Branch:** `feature/development-constitution-workflow`
> **Target:** `develop`
> **Owner:** Dragon Pixel Engine maintainers
> **Started:** 2026-07-27
> **Updated:** 2026-07-27
> **Design baseline:** `DPE-ARCH-0014` (unchanged)
> **Repository mirror:** `docs/Plans/Dragon Pixel Engine Development Constitution Workflow Setup Plan.md`
> **External mirror:** `C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans\Dragon Pixel Engine Development Constitution Workflow Setup Plan.md`

## Goal

Install the supplied Dragon Pixel Engine Development Constitution Kit as the governing feature-development workflow while preserving the repository's stricter project-specific architecture, documentation-mirroring, evidence, and active-work requirements.

## User Value

Every future feature will begin from current `develop`, use one focused `feature/*` branch and a living feature plan, progress through reviewable commits with tests and exact evidence, and end in a pushed draft pull request to `develop` for human review rather than an unreviewed merge.

## Background

The user supplied `C:\Users\monyd\Downloads\Dragon-Pixel-Engine-Development-Constitution-Kit.zip` (SHA-256 `8407A171827F2FD91CBFB0FD3D6381F0CADDBF2A5F6A73D75809664D9B4CE10D`) and explicitly requested that its workflow become the governing development source before the next feature begins. The archive contains a proposed root `AGENTS.md`, Codex prompts, development handbooks, a feature-plan template, GitHub issue and pull-request templates, and the recommended atomic-publication recovery start prompt.

Pre-change checks on 2026-07-27 established:

- All four required documentation pairs exist and have matching SHA-256 hashes.
- The Design and Prompt/Result documents both declare `DPE-ARCH-0014`, matching root `AGENTS.md`.
- `develop` and `origin/develop` both resolve to `0adbe49a73405dc72bc3d072bed4409c0e30fca3` after fetch.
- The starting worktree was clean.
- The setup branch was created directly from that `develop` commit.
- The kit must be merged with, not blindly substituted for, the existing `AGENTS.md`, because the kit itself requires preservation of more-specific project architecture and documentation rules.

## Scope

- Merge the kit's GitFlow, branch, commit, planning, verification, pull-request, release, and Codex operating rules into root `AGENTS.md`.
- Retain the existing paired-document prerequisites, architecture guardrails, current work boundary, evidence gates, and `DPE-ARCH-0014` status.
- Install the kit's development constitution and handbooks under `docs/Development/` with byte-identical external mirrors.
- Install the Codex master and atomic-publication start prompts under repository `prompts/` as repository workflow controls.
- Install the feature pull-request and issue templates under `.github/`.
- Preserve the kit installation README as mirrored development provenance without replacing the product README.
- Verify Markdown encoding/line endings, mirror equality, links, Git diff hygiene, and the resulting workflow file set.
- Commit the setup in small meaningful commits, push the branch, and open a draft pull request into `develop`.

## Non-Goals

- Do not begin or repair atomic-publication recovery in this work item.
- Do not modify engine, editor, worker, schema, or test behavior.
- Do not change `DPE-ARCH-0014`, any public contract, any ADR status, any POC/slice status, or KNI support status.
- Do not merge the draft pull request.
- Do not add branch-protection or CI enforcement not supplied by the kit; record such enforcement as follow-up if it is not already present.
- Do not replace the product `README.md` with the kit installation README.

## Existing Architecture

This is a development-governance installation at the existing `DPE-ARCH-0014` architecture baseline. The synchronized Design Document, Prompt/Result provenance, master completion plan, active feature evidence, ADR gates, and architecture guardrails remain authoritative in their existing precedence order. The new development constitution governs how future accepted work is branched, planned, implemented, verified, reviewed, and proposed for integration.

## Ownership and State Boundaries

- Root `AGENTS.md` remains the repository-wide agent instruction entry point.
- Mirrored development handbooks and plans are durable documentation owned jointly by the repository `docs` tree and the external documentation system.
- Repository `prompts/` and `.github/` files are versioned workflow controls; they do not mutate project authoring data.
- `develop` is the integration base, `main` is release-only, and each future feature owns one `feature/*` branch until reviewed.
- Human review owns PR readiness and merge authorization; Codex may push and open a draft PR but may not merge without explicit authorization.

## Contract, ABI, Protocol, or Format Impact

No engine contract, C ABI, worker protocol, durable project format, schema, runtime behavior, or architecture revision changes. This work adds repository development-governance contracts only.

## Security and Data-Safety Considerations

- Preserve unrelated work and never rewrite shared history or force-push without explicit instruction.
- Retain transactional authoring and recovery guardrails in root governance.
- Do not expose credentials in prompts, documentation, Git output, commits, or PR text.
- Keep external mirror paths out of packaged engine artifacts; these paths are maintainer workflow metadata only.
- Require exact evidence rather than inferred or invented test/platform results.

## Implementation Increments

### Increment 1: Plan and governing instructions

- Create this mirrored plan before other repository edits.
- Merge the proposed kit `AGENTS.md` into the current repository-specific instructions.
- Verify no project-specific prerequisite, guardrail, work boundary, or evidence status is lost.

### Increment 2: Development documents and workflow controls

- Add the constitution, GitFlow, commit, verification, release, sustainability, and feature-plan documents under mirrored `Development/` paths.
- Add the exact master/start prompts under repository `prompts/`.
- Add the GitHub feature PR and issue templates.
- Preserve the kit README as mirrored installation provenance.

### Increment 3: Verification, evidence, and review handoff

- Run the recursive documentation mirror validator.
- Check UTF-8 without BOM, LF endings, exact mirror hashes, Git whitespace, Markdown links, and YAML syntax where tooling is available.
- Review the complete branch diff and ensure only workflow/governance files changed.
- Update this plan with exact results, limitations, and handoff.
- Commit coherently, push, and open a draft PR to `develop` without merging.

## Test Strategy

- Run `scripts/docs/Test-DocumentationMirrors.ps1` against every mirrored Markdown pair.
- Compare SHA-256 values for all new mirrored development documents and this plan.
- Validate `AGENTS.md` retains `DPE-ARCH-0014`, required mirror mappings, current work boundary, and all architecture guardrails.
- Validate archive-derived files against intended installed content, allowing only the intentional merged `AGENTS.md` and relocated kit README.
- Run `git diff --check` and inspect `git status` plus the aggregate branch diff.
- Parse the feature issue-template YAML if an available parser exists.
- Confirm the branch base and intended PR target.

## Platform Strategy

This documentation-only setup is platform-neutral. Verification runs on the current Windows host and checks deterministic bytes and Git state. It does not constitute Windows engine evidence or any cross-platform engine acceptance result.

## Documentation Requirements

- Keep every new `docs/Development/*.md` and `docs/Plans/*.md` file byte-identical to its external counterpart.
- Record kit source identity, installation decisions, exact verification, and remaining enforcement gaps in this plan.
- Do not revise Design/Prompt/Result or ADR documents because no architecture or public engine contract changes.
- Ensure the final response agrees with this plan's final work log and handoff.

## Risks and Mitigations

- **Risk:** Replacing root `AGENTS.md` drops stricter project rules. **Mitigation:** Merge additively and audit required sections/phrases against both sources.
- **Risk:** The kit's repository-public documentation wording conflicts with mandatory private mirror access. **Mitigation:** Preserve required maintainer mirror verification while clarifying that external contributors cannot claim unavailable mirror checks and must not silently bypass them.
- **Risk:** Root prompts become untracked duplicates of durable documentation. **Mitigation:** Treat them explicitly as repository workflow controls and keep the durable policy in mirrored development documents and plans.
- **Risk:** The workflow appears enforced when only documented. **Mitigation:** State whether branch protection or CI enforcement exists and leave a bounded follow-up rather than claiming it.
- **Risk:** Setup accidentally starts the next feature. **Mitigation:** Install the atomic-publication prompt but do not execute it.

## Definition of Done

- [x] Required document mirrors and revision fields verified before planning.
- [x] Current Design and Prompt/Result documents read completely.
- [x] Current `develop` fetched, confirmed current, and clean.
- [x] Focused setup branch created from current `develop`.
- [x] Mirrored setup plan created before other file edits.
- [x] Root governance merges kit workflow rules without losing project-specific requirements.
- [x] Development handbooks, prompts, and GitHub templates are installed.
- [x] New mirrored documents are UTF-8/LF and byte-identical.
- [x] Workflow and YAML/Markdown verification passes.
- [x] Aggregate branch diff and commit sequence reviewed.
- [x] Exact evidence and remaining gaps recorded.
- [x] Branch pushed.
- [x] Draft pull request opened into `develop`.
- [x] Pull request left unmerged for review.

## Work Log

| Date | State | Evidence / Decision |
| --- | --- | --- |
| 2026-07-27 | Preconditions passed | Four required documentation pairs match by SHA-256; Design and Prompt/Result are complete and agree with root governance on `DPE-ARCH-0014`. |
| 2026-07-27 | Branch prepared | Fetched `origin`; local and remote `develop` both resolve to `0adbe49a73405dc72bc3d072bed4409c0e30fca3`; created `feature/development-constitution-workflow` from the clean integration branch. |
| 2026-07-27 | Plan created | Created this byte-identical mirrored plan before any other repository file modification. The workflow setup explicitly excludes starting atomic-publication recovery. |
| 2026-07-27 | Governing workflow installed | Merged the kit's mission, GitFlow, preparation, planning, commit, verification, PR, release/hotfix, coding, review-disposition, and Codex rules into root `AGENTS.md` while retaining all DPE-ARCH-0014 mirror, precedence, architecture, current-evidence, and acceptance guardrails. Installed the seven packaged handbooks, package README provenance, two operational prompts, and feature PR/issue templates. |
| 2026-07-27 | Initial byte and syntax checks passed | All 12 archive-derived files outside merged `AGENTS.md` match their source-entry SHA-256 exactly. The recursive mirror validator passes **57 UTF-8/LF Markdown pairs** at `DPE-ARCH-0014`; the issue form parses as YAML with six body entries; all repository workflow controls checked so far are UTF-8 without BOM and LF-only; `git diff --check` passes. |
| 2026-07-27 | Independent governance review resolved | Read-only review found no lost architecture guardrail and confirmed that no product feature or source change began. It identified three ambiguities, all corrected before handoff: repository-only external proposals now have an explicit unverified/non-mergeable mirror exception; an early draft PR no longer triggers Codex's final stop; and the master prompt's hard-coded First Feature is explicitly a one-time kickoff that current plans/user direction supersede. |
| 2026-07-27 | Aggregate verification passed | The final bounded verification passes: **57/57** mirrored Markdown pairs; **12/12** archive-derived files outside merged `AGENTS.md` exactly match package hashes; **6/6** repository controls checked are UTF-8/no-BOM/LF; **5/5** README workflow targets exist; **7/7** critical merged-governance phrases are present; feature issue YAML parses with six body entries; and `git diff --check develop` passes after removing Markdown hard-break whitespace found by the first aggregate check. The branch merge-base is the fetched `develop` commit `0adbe49a73405dc72bc3d072bed4409c0e30fca3`. Aggregate scope review finds 16 workflow/governance paths and zero engine source, schema, or test paths. Engine matrices were not rerun because runtime/editor behavior is unchanged. |
| 2026-07-27 | Enforcement gaps recorded | The package installs policy, prompts, and contribution templates, not automated enforcement. GitHub still uses `main` as the default branch, `develop` has no protection/ruleset, merged branches are not automatically deleted, and the existing Slice 1 workflow does not run on pushes to `develop`/`feature/*`. Its latest audited run `30247623903` failed overall and includes untrustworthy Windows success reporting after failed commands/no tests plus Ubuntu provisioning and macOS warning-as-error failures. CI repair and branch protection remain bounded follow-up work; no failing check should become required until it is trustworthy. |
| 2026-07-27 | Review handoff opened | Pushed `feature/development-constitution-workflow` and opened [draft PR #1](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/1) with base `develop` and the exact verification, limitations, and follow-up. GitHub confirms it is open and draft; the existing six Slice 1 workflow jobs started automatically and remain outside this documentation-only verification claim. The PR is intentionally unmerged. |

## Handoff Notes

The documented feature workflow is installed, verified, pushed, and available for human review in [draft PR #1](https://github.com/DragonLensStudios/Dragon-Pixel-Engine/pull/1). The PR remains unmerged. This work intentionally does not claim CI or branch-protection enforcement. After this governance PR is reviewed and merged, begin `feature/atomic-publication-recovery` from the then-current `develop`; do not start it from this branch. The subsequent `feature/ci-gitflow-integration` work must repair CI trust and only then establish appropriate protected-branch required checks.
